#include "EngineBase.h"
#include "AObject.h"
#include "AScene.h"
#include "PersistentId.h"
#include "SchemaHash.h"
#include "TypeInfo.h"
#include "engine/save/SaveHeader.h"
#include "engine/serialization/SceneDoc.h"
#include "engine/serialization/TomlDoc.h"
#include "engine/mod/ModInfo.h"
#include "engine/platform/Input.h"
#include "engine/platform/Time.h"
#include "engine/platform/Paths.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/rendering/Camera.h"
#include "engine/scripting/ScriptHost.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <filesystem>

namespace ark {

EngineBase& EngineBase::Get() {
    static EngineBase instance;
    return instance;
}

void EngineBase::Initialize(int width, int height, const std::string& title) {
    ARK_LOG_INFO("Core", "StarArk Engine initializing...");

    window_ = std::make_unique<Window>(width, height, title);
    Input::Init(window_->GetNativeHandle());
    Time::Init();

    // Pick the first .ico shipped under <exeDir>/icon/ as the window icon.
    // Filename is intentionally not hard-coded so the asset can be swapped
    // (CMake copies whatever .ico is under <repo>/icon/ next to the exe).
    {
        std::error_code ec;
        std::filesystem::path iconDir = Paths::GameRoot() / "icon";
        if (std::filesystem::is_directory(iconDir, ec)) {
            for (const auto& entry : std::filesystem::directory_iterator(iconDir, ec)) {
                if (!entry.is_regular_file()) continue;
                auto ext = entry.path().extension().string();
                std::transform(ext.begin(), ext.end(), ext.begin(),
                               [](unsigned char c) { return std::tolower(c); });
                if (ext == ".ico") {
                    window_->SetIconFromFile(entry.path().string());
                    break;
                }
            }
        }
    }

    rhiDevice_ = CreateOpenGLDevice();
    renderer_ = std::make_unique<ForwardRenderer>(rhiDevice_.get());
    sceneManager_ = std::make_unique<SceneManager>(this);

    // Phase 15.F: boot the C# scripting host. Stub when STARARK_ENABLE_SCRIPTING
    // is OFF; never fatal — engine keeps running without scripts on failure.
    ScriptHost::Get().Initialize(Paths::GameRoot().string());

    // v0.3 ModSpec §6.2 — emit registry-wide schema_hash for traceability.
    // This is the value that will go into save-header active_mods entries
    // once §6.1 lands; logging it now lets us verify stability across runs
    // and detect accidental schema drift in CI.
    {
        const std::string regHash = ComputeRegistrySchemaHash();
        ARK_LOG_INFO("Core",
            "Reflection registry schema_hash=" + regHash
            + " (" + std::to_string(TypeRegistry::Get().All().size()) + " types)");
    }

    // v0.3 ModSpec §6.2 self-test — verify ComputeSchemaHash is:
    //   (a) field-order independent  (same fields, different order → same hash)
    //   (b) field-name sensitive     (rename one field → hash differs)
    //   (c) field-set sensitive      (remove one field → hash differs)
    // Runs once at startup. Failures emit ARK_LOG_ERROR (stderr, CI gate).
    {
        TypeInfo a;
        a.name = "_SchemaHashSelfTestA";
        a.fields = {
            {"intensity", FieldType::Float,  0, 4},
            {"color",     FieldType::Color3, 4, 12},
            {"enabled",   FieldType::Bool,   16, 1},
        };
        TypeInfo b = a;          // same fields, different order
        std::swap(b.fields[0], b.fields[2]);
        TypeInfo c = a;          // rename a field
        c.fields[0].name = "intensity_renamed";
        TypeInfo d = a;          // drop a field
        d.fields.pop_back();

        const std::string ha = ComputeSchemaHash(a);
        const std::string hb = ComputeSchemaHash(b);
        const std::string hc = ComputeSchemaHash(c);
        const std::string hd = ComputeSchemaHash(d);
        if (ha != hb) {
            ARK_LOG_ERROR("Core",
                "SchemaHash self-test FAIL: order should not matter, "
                "got " + ha + " vs " + hb);
        }
        if (ha == hc) {
            ARK_LOG_ERROR("Core",
                "SchemaHash self-test FAIL: rename should change hash, "
                "got " + ha + " == " + hc);
        }
        if (ha == hd) {
            ARK_LOG_ERROR("Core",
                "SchemaHash self-test FAIL: field removal should change hash, "
                "got " + ha + " == " + hd);
        }
        ARK_LOG_INFO("Core",
            "SchemaHash self-test: order-stable=" + std::string(ha == hb ? "yes" : "NO")
            + " rename-detected=" + std::string(ha != hc ? "yes" : "NO")
            + " removal-detected=" + std::string(ha != hd ? "yes" : "NO"));
    }

    // v0.3 ModSpec §4.2 — persistent ID grammar self-test. Validates the
    // IsValidPersistentId predicate that future ark-validate / SceneDoc
    // load-time WARNings rely on. Failures route to ARK_LOG_ERROR (CI gate).
    {
        struct Case { const char* id; bool expect; };
        const Case cases[] = {
            // Positive — canonical persistent IDs.
            {"core:cottage_door",          true},
            {"core:item.iron_sword",       true},
            {"my_addon:secret_chest",      true},
            {"a:b",                        true},
            // Negative — bad shape.
            {"",                           false},
            {"no_colon",                   false},
            {":missing_mod",               false},
            {"missing_local:",             false},
            {"Core:foo",                   false}, // uppercase mod
            {"core:Foo",                   false}, // uppercase local
            {"1bad:start",                 false}, // mod starts with digit
            {"core:.dot_first",            false}, // local empty first part
            {"core:trailing.",             false}, // local empty last part
            {"core:double..dot",           false},
            {"core:has space",             false},
            // UUID v4 must be rejected by persistent-id check, accepted by
            // legacy-uuid check.
            {"00000000-0000-4000-8000-000000000001", false},
        };
        bool allOk = true;
        for (const auto& c : cases) {
            const bool got = IsValidPersistentId(c.id);
            if (got != c.expect) {
                allOk = false;
                ARK_LOG_ERROR("Core",
                    std::string("PersistentId self-test FAIL: '") + c.id
                    + "' got=" + (got ? "true" : "false")
                    + " expected=" + (c.expect ? "true" : "false"));
            }
        }
        // Spot-check legacy UUID acceptor.
        if (!IsValidLegacyUuid("00000000-0000-4000-8000-000000000001")) {
            allOk = false;
            ARK_LOG_ERROR("Core",
                "PersistentId self-test FAIL: legacy UUID rejected by IsValidLegacyUuid");
        }
        if (IsValidLegacyUuid("core:foo")) {
            allOk = false;
            ARK_LOG_ERROR("Core",
                "PersistentId self-test FAIL: persistent id accepted by IsValidLegacyUuid");
        }
        ARK_LOG_INFO("Core",
            std::string("PersistentId self-test: ")
            + (allOk ? "passed" : "FAILED")
            + " (" + std::to_string(sizeof(cases) / sizeof(cases[0]))
            + " cases + 2 legacy spot-checks)");
    }

    // v0.3 ModSpec §6.1 — capture & log a save header reflecting the current
    // session. Pure observation; no save file is written. Verifies the header
    // round-trips through TomlDoc and that CheckCompatibility against itself
    // returns kOk on a clean run.
    {
        const SaveHeader hdr = SaveHeader::CaptureCurrent();
        std::string err;
        SaveHeader rt;
        const bool ok = SaveHeader::ParseString(hdr.DumpString(), &rt, &err);
        ARK_LOG_INFO("Core",
            "Save header captured: pipeline=" + hdr.pipeline
            + " mods=" + std::to_string(hdr.active_mods.size())
            + " engine=" + hdr.engine_version
            + (ok ? " (round-trip OK)"
                  : (" (round-trip FAIL: " + err + ")")));
        if (!ok) {
            ARK_LOG_ERROR("Core", "SaveHeader round-trip failed: " + err);
        } else {
            const auto cmp = CheckCompatibility(rt);
            if (cmp.status != SaveCompatibility::kOk) {
                ARK_LOG_ERROR("Core",
                    "SaveHeader self-compat check failed: " + cmp.detail);
            }
        }
    }

    ARK_LOG_INFO("Core", "Engine initialized successfully");
}

void EngineBase::Shutdown() {
    ARK_LOG_INFO("Core", "StarArk Engine shutting down");
    // Scripts first so managed code can still touch live engine state.
    ScriptHost::Get().Shutdown();
    // Scene is destroyed first
    sceneManager_.reset();
    // Then persistent objects
    persistentPendingList_.clear();
    persistentList_.clear();
    // Then renderer, RHI and window
    renderer_.reset();
    rhiDevice_.reset();
    window_.reset();
}

void EngineBase::MainLoop() {
    // FPS title refresh: average frame times over a ~0.25s window so the
    // number is readable rather than jittering every frame.
    float fpsAccumTime   = 0.0f;
    int   fpsAccumFrames = 0;

    // Fixed-timestep accumulator drives ScriptHost::OnFixedTick (Unity-style
    // FixedUpdate). Step is 1/60s; accumulator capped to avoid the
    // "spiral of death" after long stalls (e.g. a debug breakpoint).
    constexpr float kFixedDt           = 1.0f / 60.0f;
    constexpr float kMaxAccumPerFrame  = 0.25f; // ≤15 substeps per frame
    float fixedAccum = 0.0f;

    while (!window_->ShouldClose()) {
        // 1. Poll Input
        window_->PollEvents();
        Input::Update();

        // 1.5. v0.3 ModSpec §6.1 — quicksave hotkeys (debug-only).
        // F5 writes the current SaveHeader to <UserData>/saves/quicksave.toml,
        // F9 reads it back and runs CheckCompatibility. No scene state is
        // serialized yet — these hotkeys exist to validate the on-disk
        // round-trip end-to-end before §5/§4.2 land.
        if (Input::GetKeyDown(GLFW_KEY_F5)) {
            HandleQuicksave();
        }
        if (Input::GetKeyDown(GLFW_KEY_F9)) {
            HandleQuickload();
        }
        if (Input::GetKeyDown(GLFW_KEY_F6)) {
            HandleApplyOverlays();
        }

        // v0.3 ModSpec §5 — smoke-test hook: when ARK_AUTO_OVERLAY=1 is set,
        // fire HandleApplyOverlays() exactly once a few frames in. This lets
        // headless smoke runs validate the addon overlay path end-to-end via
        // stdout grep without needing keyboard input. Pure debug aid.
        {
            static int  s_autoOverlayFrame = 0;
            static bool s_autoOverlayDone  = false;
            if (!s_autoOverlayDone) {
                ++s_autoOverlayFrame;
                const char* env = std::getenv("ARK_AUTO_OVERLAY");
                if (env && std::string(env) == "1" && s_autoOverlayFrame >= 5) {
                    ARK_LOG_INFO("SceneOverlay",
                        "ARK_AUTO_OVERLAY=1 — auto-applying overlays.");
                    HandleApplyOverlays();
                    s_autoOverlayDone = true;
                }
            }
        }

        // 2. Time update
        Time::Update();
        float dt = Time::DeltaTime();

        // FPS title (updated ~4x/sec)
        fpsAccumTime   += dt;
        fpsAccumFrames += 1;
        if (fpsAccumTime >= 0.25f) {
            float fps = float(fpsAccumFrames) / fpsAccumTime;
            float ms  = fpsAccumTime * 1000.0f / float(fpsAccumFrames);
            char buf[160];
            std::snprintf(buf, sizeof(buf), "%s  |  %.0f FPS  (%.2f ms)",
                          window_->GetBaseTitle().c_str(), fps, ms);
            window_->SetTitle(buf);
            fpsAccumTime   = 0.0f;
            fpsAccumFrames = 0;
        }

        // 3. Check window resize
        if (window_->WasResized()) {
            window_->ResetResizeFlag();
            // Update camera aspect ratios. Skip on minimize (height==0)
            // to keep the previous aspect — glm::perspective asserts on
            // aspect ≈ 0 / NaN.
            const int w = window_->GetWidth();
            const int h = window_->GetHeight();
            if (w > 0 && h > 0) {
                const float aspect = static_cast<float>(w) / static_cast<float>(h);
                for (auto* cam : Camera::GetAllCameras()) {
                    cam->SetAspectRatio(aspect);
                }
            }
        }

        // 4. PreInit/Init/PostInit new objects
        DrainPendingObjects();

        // 4.5a. MOD FixedLoop — drain the fixed-timestep accumulator.
        // Same semantics as Unity FixedUpdate: zero or more calls per frame
        // at exactly kFixedDt seconds each. Runs before MOD Loop so MODs can
        // see physics-style state ahead of game logic.
        fixedAccum += dt;
        if (fixedAccum > kMaxAccumPerFrame) fixedAccum = kMaxAccumPerFrame;
        while (fixedAccum >= kFixedDt) {
            ScriptHost::Get().OnFixedTick(kFixedDt);
            fixedAccum -= kFixedDt;
        }

        // 4.5b. MOD Loop — runs before native StepTick so MOD scripts can
        // observe the previous frame's state plus objects spawned during
        // DrainPendingObjects, and influence what runs this frame.
        ScriptHost::Get().OnTick(dt);

        // 5-6. Loop
        StepTick(dt);

        // 7-8. PostLoop
        StepPostTick(dt);

        // 8.5. MOD PostLoop — after native PostLoop so MOD scripts see the
        // fully-updated world before transforms/render.
        ScriptHost::Get().OnPostTick(dt);

        // 9. Scene-level Tick
        AScene* scene = sceneManager_->GetActiveScene();
        if (scene) {
            scene->Tick(dt);
        }

        // 10. Update transforms
        StepUpdateTransforms();

        // 11. Render — ForwardRenderer iterates cameras, collects MeshRenderers, draws
        renderer_->RenderFrame(window_.get());

        // 12. Destroy objects
        StepDestroyObjects();

        // 13. Process pending scene switch
        if (sceneManager_->HasPendingSwitch()) {
            sceneManager_->ProcessPendingSwitch();
        }

        // 14. Swap buffers
        window_->SwapBuffers();
    }
}

void EngineBase::DrainPendingObjects() {
    AScene* scene = sceneManager_->GetActiveScene();

    // Drain loop: keep processing until both pending lists are empty
    bool hasPending = true;
    while (hasPending) {
        hasPending = false;

        // Scene pending
        if (scene && !scene->GetPendingList().empty()) {
            hasPending = true;
            auto batch = std::move(scene->GetPendingList());
            scene->GetPendingList().clear();

            for (auto& obj : batch) {
                if (obj->IsDestroyed()) {
                    // Skip: destructor handles cleanup
                    continue;
                }
                AObject* raw = obj.get();
                scene->GetObjectList().push_back(std::move(obj));
                raw->PreInit();
                raw->Init();
                raw->PostInit();
            }
        }

        // Engine persistent pending
        if (!persistentPendingList_.empty()) {
            hasPending = true;
            auto batch = std::move(persistentPendingList_);
            persistentPendingList_.clear();

            for (auto& obj : batch) {
                if (obj->IsDestroyed()) {
                    continue;
                }
                AObject* raw = obj.get();
                persistentList_.push_back(std::move(obj));
                raw->PreInit();
                raw->Init();
                raw->PostInit();
            }
        }
    }
}

void EngineBase::StepTick(float dt) {
    // 5. Loop persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
            obj->Loop(dt);
            obj->LoopComponents(dt);
        }
    }

    // 6. Loop scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
                obj->Loop(dt);
                obj->LoopComponents(dt);
            }
        }
    }
}

