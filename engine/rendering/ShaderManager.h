// ShaderManager.h — Loads GLSL shaders from disk, caches them, and
// optionally hot-reloads them when the source files change on disk.
//
// Convention: given a name "pbr", ShaderManager looks for:
//   {content}/shaders/pbr.vert
//   {content}/shaders/pbr.frag
// relative to Paths::ResolveContent(). If either file is missing, it
// falls back to the embedded GLSL strings in ShaderSources.h (and hot
// reload is disabled for that entry).
//
// Intended usage pattern:
//   auto shader = engine.GetRenderer()->GetShaderManager()->Get("pbr");
//   material->SetShader(shader);
//
// The returned shared_ptr's *program* is reloaded in-place when the
// file changes; all Materials keep the same shared_ptr and
// automatically see the new program on the next draw.
#pragma once

#include "engine/rhi/RHIDevice.h"
#include "engine/rhi/RHIShader.h"

#include <filesystem>
#include <memory>
#include <string>
#include <unordered_map>

namespace ark {

class ShaderManager {
public:
    explicit ShaderManager(RHIDevice* device);
    ~ShaderManager() = default;

    ShaderManager(const ShaderManager&) = delete;
    ShaderManager& operator=(const ShaderManager&) = delete;

    /// Get (or lazily load) the shader named `name`. Returns nullptr on
    /// first-time load failure; subsequent calls keep returning the cached
    /// entry (which may hold an older successful program if a reload failed).
    std::shared_ptr<RHIShader> Get(const std::string& name);

    /// Poll all cached shaders for file mtime changes and recompile any that
    /// changed on disk. Safe to call every frame. If a hot reload fails to
    /// compile, the previous program is retained and an error is logged.
    void CheckHotReload();

    /// Enable or disable the hot-reload polling. Default is controlled by the
    /// ARK_SHADER_HOT_RELOAD CMake option (ON in Debug, OFF in Release).
    void SetHotReloadEnabled(bool enabled) { hotReloadEnabled_ = enabled; }
    bool IsHotReloadEnabled() const { return hotReloadEnabled_; }

private:
    struct Entry {
        std::shared_ptr<RHIShader> shader;
        std::filesystem::path vertPath;
        std::filesystem::path fragPath;
        std::filesystem::file_time_type vertMTime{};
        std::filesystem::file_time_type fragMTime{};
        bool fromDisk = false; // false = using embedded fallback
    };

    bool LoadEntry(const std::string& name, Entry& outEntry);
    static std::string ReadFile(const std::filesystem::path& path);
    static bool LookupEmbeddedSource(const std::string& name,
                                     std::string& outVS,
                                     std::string& outFS);

    RHIDevice* device_;
    std::unordered_map<std::string, Entry> cache_;
    bool hotReloadEnabled_ = true;
};

} // namespace ark
