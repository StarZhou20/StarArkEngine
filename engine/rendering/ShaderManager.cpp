#include "ShaderManager.h"
#include "ShaderSources.h"
#include "engine/platform/Paths.h"
#include "engine/debug/DebugListenBus.h"

#include <fstream>
#include <sstream>
#include <system_error>

namespace ark {

// ---------- default hot-reload policy ----------
#ifndef ARK_SHADER_HOT_RELOAD
#  if defined(NDEBUG)
#    define ARK_SHADER_HOT_RELOAD 0
#  else
#    define ARK_SHADER_HOT_RELOAD 1
#  endif
#endif

ShaderManager::ShaderManager(RHIDevice* device)
    : device_(device)
{
    hotReloadEnabled_ = (ARK_SHADER_HOT_RELOAD != 0);
}

std::string ShaderManager::ReadFile(const std::filesystem::path& path) {
    std::ifstream f(path, std::ios::in | std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    return ss.str();
}

bool ShaderManager::LookupEmbeddedSource(const std::string& name,
                                         std::string& outVS,
                                         std::string& outFS) {
    if (name == "pbr")   { outVS = kPBR_VS;   outFS = kPBR_FS;   return true; }
    if (name == "phong") { outVS = kPhongVS;  outFS = kPhongFS;  return true; }
    if (name == "depth") { outVS = kDepth_VS; outFS = kDepth_FS; return true; }
    return false;
}

bool ShaderManager::LoadEntry(const std::string& name, Entry& out) {
    std::filesystem::path vertPath = Paths::ResolveContent("shaders/" + name + ".vert");
    std::filesystem::path fragPath = Paths::ResolveContent("shaders/" + name + ".frag");

    std::string vs, fs;
    bool fromDisk = false;

    std::error_code ec;
    bool haveFiles = std::filesystem::exists(vertPath, ec) &&
                     std::filesystem::exists(fragPath, ec);
    if (haveFiles) {
        vs = ReadFile(vertPath);
        fs = ReadFile(fragPath);
        if (!vs.empty() && !fs.empty()) {
            fromDisk = true;
            out.vertMTime = std::filesystem::last_write_time(vertPath, ec);
            out.fragMTime = std::filesystem::last_write_time(fragPath, ec);
        }
    }

    if (!fromDisk) {
        if (!LookupEmbeddedSource(name, vs, fs)) {
            ARK_LOG_ERROR("Rendering",
                "ShaderManager: shader '" + name + "' not found on disk and has no embedded fallback");
            return false;
        }
        ARK_LOG_INFO("Rendering",
            "ShaderManager: using embedded source for '" + name + "' (no files at " +
            vertPath.string() + ")");
    }

    auto shader = std::shared_ptr<RHIShader>(device_->CreateShader().release());
    if (!shader || !shader->Compile(vs, fs)) {
        ARK_LOG_ERROR("Rendering", "ShaderManager: failed to compile shader '" + name + "'");
        return false;
    }

    out.shader   = std::move(shader);
    out.vertPath = vertPath;
    out.fragPath = fragPath;
    out.fromDisk = fromDisk;

    if (fromDisk) {
        ARK_LOG_INFO("Rendering", "ShaderManager: loaded '" + name + "' from " + vertPath.string());
    }
    return true;
}

std::shared_ptr<RHIShader> ShaderManager::Get(const std::string& name) {
    auto it = cache_.find(name);
    if (it != cache_.end()) return it->second.shader;

    Entry e;
    if (!LoadEntry(name, e)) return nullptr;
    auto shader = e.shader;
    cache_.emplace(name, std::move(e));
    return shader;
}

void ShaderManager::CheckHotReload() {
    if (!hotReloadEnabled_) return;

    for (auto& [name, entry] : cache_) {
        if (!entry.fromDisk) continue;

        std::error_code ec;
        auto vTime = std::filesystem::last_write_time(entry.vertPath, ec);
        if (ec) continue;
        auto fTime = std::filesystem::last_write_time(entry.fragPath, ec);
        if (ec) continue;

        if (vTime == entry.vertMTime && fTime == entry.fragMTime) continue;

        // At least one file changed — try to reload in-place. GLShader::Compile
        // keeps the previous program on failure, so Materials that hold this
        // shared_ptr continue to render with the old (working) code if the
        // new code fails to compile.
        std::string vs = ReadFile(entry.vertPath);
        std::string fs = ReadFile(entry.fragPath);
        if (vs.empty() || fs.empty()) continue; // file mid-write, retry next frame

        if (entry.shader->Compile(vs, fs)) {
            entry.vertMTime = vTime;
            entry.fragMTime = fTime;
            ARK_LOG_INFO("Rendering", "ShaderManager: hot-reloaded '" + name + "'");
        } else {
            // Update mtime anyway so we don't spam the same broken file every frame.
            entry.vertMTime = vTime;
            entry.fragMTime = fTime;
            ARK_LOG_ERROR("Rendering",
                "ShaderManager: hot-reload failed for '" + name + "', keeping previous program");
        }
    }
}

} // namespace ark
