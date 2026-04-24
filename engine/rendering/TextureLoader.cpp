// TextureLoader.cpp — stb_image-based texture loading
#include "TextureLoader.h"
#include "engine/debug/DebugListenBus.h"

#include <stb_image.h>

namespace ark {

std::shared_ptr<RHITexture> TextureLoader::Load(RHIDevice* device,
                                                const std::string& filepath,
                                                bool isSRGB) {
    if (!device) {
        ARK_LOG_ERROR("Rendering", "TextureLoader::Load: null device");
        return nullptr;
    }

    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-left origin

    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(filepath.c_str(), &width, &height, &channels, 0);
    if (!data) {
        ARK_LOG_ERROR("Rendering", "TextureLoader: failed to load '" + filepath + "': " + stbi_failure_reason());
        return nullptr;
    }

    auto texture = std::shared_ptr<RHITexture>(device->CreateTexture().release());
    texture->Upload(width, height, channels, data,
                    isSRGB ? TextureFormat::sRGB_Auto : TextureFormat::Linear);

    stbi_image_free(data);

    ARK_LOG_INFO("Rendering", "Loaded texture '" + filepath + "' (" +
        std::to_string(width) + "x" + std::to_string(height) + ", " +
        std::to_string(channels) + "ch, " + (isSRGB ? "sRGB" : "linear") + ")");

    return texture;
}

} // namespace ark
