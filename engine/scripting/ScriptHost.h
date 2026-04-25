// ScriptHost.h — C# (CoreCLR) hosting facade for StarArk MOD scripting.
//
// Phase 15.F roadmap step. This header defines the *engine-side* API used to:
//   - boot a managed runtime (CoreCLR) once at engine startup
//   - load MOD assemblies discovered through the existing Paths VFS
//   - dispatch lifecycle callbacks (OnEngineStart / OnFrame / OnEngineStop)
//   - expose engine entry points that managed code can P/Invoke into
//
// The actual CoreCLR wiring (nethost + hostfxr) lives in ScriptHost.cpp behind
// the ARK_SCRIPTING_ENABLED compile guard. When the guard is OFF the class
// degrades to a no-op so engine + samples + game still build/run without a
// .NET SDK installed.
//
// Intentionally conservative scope for 15.F.0 (this commit):
//   - public API only
//   - stub backend that logs "scripting disabled" and returns success
//   - lifecycle hooks called from EngineBase
//
// Next slice (15.F.1) will:
//   - dlopen nethost + resolve get_hostfxr_path
//   - load hostfxr, init runtime config from <exe>/scripting/StarArk.runtimeconfig.json
//   - resolve a managed `OnEngineStart` via load_assembly_and_get_function_pointer
//   - implement the callback dispatch
#pragma once

#include <memory>
#include <string>

namespace ark {

// Implementation detail; defined entirely inside ScriptHost.cpp. Forward-
// declared at namespace scope so internal helpers in the .cpp's anonymous
// namespace can name `ScriptHostImpl&` without friend-ing ScriptHost.
struct ScriptHostImpl;

// ---------------------------------------------------------------------------
// ScriptApi — function pointer table passed to managed code during
// OnEngineStart. Lets managed scripts reach back into native engine services
// without requiring [DllImport] (which would tie scripts to a specific exe
// name, awkward for libraries linked into game/ AND samples/).
//
// **Stability**: increment `version` whenever the layout changes; managed
// side checks the version and refuses to bind to incompatible builds.
// Append-only. Never reorder or remove fields once shipped.
// ---------------------------------------------------------------------------
extern "C" {

using ArkLogFn      = void (*)(int level, const char* category, const char* message);
using ArkTimeFnF    = float (*)();
using ArkKeyFn      = int  (*)(int keyCode);

// --- v2: object & transform API -------------------------------------------
// `ArkObjectHandle` is the AObject::GetId() value. 0 means "invalid".
using ArkObjectHandle = unsigned long long;

using ArkFindObjectFn   = ArkObjectHandle (*)(const char* name);
using ArkSpawnObjectFn  = ArkObjectHandle (*)(const char* name);
using ArkDestroyObjFn   = int (*)(ArkObjectHandle);
using ArkGetNameFn      = int (*)(ArkObjectHandle, char* buf, int bufLen); // bytes written, including NUL
using ArkSetNameFn      = int (*)(ArkObjectHandle, const char* name);
using ArkGetPosFn       = int (*)(ArkObjectHandle, float* x, float* y, float* z);
using ArkSetPosFn       = int (*)(ArkObjectHandle, float x, float y, float z);
using ArkGetSceneNameFn = int (*)(char* buf, int bufLen);

struct ScriptApi {
    int          version;        // ARK_SCRIPT_API_VERSION below
    // ---- v1 ----
    ArkLogFn     log;            // (LogLevel, utf8 category, utf8 message)
    ArkTimeFnF   getDeltaTime;   // seconds since last frame
    ArkTimeFnF   getTotalTime;   // seconds since engine start
    ArkKeyFn     getKey;         // GLFW_KEY_* — true while held
    ArkKeyFn     getKeyDown;     // true on the frame the key went down
    // ---- v2 (append-only) ----
    ArkFindObjectFn   findObjectByName;  // 0 if not found
    ArkSpawnObjectFn  spawnObject;       // creates a basic AObject in the active scene
    ArkDestroyObjFn   destroyObject;     // 1 if marked, 0 if handle invalid
    ArkGetNameFn      getObjectName;
    ArkSetNameFn      setObjectName;
    ArkGetPosFn       getPosition;       // local position
    ArkSetPosFn       setPosition;       // local position
    ArkGetSceneNameFn getActiveSceneName;
};

constexpr int ARK_SCRIPT_API_VERSION = 2;

} // extern "C"

class ScriptHost {
public:
    /// Singleton accessor. ScriptHost is owned by EngineBase but accessed
    /// globally so binding-layer trampolines can reach it.
    static ScriptHost& Get();

    /// Boot the managed runtime. Idempotent. Returns false on hard failure;
    /// engine treats failure as non-fatal (keeps running without scripts).
    /// `gameRoot` is the directory containing the executable (Paths::GameRoot()).
    bool Initialize(const std::string& gameRoot);

    /// Tear down the managed runtime. Safe to call when not initialized.
    void Shutdown();

    /// Per-frame phased ticks. All are no-ops when scripting is disabled.
    /// `OnFixedTick` may be called zero or multiple times per frame
    /// depending on the fixed-timestep accumulator.
    void OnFixedTick(float fixedDt);
    void OnTick(float dt);       // before native StepTick (== Unity Update)
    void OnPostTick(float dt);   // after  native StepPostTick (== Unity LateUpdate)

    /// Whether the managed runtime is loaded AND a MOD assembly was found.
    /// Useful for samples to decide whether to log "scripts ran".
    bool IsActive() const;

private:
    ScriptHost();
    ~ScriptHost();

    ScriptHost(const ScriptHost&) = delete;
    ScriptHost& operator=(const ScriptHost&) = delete;

    std::unique_ptr<ScriptHostImpl> impl_;
};

} // namespace ark
