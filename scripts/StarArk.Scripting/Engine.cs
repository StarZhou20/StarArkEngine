// Engine.cs — Managed entry points called by the native ScriptHost.
//
// Five [UnmanagedCallersOnly] entry points are exposed to native code via
// load_assembly_and_get_function_pointer:
//
//   OnEngineStart(api*, modsDirUtf8)  — boot bridge, scan & load MODs,
//                                       fire PreInit + OnLoad on each.
//   OnFixedTick(fixedDt)              — fan out to IMod.FixedLoop.
//   OnTick(dt)                        — fan out to IMod.Loop. The first
//                                       time a mod is ticked, fires Init
//                                       beforehand (Unity's Start semantics).
//   OnPostTick(dt)                    — fan out to IMod.PostLoop.
//   OnEngineStop()                    — fire OnUnload on each MOD.

using System;
using System.Collections.Generic;
using System.IO;
using System.Reflection;
using System.Runtime.InteropServices;
using System.Runtime.Loader;

namespace StarArk.Scripting;

public static unsafe class Engine
{
    private static readonly List<LoadedMod> _mods = new();

    private sealed class LoadedMod
    {
        public required string              Name      { get; init; }
        public required IMod                Instance  { get; init; }
        public required AssemblyLoadContext Context   { get; init; }
        public bool                         Started   { get; set; }   // Init called?
    }

    // ─────────────────────────────────────────────────────────────────
    //  Lifecycle entry points
    // ─────────────────────────────────────────────────────────────────

    [UnmanagedCallersOnly]
    public static int OnEngineStart(ArkScriptApi* api, byte* modsDirUtf8)
    {
        try
        {
            if (!Bridge.Initialize(api))
            {
                Console.WriteLine("[StarArk.Scripting] Bridge.Initialize failed; aborting.");
                return 1;
            }
            Bridge.LogInfo("Scripting",
                "Managed runtime alive (.NET " + Environment.Version + ").");

            string modsDir = Marshal.PtrToStringUTF8((IntPtr)modsDirUtf8) ?? string.Empty;
            DiscoverAndLoadMods(modsDir);
            return 0;
        }
        catch (Exception ex)
        {
            try { Bridge.LogError("Scripting", "OnEngineStart threw: " + ex); }
            catch { Console.WriteLine("[StarArk.Scripting] OnEngineStart threw: " + ex); }
            return 1;
        }
    }

    [UnmanagedCallersOnly]
    public static int OnFixedTick(float fixedDt)
    {
        for (int i = 0; i < _mods.Count; i++)
        {
            var m = _mods[i];
            try { m.Instance.FixedLoop(fixedDt); }
            catch (Exception ex)
            {
                Bridge.LogError("Mod", $"{m.Name}.FixedLoop threw: {ex.Message}");
            }
        }
        return 0;
    }

    [UnmanagedCallersOnly]
    public static int OnTick(float dt)
    {
        // Unity-style Start semantics: fire Init lazily right before the
        // first Loop on a given MOD.
        for (int i = 0; i < _mods.Count; i++)
        {
            var m = _mods[i];
            if (!m.Started)
            {
                try { m.Instance.Init(); }
                catch (Exception ex)
                {
                    Bridge.LogError("Mod", $"{m.Name}.Init threw: {ex.Message}");
                }
                m.Started = true;
            }
            try { m.Instance.Loop(dt); }
            catch (Exception ex)
            {
                Bridge.LogError("Mod", $"{m.Name}.Loop threw: {ex.Message}");
            }
        }
        return 0;
    }

    [UnmanagedCallersOnly]
    public static int OnPostTick(float dt)
    {
        for (int i = 0; i < _mods.Count; i++)
        {
            var m = _mods[i];
            if (!m.Started) continue;   // PostLoop only after Init has fired
            try { m.Instance.PostLoop(dt); }
            catch (Exception ex)
            {
                Bridge.LogError("Mod", $"{m.Name}.PostLoop threw: {ex.Message}");
            }
        }
        return 0;
    }

    [UnmanagedCallersOnly]
    public static int OnEngineStop()
    {
        Bridge.LogInfo("Scripting",
            $"OnEngineStop — unloading {_mods.Count} mod instance(s).");
        // Reverse order so dependents tear down before dependencies.
        for (int i = _mods.Count - 1; i >= 0; i--)
        {
            var m = _mods[i];
            try { m.Instance.OnUnload(); }
            catch (Exception ex)
            {
                Bridge.LogError("Mod", $"{m.Name}.OnUnload threw: {ex.Message}");
            }
        }
        _mods.Clear();
        return 0;
    }

