// Bridge.cs — Managed-side surface of the native↔managed interop layer.
//
// Native ScriptHost.cpp builds a `ScriptApi` struct of function pointers and
// hands it to OnEngineStart. This file declares the matching managed layout
// and exposes friendly static helpers (Bridge.Log, Bridge.Find, ...) that
// MOD code can call without touching unsafe / [DllImport] machinery.
//
// LAYOUT WARNING: ArkScriptApi MUST mirror native struct ScriptApi 1:1
// (engine/scripting/ScriptHost.h). Append-only — never reorder or remove
// fields once shipped. Bump the version constant in lockstep with the
// native ARK_SCRIPT_API_VERSION whenever a new field is appended.

using System;
using System.Runtime.InteropServices;

namespace StarArk.Scripting;

/// <summary>Severity levels — values must match native enum class LogLevel.</summary>
public enum LogLevel : int
{
    Trace   = 0,
    Debug   = 1,
    Info    = 2,
    Warning = 3,
    Error   = 4,
    Fatal   = 5,
}

/// <summary>
/// Strongly-typed handle to an engine AObject. Wraps a uint64 id so MOD code
/// doesn't accidentally do arithmetic on it. <see cref="None"/> means "not found".
/// </summary>
public readonly struct ObjectHandle : IEquatable<ObjectHandle>
{
    public readonly ulong Id;
    public ObjectHandle(ulong id) { Id = id; }
    public static ObjectHandle None => default;
    public bool IsValid => Id != 0;
    public bool Equals(ObjectHandle o) => Id == o.Id;
    public override bool Equals(object? o) => o is ObjectHandle h && Equals(h);
    public override int GetHashCode() => Id.GetHashCode();
    public override string ToString() => $"AObject#{Id}";
}

/// <summary>3-component vector — matches glm::vec3 layout.</summary>
[StructLayout(LayoutKind.Sequential)]
public struct Vec3
{
    public float X, Y, Z;
    public Vec3(float x, float y, float z) { X = x; Y = y; Z = z; }
    public override string ToString() => $"({X:F2},{Y:F2},{Z:F2})";
}

/// <summary>
/// Mirror of native <c>struct ScriptApi</c>. Sequential layout, default packing.
/// All function pointers are <c>cdecl</c> on Windows x64 (the default).
/// </summary>
[StructLayout(LayoutKind.Sequential)]
public unsafe struct ArkScriptApi
{
    public int    Version;
    // ---- v1 ----
    public delegate* unmanaged<int, byte*, byte*, void> Log;
    public delegate* unmanaged<float>                   GetDeltaTime;
    public delegate* unmanaged<float>                   GetTotalTime;
    public delegate* unmanaged<int, int>                GetKey;
    public delegate* unmanaged<int, int>                GetKeyDown;
    // ---- v2 ----
    public delegate* unmanaged<byte*, ulong>                              FindObjectByName;
    public delegate* unmanaged<byte*, ulong>                              SpawnObject;
    public delegate* unmanaged<ulong, int>                                DestroyObject;
    public delegate* unmanaged<ulong, byte*, int, int>                    GetObjectName;
    public delegate* unmanaged<ulong, byte*, int>                         SetObjectName;
    public delegate* unmanaged<ulong, float*, float*, float*, int>        GetPosition;
    public delegate* unmanaged<ulong, float, float, float, int>           SetPosition;
    public delegate* unmanaged<byte*, int, int>                           GetActiveSceneName;
}

/// <summary>
/// Static facade over the native ScriptApi table. <see cref="Initialize"/>
/// is called by <c>StarArk.Scripting.Engine.OnEngineStart</c>; MOD code only
/// ever sees the high-level helpers.
/// </summary>
public static unsafe class Bridge
{
    public const int RequiredApiVersion = 2;

    private static ArkScriptApi _api;
    private static bool         _ready;

    /// <summary>Wired up by Engine.OnEngineStart. Returns false on version mismatch.</summary>
    internal static bool Initialize(ArkScriptApi* apiPtr)
    {
        if (apiPtr == null) return false;
        if (apiPtr->Version < RequiredApiVersion)
        {
            Console.WriteLine(
                $"[StarArk.Scripting] FATAL: ScriptApi version too low " +
                $"(native={apiPtr->Version}, managed needs ≥ {RequiredApiVersion}). " +
                $"Refusing to bind.");
            return false;
        }
        _api   = *apiPtr;
        _ready = true;
        return true;
    }

    /// <summary>True once the native bridge is wired up.</summary>
    public static bool IsReady => _ready;

    // ─────────────────────────────────────────────────────────────────
    // Logging
    // ─────────────────────────────────────────────────────────────────
    public static void Log(LogLevel level, string category, string message)
    {
        if (!_ready) { Console.WriteLine($"[{level}][{category}] {message}"); return; }
        WithUtf8(category ?? "Mod", catPtr =>
        WithUtf8(message  ?? string.Empty, msgPtr =>
        {
            _api.Log((int)level, catPtr, msgPtr);
        }));
    }

