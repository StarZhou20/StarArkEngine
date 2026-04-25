// ScriptHost.cpp — Phase 15.F.1.
//
// Two implementations selected by ARK_SCRIPTING_ENABLED (CMake option
// STARARK_ENABLE_SCRIPTING):
//
//   == 0  No-op stub. Engine compiles without any .NET dependency.
//   == 1  Real CoreCLR backend. Boots .NET 10 via nethost + hostfxr,
//         loads <exeDir>/scripting/StarArk.Scripting.dll and resolves
//         OnEngineStart / OnFrame / OnEngineStop entry points
//         (declared with [UnmanagedCallersOnly] on the managed side).
//
// All hostfxr APIs receive WIDE strings on Windows (`char_t = wchar_t`).
// Helper Utf8ToWide() handles conversion from std::string at the boundary.

#include "engine/scripting/ScriptHost.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"
#include "engine/platform/Time.h"
#include "engine/platform/Input.h"
#include "engine/core/EngineBase.h"
#include "engine/core/SceneManager.h"
#include "engine/core/AScene.h"
#include "engine/core/AObject.h"
#include "engine/core/Transform.h"

#include <filesystem>
#include <cstdio>
#include <cstring>
#include <string>

#if defined(ARK_SCRIPTING_ENABLED) && ARK_SCRIPTING_ENABLED == 1
  #if defined(_WIN32)
    #define WIN32_LEAN_AND_MEAN
    #include <windows.h>
  #else
    #include <dlfcn.h>
  #endif
  #include <nethost.h>
  #include <coreclr_delegates.h>
  #include <hostfxr.h>
  #include <string>
#endif

namespace ark {

struct ScriptHostImpl {
    bool initialized = false;
    bool active      = false;
    std::string gameRoot;

#if defined(ARK_SCRIPTING_ENABLED) && ARK_SCRIPTING_ENABLED == 1
  #if defined(_WIN32)
    HMODULE hostfxrLib = nullptr;
  #else
    void*   hostfxrLib = nullptr;
  #endif
    hostfxr_initialize_for_runtime_config_fn fxrInit        = nullptr;
    hostfxr_get_runtime_delegate_fn          fxrGetDelegate = nullptr;
    hostfxr_close_fn                         fxrClose       = nullptr;
    hostfxr_handle                           fxrCtx         = nullptr;

    // Cached managed entry points. Signatures must mirror the C# methods
    // marked [UnmanagedCallersOnly] in scripts/StarArk.Scripting/Engine.cs.
    // OnEngineStart receives a pointer to the ScriptApi function table and
    // a UTF-8 path to the mods directory; managed side stores both then
    // proceeds to scan mods.
    int (*onStart)    (const ScriptApi*, const char*) = nullptr;
    int (*onFixedTick)(float)                          = nullptr;
    int (*onTick)     (float)                          = nullptr;
    int (*onPostTick) (float)                          = nullptr;
    int (*onStop)     ()                               = nullptr;

    // The ScriptApi instance handed to managed code. Lives as long as the
    // singleton so function pointers stored on the managed side stay valid.
    ScriptApi api{};
#endif
};

ScriptHost::ScriptHost() : impl_(std::make_unique<ScriptHostImpl>()) {}
ScriptHost::~ScriptHost() = default;

ScriptHost& ScriptHost::Get() {
    static ScriptHost instance;
    return instance;
}

bool ScriptHost::IsActive() const {
    return impl_->active;
}

#if !defined(ARK_SCRIPTING_ENABLED) || ARK_SCRIPTING_ENABLED == 0

// =====================================================================
// Stub backend (default build)
// =====================================================================

bool ScriptHost::Initialize(const std::string& gameRoot) {
    if (impl_->initialized) return true;
    impl_->gameRoot    = gameRoot;
    impl_->initialized = true;
    impl_->active      = false;
    ARK_LOG_INFO("Scripting",
        "ScriptHost initialized in stub mode (CoreCLR disabled at build time). "
        "Rebuild with -DSTARARK_ENABLE_SCRIPTING=ON to enable C# MOD scripts.");
    return true;
}

void ScriptHost::Shutdown() {
    if (!impl_->initialized) return;
    impl_->initialized = false;
    impl_->active      = false;
    ARK_LOG_INFO("Scripting", "ScriptHost shut down (stub).");
}

void ScriptHost::OnFixedTick(float /*dt*/) {}
void ScriptHost::OnTick(float /*dt*/)      {}
void ScriptHost::OnPostTick(float /*dt*/)  {}

#else  // ARK_SCRIPTING_ENABLED == 1

// =====================================================================
// CoreCLR backend
// =====================================================================

namespace {

#if defined(_WIN32)
    using StringT = std::wstring;