    // ─────────────────────────────────────────────────────────────────
    //  MOD discovery
    // ─────────────────────────────────────────────────────────────────
    private static void DiscoverAndLoadMods(string modsDir)
    {
        if (string.IsNullOrEmpty(modsDir) || !Directory.Exists(modsDir))
        {
            Bridge.LogInfo("Mod",
                $"No mods directory found at '{modsDir}'. Engine running without MODs.");
            return;
        }

        // Walk one level: <modsDir>/<modname>/scripts/*.dll
        foreach (var modFolder in Directory.GetDirectories(modsDir))
        {
            string scriptsDir = Path.Combine(modFolder, "scripts");
            if (!Directory.Exists(scriptsDir)) continue;
            string modName = Path.GetFileName(modFolder);

            foreach (var dllPath in Directory.GetFiles(scriptsDir, "*.dll"))
            {
                TryLoadModAssembly(modName, dllPath);
            }
        }

        Bridge.LogInfo("Mod",
            $"MOD discovery complete: {_mods.Count} mod instance(s) loaded from '{modsDir}'.");
    }

    private static void TryLoadModAssembly(string modName, string dllPath)
    {
        try
        {
            var alc = new ModLoadContext(dllPath);
            var asm = alc.LoadFromAssemblyPath(dllPath);

            int found = 0;
            foreach (var type in asm.GetTypes())
            {
                if (!typeof(IMod).IsAssignableFrom(type)) continue;
                if (type.IsAbstract || type.IsInterface)  continue;
                if (type.GetConstructor(Type.EmptyTypes) == null)
                {
                    Bridge.LogWarn("Mod",
                        $"Skipping {type.FullName} in {modName}: no parameterless ctor.");
                    continue;
                }

                IMod? instance = Activator.CreateInstance(type) as IMod;
                if (instance == null) continue;

                // PreInit (Awake) — instance just constructed.
                try { instance.PreInit(); }
                catch (Exception ex)
                {
                    Bridge.LogError("Mod",
                        $"{type.FullName}.PreInit threw: {ex.Message}. Skipping mod.");
                    continue;
                }

                // OnLoad (OnEnable) — MOD is now live.
                try { instance.OnLoad(); }
                catch (Exception ex)
                {
                    Bridge.LogError("Mod",
                        $"{type.FullName}.OnLoad threw: {ex.Message}. Skipping mod.");
                    continue;
                }

                _mods.Add(new LoadedMod
                {
                    Name     = instance.Name ?? type.FullName ?? "<anon>",
                    Instance = instance,
                    Context  = alc,
                });
                found++;
                Bridge.LogInfo("Mod",
                    $"Loaded MOD '{modName}' :: {type.FullName} (asm={Path.GetFileName(dllPath)}).");
            }

            if (found == 0)
            {
                Bridge.LogWarn("Mod",
                    $"No IMod implementations found in {dllPath} (mod '{modName}').");
            }
        }
        catch (Exception ex)
        {
            Bridge.LogError("Mod",
                $"Failed to load MOD assembly {dllPath} ({modName}): {ex.Message}");
        }
    }

    /// <summary>
    /// Per-MOD load context. Resolves StarArk.Scripting back to the host's
    /// already-loaded copy so IMod / Bridge stay unified across MODs.
    /// </summary>
    private sealed class ModLoadContext : AssemblyLoadContext
    {
        private readonly AssemblyDependencyResolver _resolver;
        public ModLoadContext(string mainAssemblyPath)
            : base(name: $"Mod:{Path.GetFileNameWithoutExtension(mainAssemblyPath)}", isCollectible: true)
        {
            _resolver = new AssemblyDependencyResolver(mainAssemblyPath);
        }

        protected override Assembly? Load(AssemblyName name)
        {
            // Default ALC's probing path doesn't cover <exe>/scripting/, so
            // hand back the host's StarArk.Scripting explicitly.
            if (name.Name == "StarArk.Scripting")
                return typeof(IMod).Assembly;

            string? path = _resolver.ResolveAssemblyToPath(name);
            return path != null ? LoadFromAssemblyPath(path) : null;
        }
    }
}
