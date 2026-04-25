// IMod.cs — MOD lifecycle contract.
//
// Inspired by Unity's MonoBehaviour callbacks but renamed to align with
// the engine's native AComponent lifecycle (PreInit / Init / Loop /
// PostLoop). All methods have default empty implementations so a MOD only
// overrides what it cares about.
//
// Per-frame ordering (matches engine main loop in EngineBase::MainLoop):
//
//   ┌───────────────────── once at load time ─────────────────────┐
//   │  PreInit()    — Unity's Awake.   Constructor done, before    │
//   │                 any other MOD has been touched.              │
//   │  OnLoad()     — Unity's OnEnable. MOD is "live"; called      │
//   │                 again after a future hot-reload re-enable.   │
//   │  Init()       — Unity's Start.   First time before Loop.     │
//   └──────────────────────────────────────────────────────────────┘
//
//   per frame:
//     while (fixedAccum ≥ 1/60s)  ─►  FixedLoop(1/60)   // physics-style
//     Loop(dt)                    ─►  Unity Update      // game logic
//     ── native engine StepTick / StepPostTick run between these ──
//     PostLoop(dt)                ─►  Unity LateUpdate  // post-fixup
//
//   ┌───────────────────── once at shutdown ───────────────────────┐
//   │  OnUnload()   — Unity's OnDestroy. Free resources here.      │
//   └──────────────────────────────────────────────────────────────┘

namespace StarArk.Scripting;

public interface IMod
{
    /// <summary>Friendly name shown in logs. Falls back to type full name when null.</summary>
    string Name => GetType().Name;

    /// <summary>Awake-equivalent: instance just constructed. Avoid heavy work here.</summary>
    void PreInit() {}

    /// <summary>OnEnable-equivalent: MOD becomes active. May fire again after hot-reload re-enable.</summary>
    void OnLoad() {}

    /// <summary>Start-equivalent: called once just before the first Loop.</summary>
    void Init() {}

    /// <summary>FixedUpdate-equivalent: called 0..N times per frame at fixed dt = 1/60s.</summary>
    void FixedLoop(float fixedDeltaTime) {}

    /// <summary>Update-equivalent: per-frame game logic, before native object Tick.</summary>
    void Loop(float deltaTime) {}

    /// <summary>LateUpdate-equivalent: per-frame post-fixup, after native PostTick.</summary>
    void PostLoop(float deltaTime) {}

    /// <summary>OnDestroy-equivalent: engine shutdown / MOD unload. Release resources.</summary>
    void OnUnload() {}
}