    StringT Utf8ToWide(const std::string& s) {
        if (s.empty()) return L"";
        int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, nullptr, 0);
        if (n <= 0) return L"";
        StringT w(static_cast<size_t>(n - 1), L'\0');
        MultiByteToWideChar(CP_UTF8, 0, s.c_str(), -1, w.data(), n);
        return w;
    }

    HMODULE LoadLib(const StringT& path) {
        return LoadLibraryW(path.c_str());
    }
    template <typename T>
    T GetSym(HMODULE m, const char* name) {
        return reinterpret_cast<T>(GetProcAddress(m, name));
    }
#else
    using StringT = std::string;
    StringT Utf8ToWide(const std::string& s) { return s; }
    void* LoadLib(const StringT& path) { return dlopen(path.c_str(), RTLD_LAZY | RTLD_LOCAL); }
    template <typename T>
    T GetSym(void* m, const char* name) {
        return reinterpret_cast<T>(dlsym(m, name));
    }
#endif

    // Convert hostfxr's `const char_t*` (wchar_t* on Windows, char* elsewhere)
    // into a UTF-8 std::string for logging.
    std::string ToUtf8(const char_t* s) {
#if defined(_WIN32)
        if (!s) return {};
        int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
        if (n <= 0) return {};
        std::string out(static_cast<size_t>(n - 1), '\0');
        WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
        return out;
#else
        return s ? std::string(s) : std::string();
#endif
    }

    bool LoadHostfxr(ScriptHostImpl& s) {
        // Step 1: ask nethost where hostfxr.dll lives.
        char_t buffer[1024];
        size_t bufferSize = sizeof(buffer) / sizeof(char_t);
        int rc = get_hostfxr_path(buffer, &bufferSize, nullptr);
        if (rc != 0) {
            ARK_LOG_WARN("Scripting",
                "get_hostfxr_path failed (rc=" + std::to_string(rc) +
                "). Is the .NET runtime installed?");
            return false;
        }
        ARK_LOG_INFO("Scripting", "hostfxr located: " + ToUtf8(buffer));

        s.hostfxrLib = LoadLib(buffer);
        if (!s.hostfxrLib) {
            ARK_LOG_WARN("Scripting", "Failed to load hostfxr library");
            return false;
        }
        s.fxrInit = GetSym<hostfxr_initialize_for_runtime_config_fn>(
            s.hostfxrLib, "hostfxr_initialize_for_runtime_config");
        s.fxrGetDelegate = GetSym<hostfxr_get_runtime_delegate_fn>(
            s.hostfxrLib, "hostfxr_get_runtime_delegate");
        s.fxrClose = GetSym<hostfxr_close_fn>(
            s.hostfxrLib, "hostfxr_close");
        if (!s.fxrInit || !s.fxrGetDelegate || !s.fxrClose) {
            ARK_LOG_WARN("Scripting", "Failed to resolve hostfxr exports");
            return false;
        }
        return true;
    }

    std::string FormatHex(int v) {
        char buf[16];
        std::snprintf(buf, sizeof(buf), "0x%X", v);
        return std::string(buf);
    }