    public static void LogInfo(string c, string m)  => Log(LogLevel.Info,    c, m);
    public static void LogWarn(string c, string m)  => Log(LogLevel.Warning, c, m);
    public static void LogError(string c, string m) => Log(LogLevel.Error,   c, m);

    // ─────────────────────────────────────────────────────────────────
    // Time / Input
    // ─────────────────────────────────────────────────────────────────
    public static float DeltaTime  => _ready ? _api.GetDeltaTime() : 0f;
    public static float TotalTime  => _ready ? _api.GetTotalTime() : 0f;

    /// <summary>True while the GLFW key is held. See GLFW key constants.</summary>
    public static bool GetKey(int glfwKey)     => _ready && _api.GetKey(glfwKey)     != 0;
    /// <summary>True only on the frame the key transitions to pressed.</summary>
    public static bool GetKeyDown(int glfwKey) => _ready && _api.GetKeyDown(glfwKey) != 0;

    // ─────────────────────────────────────────────────────────────────
    // Object / Transform / Scene
    // ─────────────────────────────────────────────────────────────────
    public static ObjectHandle Find(string name)
    {
        if (!_ready || string.IsNullOrEmpty(name)) return ObjectHandle.None;
        ulong id = 0;
        WithUtf8(name, p => { id = _api.FindObjectByName(p); });
        return new ObjectHandle(id);
    }

    public static ObjectHandle Spawn(string name)
    {
        if (!_ready) return ObjectHandle.None;
        ulong id = 0;
        WithUtf8(name ?? string.Empty, p => { id = _api.SpawnObject(p); });
        return new ObjectHandle(id);
    }

    public static bool Destroy(ObjectHandle h)
    {
        if (!_ready || !h.IsValid) return false;
        return _api.DestroyObject(h.Id) != 0;
    }

    public static string GetName(ObjectHandle h)
    {
        if (!_ready || !h.IsValid) return string.Empty;
        // Two-call protocol: first call with NULL probes for required size,
        // second call fills the buffer. Required size includes NUL.
        int needed = _api.GetObjectName(h.Id, null, 0);
        if (needed <= 1) return string.Empty;
        Span<byte> buf = needed <= 256 ? stackalloc byte[needed] : new byte[needed];
        fixed (byte* p = buf) _api.GetObjectName(h.Id, p, needed);
        // needed counts the trailing NUL; trim it.
        return System.Text.Encoding.UTF8.GetString(buf[..(needed - 1)]);
    }

    public static bool SetName(ObjectHandle h, string name)
    {
        if (!_ready || !h.IsValid) return false;
        int rc = 0;
        WithUtf8(name ?? string.Empty, p => { rc = _api.SetObjectName(h.Id, p); });
        return rc != 0;
    }

    public static Vec3 GetPosition(ObjectHandle h)
    {
        if (!_ready || !h.IsValid) return default;
        float x = 0, y = 0, z = 0;
        _api.GetPosition(h.Id, &x, &y, &z);
        return new Vec3(x, y, z);
    }

    public static bool SetPosition(ObjectHandle h, float x, float y, float z)
    {
        if (!_ready || !h.IsValid) return false;
        return _api.SetPosition(h.Id, x, y, z) != 0;
    }

    public static bool SetPosition(ObjectHandle h, Vec3 v) => SetPosition(h, v.X, v.Y, v.Z);

    public static string ActiveSceneName
    {
        get
        {
            if (!_ready) return string.Empty;
            int needed = _api.GetActiveSceneName(null, 0);
            if (needed <= 1) return string.Empty;
            Span<byte> buf = needed <= 256 ? stackalloc byte[needed] : new byte[needed];
            fixed (byte* p = buf) _api.GetActiveSceneName(p, needed);
            return System.Text.Encoding.UTF8.GetString(buf[..(needed - 1)]);
        }
    }

    // ─────────────────────────────────────────────────────────────────
    // UTF-8 helper
    // ─────────────────────────────────────────────────────────────────
    private delegate void Utf8Action(byte* ptr);

    private static void WithUtf8(string s, Utf8Action action)
    {
        int max = System.Text.Encoding.UTF8.GetMaxByteCount(s.Length) + 1;
        if (max <= 1024)
        {
            byte* buf = stackalloc byte[max];
            int n = System.Text.Encoding.UTF8.GetBytes(s, new Span<byte>(buf, max - 1));
            buf[n] = 0;
            action(buf);
        }
        else
        {
            byte[] arr = new byte[max];
            int n = System.Text.Encoding.UTF8.GetBytes(s, 0, s.Length, arr, 0);
            arr[n] = 0;
            fixed (byte* p = arr) action(p);
        }
    }
}