void EngineBase::StepPostTick(float dt) {
    // 7. PostLoop persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
            obj->PostLoop(dt);
            obj->PostLoopComponents(dt);
        }
    }

    // 8. PostLoop scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->IsActiveInHierarchy()) {
                obj->PostLoop(dt);
                obj->PostLoopComponents(dt);
            }
        }
    }
}

void EngineBase::StepUpdateTransforms() {
    // Update dirty transforms for persistent objects
    for (auto& obj : persistentList_) {
        if (!obj->IsDestroyed() && obj->GetTransform().GetParent() == nullptr) {
            if (obj->GetTransform().IsDirty()) {
                obj->GetTransform().UpdateWorldMatrix();
            }
        }
    }

    // Update dirty transforms for scene objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene) {
        for (auto& obj : scene->GetObjectList()) {
            if (!obj->IsDestroyed() && obj->GetTransform().GetParent() == nullptr) {
                if (obj->GetTransform().IsDirty()) {
                    obj->GetTransform().UpdateWorldMatrix();
                }
            }
        }
    }
}

void EngineBase::StepDestroyObjects() {
    // 12. Scan and remove destroyed objects
    AScene* scene = sceneManager_->GetActiveScene();
    if (scene && scene->HasDestroyedObjects()) {
        auto& list = scene->GetObjectList();
        list.erase(
            std::remove_if(list.begin(), list.end(),
                [](const std::unique_ptr<AObject>& obj) { return obj->IsDestroyed(); }),
            list.end());
        scene->ClearDestroyedFlag();
    }

    if (hasDestroyedPersistent_) {
        persistentList_.erase(
            std::remove_if(persistentList_.begin(), persistentList_.end(),
                [](const std::unique_ptr<AObject>& obj) { return obj->IsDestroyed(); }),
            persistentList_.end());
        hasDestroyedPersistent_ = false;
    }
}