    // -----------------------------------------------------------------
    // Native trampolines exposed to managed code via ScriptApi.
    // All have C linkage and cdecl by default on x64. They must never
    // throw — managed callers don't tolerate SEH propagating across the
    // boundary on Windows.
    // -----------------------------------------------------------------
    extern "C" void ArkApi_Log(int level, const char* category, const char* message) {
        const char* cat = category ? category : "Mod";
        const char* msg = message  ? message  : "";
        // Map managed LogLevel ints (matches enum class LogLevel) defensively.
        ::ark::LogLevel lv = ::ark::LogLevel::Info;
        switch (level) {
            case 0: lv = ::ark::LogLevel::Trace;   break;
            case 1: lv = ::ark::LogLevel::Debug;   break;
            case 2: lv = ::ark::LogLevel::Info;    break;
            case 3: lv = ::ark::LogLevel::Warning; break;
            case 4: lv = ::ark::LogLevel::Error;   break;
            case 5: lv = ::ark::LogLevel::Fatal;   break;
            default: break;
        }
        ::ark::DebugListenBus::Get().Broadcast(lv, cat, msg);
    }
    extern "C" float ArkApi_GetDeltaTime() { return ::ark::Time::DeltaTime(); }
    extern "C" float ArkApi_GetTotalTime() { return ::ark::Time::TotalTime(); }
    extern "C" int   ArkApi_GetKey(int code)     { return ::ark::Input::GetKey(code)     ? 1 : 0; }
    extern "C" int   ArkApi_GetKeyDown(int code) { return ::ark::Input::GetKeyDown(code) ? 1 : 0; }

    // ─────────────────────────────────────────────────────────────────
    // v2 — Object / Transform / Scene access.
    //
    // Lookup model: handle == AObject::GetId(). 0 reserved for "invalid".
    // We linearly scan the active scene's object list (and pending list,
    // so freshly spawned objects are findable in the same frame). The
    // engine's persistent list is NOT exposed yet — script-spawned objects
    // always live in the active scene.
    //
    // Concurrency: managed code only calls these from the script tick,
    // which is on the engine thread between native steps, so no locking.
    // ─────────────────────────────────────────────────────────────────

    static ::ark::AObject* ArkLookup_(::ark::ArkObjectHandle id) {
        if (id == 0) return nullptr;
        auto* sm = ::ark::EngineBase::Get().GetSceneManager();
        if (!sm) return nullptr;
        ::ark::AScene* scene = sm->GetActiveScene();
        if (!scene) return nullptr;
        for (auto& obj : scene->GetObjectList()) {
            if (obj && !obj->IsDestroyed() && obj->GetId() == id) return obj.get();
        }
        for (auto& obj : scene->GetPendingList()) {
            if (obj && !obj->IsDestroyed() && obj->GetId() == id) return obj.get();
        }
        return nullptr;
    }

    static int ArkCopyString_(const std::string& s, char* buf, int bufLen) {
        if (!buf || bufLen <= 0) return static_cast<int>(s.size()) + 1;
        int n = static_cast<int>(s.size());
        if (n >= bufLen) n = bufLen - 1;
        std::memcpy(buf, s.data(), static_cast<size_t>(n));
        buf[n] = '\0';
        return n + 1;
    }

    extern "C" ::ark::ArkObjectHandle ArkApi_FindObjectByName(const char* name) {
        if (!name) return 0;
        auto* sm = ::ark::EngineBase::Get().GetSceneManager();
        if (!sm) return 0;
        ::ark::AScene* scene = sm->GetActiveScene();
        if (!scene) return 0;
        for (auto& obj : scene->GetObjectList()) {
            if (obj && !obj->IsDestroyed() && obj->GetName() == name) return obj->GetId();
        }
        for (auto& obj : scene->GetPendingList()) {
            if (obj && !obj->IsDestroyed() && obj->GetName() == name) return obj->GetId();
        }
        return 0;
    }

    extern "C" ::ark::ArkObjectHandle ArkApi_SpawnObject(const char* name) {
        auto* sm = ::ark::EngineBase::Get().GetSceneManager();
        if (!sm) return 0;
        ::ark::AScene* scene = sm->GetActiveScene();
        if (!scene) return 0;
        // CreateObject<AObject>() creates and pushes onto pendingList_; the
        // engine will Init/PostInit on the next DrainPendingObjects().
        auto* raw = scene->CreateObject<::ark::AObject>();
        if (!raw) return 0;
        if (name && *name) raw->SetName(name);
        return raw->GetId();
    }

    extern "C" int ArkApi_DestroyObject(::ark::ArkObjectHandle h) {
        ::ark::AObject* obj = ArkLookup_(h);
        if (!obj) return 0;
        obj->Destroy();
        return 1;
    }

    extern "C" int ArkApi_GetObjectName(::ark::ArkObjectHandle h, char* buf, int bufLen) {
        ::ark::AObject* obj = ArkLookup_(h);
        if (!obj) { if (buf && bufLen > 0) buf[0] = '\0'; return 0; }
        return ArkCopyString_(obj->GetName(), buf, bufLen);
    }

