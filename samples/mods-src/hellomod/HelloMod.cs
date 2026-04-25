// HelloMod — demonstrates the Phase 15.F.3 engine API:
//   • Find / Spawn / Destroy AObjects
//   • Read / write Transform position
//   • Query active scene name
//
// Controls (in a focused engine window):
//   F1 — spawn an empty AObject named "HelloMod.Marker" if none exists
//   F2 — destroy the marker
//   F3 — print all known coords (marker + active camera if found by name)
//
// Behaviour: while the marker exists, it orbits in a horizontal circle of
// radius 3 around the world origin once every 4 seconds.

using System;
using StarArk.Scripting;

namespace HelloMod;

public sealed class HelloMod : IMod
{
    private const int GLFW_KEY_F1 = 290;
    private const int GLFW_KEY_F2 = 291;
    private const int GLFW_KEY_F3 = 292;
    private const string MarkerName = "HelloMod.Marker";

    private ObjectHandle _marker;
    private float        _phase;

    public string Name => "HelloMod";

    public void OnLoad()
    {
        Bridge.LogInfo("HelloMod",
            $"OnLoad. Active scene = '{Bridge.ActiveSceneName}'. " +
            $"F1=spawn  F2=destroy  F3=print");
    }

    public void Loop(float dt)
    {
        // Refresh handle if a previous spawn was destroyed externally.
        if (_marker.IsValid && string.IsNullOrEmpty(Bridge.GetName(_marker)))
            _marker = ObjectHandle.None;

        if (Bridge.GetKeyDown(GLFW_KEY_F1)) HandleSpawn();
        if (Bridge.GetKeyDown(GLFW_KEY_F2)) HandleDestroy();
        if (Bridge.GetKeyDown(GLFW_KEY_F3)) HandlePrint();

        // Orbit the marker if it exists.
        if (_marker.IsValid)
        {
            _phase += dt;
            const float radius = 3.0f;
            const float period = 4.0f;
            float a = _phase * (MathF.PI * 2f / period);
            Bridge.SetPosition(_marker,
                MathF.Cos(a) * radius,
                1.0f,
                MathF.Sin(a) * radius);
        }
    }

    private void HandleSpawn()
    {
        if (_marker.IsValid)
        {
            Bridge.LogInfo("HelloMod", $"Marker already exists: {_marker}");
            return;
        }
        // Try to reuse an existing one by name first (e.g. created by another MOD).
        var existing = Bridge.Find(MarkerName);
        if (existing.IsValid)
        {
            _marker = existing;
            Bridge.LogInfo("HelloMod", $"Adopted existing marker: {existing}");
            return;
        }
        _marker = Bridge.Spawn(MarkerName);
        if (_marker.IsValid)
            Bridge.LogInfo("HelloMod", $"Spawned marker {_marker} in scene '{Bridge.ActiveSceneName}'");
        else
            Bridge.LogWarn("HelloMod", "Spawn failed (no active scene?)");
    }

    private void HandleDestroy()
    {
        if (!_marker.IsValid)
        {
            Bridge.LogInfo("HelloMod", "No marker to destroy");
            return;
        }
        bool ok = Bridge.Destroy(_marker);
        Bridge.LogInfo("HelloMod", $"Destroy({_marker}) = {ok}");
        _marker = ObjectHandle.None;
    }

    private void HandlePrint()
    {
        if (_marker.IsValid)
        {
            var pos = Bridge.GetPosition(_marker);
            Bridge.LogInfo("HelloMod",
                $"Marker {_marker} '{Bridge.GetName(_marker)}' pos={pos}");
        }
        else
        {
            Bridge.LogInfo("HelloMod", "No marker active.");
        }

        // Probe a likely camera name (DemoScene uses "MainCamera" via CameraObject).
        var cam = Bridge.Find("MainCamera");
        if (cam.IsValid)
            Bridge.LogInfo("HelloMod", $"Found camera {cam} pos={Bridge.GetPosition(cam)}");
    }

    public void OnUnload()
    {
        if (_marker.IsValid) Bridge.Destroy(_marker);
        Bridge.LogInfo("HelloMod", "OnUnload.");
    }
}