void EngineBase::AcceptPersistentObject(std::unique_ptr<AObject> obj, bool isPending) {
    obj->SetOwnerInterface(this);
    if (isPending) {
        persistentPendingList_.push_back(std::move(obj));
    } else {
        persistentList_.push_back(std::move(obj));
    }
}

void EngineBase::TransferToPersistent(AObject* obj) {
    // Already persistent — no-op
    (void)obj;
}

void EngineBase::HandleQuicksave() {
    namespace fs = std::filesystem;
    const fs::path savesDir = Paths::UserData() / "saves";
    std::error_code ec;
    fs::create_directories(savesDir, ec);
    if (ec) {
        ARK_LOG_ERROR("Save", "Failed to create saves directory '"
                      + savesDir.string() + "': " + ec.message());
        return;
    }
    const fs::path headerFile = savesDir / "quicksave.toml";
    const fs::path sceneFile  = savesDir / "quicksave.scene.toml";

    const SaveHeader hdr  = SaveHeader::CaptureCurrent();
    const std::string text = hdr.DumpString();

    std::FILE* f = std::fopen(headerFile.string().c_str(), "wb");
    if (!f) {
        ARK_LOG_ERROR("Save", "Cannot open '" + headerFile.string() + "' for write");
        return;
    }
    std::fwrite(text.data(), 1, text.size(), f);
    std::fclose(f);

    // Sidecar scene dump — reuses SceneDoc reflection-driven serializer.
    // Two-file layout (header + scene) avoids embedding raw TOML inside
    // TOML and lets the same SceneDoc::Load path consume the scene file
    // directly once we wire actual scene-state restore in v0.4.
    AScene* scene = sceneManager_ ? sceneManager_->GetActiveScene() : nullptr;
    std::size_t objectCount = 0;
    if (scene) {
        const std::string sceneText = SceneDoc::Dump(scene);
        std::FILE* sf = std::fopen(sceneFile.string().c_str(), "wb");
        if (!sf) {
            ARK_LOG_ERROR("Save", "Cannot open '" + sceneFile.string() + "' for write");
        } else {
            std::fwrite(sceneText.data(), 1, sceneText.size(), sf);
            std::fclose(sf);
            objectCount = scene->GetObjectList().size();
        }
    }

    ARK_LOG_INFO("Save",
        "Quicksave written: " + headerFile.string()
        + " (" + std::to_string(text.size()) + " bytes header, "
        + std::to_string(objectCount) + " scene objects, "
        + std::to_string(hdr.active_mods.size()) + " mods)");
}