    extern "C" int ArkApi_SetObjectName(::ark::ArkObjectHandle h, const char* name) {
        ::ark::AObject* obj = ArkLookup_(h);
        if (!obj || !name) return 0;
        obj->SetName(name);
        return 1;
    }

    extern "C" int ArkApi_GetPosition(::ark::ArkObjectHandle h, float* x, float* y, float* z) {
        ::ark::AObject* obj = ArkLookup_(h);
        if (!obj) return 0;
        const auto& p = obj->GetTransform().GetLocalPosition();
        if (x) *x = p.x; if (y) *y = p.y; if (z) *z = p.z;
        return 1;
    }

    extern "C" int ArkApi_SetPosition(::ark::ArkObjectHandle h, float x, float y, float z) {
        ::ark::AObject* obj = ArkLookup_(h);
        if (!obj) return 0;
        obj->GetTransform().SetLocalPosition(glm::vec3(x, y, z));
        return 1;
    }

    extern "C" int ArkApi_GetActiveSceneName(char* buf, int bufLen) {
        auto* sm = ::ark::EngineBase::Get().GetSceneManager();
        if (!sm) { if (buf && bufLen > 0) buf[0] = '\0'; return 0; }
        ::ark::AScene* scene = sm->GetActiveScene();
        if (!scene) { if (buf && bufLen > 0) buf[0] = '\0'; return 0; }
        return ArkCopyString_(scene->GetSceneName(), buf, bufLen);
    }

    void FillScriptApi(ScriptApi& api) {
        api.version              = ARK_SCRIPT_API_VERSION;
        // v1
        api.log                  = &ArkApi_Log;
        api.getDeltaTime         = &ArkApi_GetDeltaTime;
        api.getTotalTime         = &ArkApi_GetTotalTime;
        api.getKey               = &ArkApi_GetKey;
        api.getKeyDown           = &ArkApi_GetKeyDown;
        // v2
        api.findObjectByName     = &ArkApi_FindObjectByName;
        api.spawnObject          = &ArkApi_SpawnObject;
        api.destroyObject        = &ArkApi_DestroyObject;
        api.getObjectName        = &ArkApi_GetObjectName;
        api.setObjectName        = &ArkApi_SetObjectName;
        api.getPosition          = &ArkApi_GetPosition;
        api.setPosition          = &ArkApi_SetPosition;
        api.getActiveSceneName   = &ArkApi_GetActiveSceneName;
    }

} // namespace

