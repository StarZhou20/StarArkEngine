// SceneSerializer.h — JSON persistence for render settings + lights (Phase M10 mini).
//
// Schema (round-tripped exactly):
// {
//   "renderSettings": { "exposure": 1.0, "bloom": {...}, "sky": {...},
//                       "ibl": {...}, "shadow": {...} },
//   "lights": [
//     { "name": "Sun", "type": "Directional",
//       "color": [r, g, b], "intensity": 1.0, "ambient": [r, g, b],
//       "position": [x, y, z], "rotationEuler": [pitch, yaw, roll],
//       "range": 10.0, "constant": 1.0, "linear": 0.09, "quadratic": 0.032,
//       "innerAngle": 12.5, "outerAngle": 17.5 }, ...
//   ]
// }
//
// Lights are matched back to runtime Light components by their owner
// AObject name. Unknown entries in the file are ignored; lights present
// in the runtime but not in the file keep their current values.
//
// Intended usage:
//   SceneSerializer::Save("lighting.json", renderer);
//   SceneSerializer::Load("lighting.json", renderer);
//   SceneSerializer::EnableHotReload("lighting.json"); // then call Tick() each frame
#pragma once

#include <filesystem>
#include <string>

namespace ark {

class ForwardRenderer;

class SceneSerializer {
public:
    /// Write renderer->GetRenderSettings() + all active Light components to a JSON file.
    /// Returns true on success.
    static bool Save(const std::filesystem::path& path, ForwardRenderer* renderer);

    /// Read JSON from `path` and apply to renderer->GetRenderSettings() and
    /// to Light components matched by AObject name. Returns true on success.
    static bool Load(const std::filesystem::path& path, ForwardRenderer* renderer);

    /// Enable mtime-polling hot reload on `path`. The next call to Tick()
    /// that observes a newer mtime re-runs Load(). Pass an empty path to disable.
    static void EnableHotReload(const std::filesystem::path& path);

    /// Poll the hot-reload target. Safe to call every frame. No-op if disabled.
    static void Tick(ForwardRenderer* renderer);
};

} // namespace ark