void EngineBase::HandleQuickload() {
    namespace fs = std::filesystem;
    const fs::path headerFile = Paths::UserData() / "saves" / "quicksave.toml";
    const fs::path sceneFile  = Paths::UserData() / "saves" / "quicksave.scene.toml";
    std::error_code ec;
    if (!fs::exists(headerFile, ec)) {
        ARK_LOG_WARN("Save", "Quickload: no save at '" + headerFile.string() + "'");
        return;
    }

    auto readAll = [](const fs::path& p, std::string* out) -> bool {
        std::FILE* f = std::fopen(p.string().c_str(), "rb");
        if (!f) return false;
        std::fseek(f, 0, SEEK_END);
        const long sz = std::ftell(f);
        std::fseek(f, 0, SEEK_SET);
        out->assign(sz > 0 ? static_cast<size_t>(sz) : 0, '\0');
        if (sz > 0) std::fread(out->data(), 1, static_cast<size_t>(sz), f);
        std::fclose(f);
        return true;
    };

    std::string text;
    if (!readAll(headerFile, &text)) {
        ARK_LOG_ERROR("Save", "Cannot open '" + headerFile.string() + "' for read");
        return;
    }

    SaveHeader hdr;
    std::string err;
    if (!SaveHeader::ParseString(text, &hdr, &err)) {
        ARK_LOG_ERROR("Save", "Quickload parse failed: " + err);
        return;
    }

    const auto cmp = CheckCompatibility(hdr);
    const char* statusName = "OK";
    switch (cmp.status) {
        case SaveCompatibility::kOk:              statusName = "OK"; break;
        case SaveCompatibility::kMissingMod:      statusName = "MissingMod"; break;
        case SaveCompatibility::kVersionMismatch: statusName = "VersionMismatch"; break;
        case SaveCompatibility::kSchemaMismatch:  statusName = "SchemaMismatch"; break;
        case SaveCompatibility::kEngineDowngrade: statusName = "EngineDowngrade"; break;
    }

    // Sidecar scene parse + actual scene restore.
    //   1) Parse TOML up-front so a corrupt sidecar doesn't wipe the live
    //      scene before bailing.
    //   2) AScene::Clear() — drop every live + pending object (persistent
    //      / DontDestroy objects owned by EngineBase are untouched).
    //   3) SceneDoc::LoadFromString — recreate objects, transforms, and
    //      reflected components from sidecar.
    // If the header was rejected by CheckCompatibility we do NOT mutate the
    // scene — only count [[objects]] for the diagnostic log.
    std::size_t sceneObjectCount = 0;
    bool sceneParseOk = true;
    AScene* activeScene = sceneManager_ ? sceneManager_->GetActiveScene() : nullptr;
    if (fs::exists(sceneFile, ec)) {
        std::string sceneText;
        if (!readAll(sceneFile, &sceneText)) {
            sceneParseOk = false;
            ARK_LOG_ERROR("Save", "Cannot read scene sidecar '" + sceneFile.string() + "'");
        } else {
            std::string sceneErr;
            int errLine = 0;
            auto parsed = TomlDoc::Parse(sceneText, &sceneErr, &errLine);
            if (!parsed) {
                sceneParseOk = false;
                ARK_LOG_ERROR("Save", "Quickload scene parse failed at line "
                              + std::to_string(errLine) + ": " + sceneErr);
            } else if (cmp.status == SaveCompatibility::kOk && activeScene) {
                activeScene->Clear();
                std::string applyErr;
                if (!SceneDoc::LoadFromString(sceneText, activeScene, &applyErr)) {
                    sceneParseOk = false;
                    ARK_LOG_ERROR("Save", "Quickload scene restore failed: " + applyErr);
                } else {
                    sceneObjectCount = activeScene->GetObjectList().size()
                                       + activeScene->GetPendingList().size();
                }
            } else if (const auto* aot = parsed->Root().FindArrayOfTables("objects")) {
                sceneObjectCount = aot->Size();
            }
        }
    }

    if (cmp.status == SaveCompatibility::kOk && sceneParseOk) {
        ARK_LOG_INFO("Save",
            "Quickload OK: pipeline=" + hdr.pipeline
            + " mods=" + std::to_string(hdr.active_mods.size())
            + " engine=" + hdr.engine_version
            + " scene_objects=" + std::to_string(sceneObjectCount));
    } else if (cmp.status != SaveCompatibility::kOk) {
        ARK_LOG_WARN("Save",
            std::string("Quickload incompatible (") + statusName + "): " + cmp.detail);
    }
}

void EngineBase::HandleApplyOverlays() {
    AScene* scene = sceneManager_ ? sceneManager_->GetActiveScene() : nullptr;
    if (!scene) {
        ARK_LOG_WARN("SceneOverlay", "No active scene; F6 no-op.");
        return;
    }

    namespace fs = std::filesystem;
    int filesApplied = 0;
    for (const auto& id : Paths::EnabledModIds()) {
        const ModInfo* info = Paths::FindModInfo(id);
        if (!info || !info->valid) continue;
        if (info->type != ModType::Addon) continue;  // §5 = addon-only

        const fs::path overlay = info->root / "scene.overlay.toml";
        std::error_code ec;
        if (!fs::exists(overlay, ec)) continue;

        Paths::ModContextScope scope(info->id);
        if (SceneDoc::ApplyOverlay(overlay, scene)) {
            ++filesApplied;
            ARK_LOG_INFO("SceneOverlay", "applied overlay from mod '" + info->id + "'");
        }
    }
    if (filesApplied == 0) {
        ARK_LOG_INFO("SceneOverlay", "F6: no scene.overlay.toml found among enabled addons.");
    }
}

} // namespace ark