bool ScriptHost::Initialize(const std::string& gameRoot) {
    if (impl_->initialized) return true;
    impl_->gameRoot    = gameRoot;
    impl_->initialized = true;
    impl_->active      = false;

    namespace fs = std::filesystem;
    fs::path scriptingDir = fs::path(gameRoot) / "scripting";
    fs::path runtimeCfg   = scriptingDir / "StarArk.Scripting.runtimeconfig.json";
    fs::path assemblyPath = scriptingDir / "StarArk.Scripting.dll";

    if (!fs::exists(runtimeCfg) || !fs::exists(assemblyPath)) {
        ARK_LOG_WARN("Scripting",
            "Scripting deploy missing under " + scriptingDir.string() +
            " (need StarArk.Scripting.dll + .runtimeconfig.json). "
            "Engine will run without scripts.");
        return false;
    }

    if (!LoadHostfxr(*impl_)) return false;

    StringT cfgW = Utf8ToWide(runtimeCfg.string());
    int rc = impl_->fxrInit(cfgW.c_str(), nullptr, &impl_->fxrCtx);
    // Success_HostAlreadyInitialized (1) is also acceptable.
    if (rc != 0 && rc != 1) {
        ARK_LOG_WARN("Scripting",
            "hostfxr_initialize_for_runtime_config failed (rc=" +
            FormatHex(rc) + ") cfg=" + runtimeCfg.string());
        if (impl_->fxrCtx) { impl_->fxrClose(impl_->fxrCtx); impl_->fxrCtx = nullptr; }
        return false;
    }

    load_assembly_and_get_function_pointer_fn loadFn = nullptr;
    rc = impl_->fxrGetDelegate(
        impl_->fxrCtx,
        hdt_load_assembly_and_get_function_pointer,
        reinterpret_cast<void**>(&loadFn));
    if (rc != 0 || !loadFn) {
        ARK_LOG_WARN("Scripting",
            "hostfxr_get_runtime_delegate failed (rc=" + FormatHex(rc) + ")");
        impl_->fxrClose(impl_->fxrCtx);
        impl_->fxrCtx = nullptr;
        return false;
    }

    StringT asmW = Utf8ToWide(assemblyPath.string());
#if defined(_WIN32)
    const wchar_t* typeName = L"StarArk.Scripting.Engine, StarArk.Scripting";
#else
    const char*    typeName = "StarArk.Scripting.Engine, StarArk.Scripting";
#endif

    auto Resolve = [&](const char_t* method, void** out) -> bool {
        int r = loadFn(asmW.c_str(), typeName, method,
                       UNMANAGEDCALLERSONLY_METHOD, nullptr, out);
        if (r != 0 || !*out) {
            ARK_LOG_WARN("Scripting",
                "Failed to resolve managed method: " + ToUtf8(method) +
                " (rc=" + FormatHex(r) + ")");
            return false;
        }
        return true;
    };

    void* p = nullptr;
#if defined(_WIN32)
    #define ARK_LIT(s) L##s
#else
    #define ARK_LIT(s) s
#endif
    if (!Resolve(ARK_LIT("OnEngineStart"), &p)) return false;
    impl_->onStart     = reinterpret_cast<int(*)(const ScriptApi*, const char*)>(p);
    if (!Resolve(ARK_LIT("OnFixedTick"), &p)) return false;
    impl_->onFixedTick = reinterpret_cast<int(*)(float)>(p);
    if (!Resolve(ARK_LIT("OnTick"), &p)) return false;
    impl_->onTick      = reinterpret_cast<int(*)(float)>(p);
    if (!Resolve(ARK_LIT("OnPostTick"), &p)) return false;
    impl_->onPostTick  = reinterpret_cast<int(*)(float)>(p);
    if (!Resolve(ARK_LIT("OnEngineStop"), &p)) return false;
    impl_->onStop      = reinterpret_cast<int(*)()>(p);
#undef ARK_LIT

    ARK_LOG_INFO("Scripting", "CoreCLR runtime up; managed entry points bound.");
    impl_->active = true;

    // Build native function pointer table and locate mods directory. Managed
    // code is responsible for scanning <modsDir>/<modname>/scripts/*.dll —
    // doing it from the managed side lets us use AssemblyLoadContext directly
    // and keeps the C++ host minimal.
    FillScriptApi(impl_->api);
    fs::path modsDir = fs::path(gameRoot) / "mods";
    std::string modsDirUtf8 = modsDir.string();
    impl_->onStart(&impl_->api, modsDirUtf8.c_str());
    return true;
}

void ScriptHost::Shutdown() {
    if (!impl_->initialized) return;

    if (impl_->active && impl_->onStop) {
        impl_->onStop();
    }

    if (impl_->fxrCtx && impl_->fxrClose) {
        impl_->fxrClose(impl_->fxrCtx);
        impl_->fxrCtx = nullptr;
    }
    // Don't FreeLibrary(hostfxr) — CoreCLR cannot be re-hosted in the same
    // process anyway, and unloading on shutdown can race with managed
    // finalizers. Leave the OS to reclaim it.

    impl_->onStart     = nullptr;
    impl_->onFixedTick = nullptr;
    impl_->onTick      = nullptr;
    impl_->onPostTick  = nullptr;
    impl_->onStop      = nullptr;
    impl_->initialized = false;
    impl_->active      = false;
    ARK_LOG_INFO("Scripting", "ScriptHost shut down (CoreCLR).");
}

void ScriptHost::OnFixedTick(float fixedDt) {
    if (!impl_->active || !impl_->onFixedTick) return;
    impl_->onFixedTick(fixedDt);
}

void ScriptHost::OnTick(float dt) {
    if (!impl_->active || !impl_->onTick) return;
    impl_->onTick(dt);
}

void ScriptHost::OnPostTick(float dt) {
    if (!impl_->active || !impl_->onPostTick) return;
    impl_->onPostTick(dt);
}

#endif  // ARK_SCRIPTING_ENABLED

} // namespace ark
