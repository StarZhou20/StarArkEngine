// TextureLoader.cpp — stb_image-based texture loading
#include "TextureLoader.h"
#include "engine/debug/DebugListenBus.h"
#include "engine/platform/Paths.h"

#include <stb_image.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace ark {

// -----------------------------------------------------------------------------
// DDS loader (minimal — handles BC1/BC3/BC5 which cover the Bistro asset).
//
// File layout:
//   "DDS " (4 bytes) + DDS_HEADER (124 bytes) + [DDS_HEADER_DXT10 (20)] + data
// -----------------------------------------------------------------------------

namespace {

#pragma pack(push, 1)
struct DDSPixelFormat {
    uint32_t dwSize;
    uint32_t dwFlags;
    uint32_t dwFourCC;
    uint32_t dwRGBBitCount;
    uint32_t dwRBitMask;
    uint32_t dwGBitMask;
    uint32_t dwBBitMask;
    uint32_t dwABitMask;
};

struct DDSHeader {
    uint32_t       dwSize;
    uint32_t       dwFlags;
    uint32_t       dwHeight;
    uint32_t       dwWidth;
    uint32_t       dwPitchOrLinearSize;
    uint32_t       dwDepth;
    uint32_t       dwMipMapCount;
    uint32_t       dwReserved1[11];
    DDSPixelFormat ddspf;
    uint32_t       dwCaps;
    uint32_t       dwCaps2;
    uint32_t       dwCaps3;
    uint32_t       dwCaps4;
    uint32_t       dwReserved2;
};
#pragma pack(pop)

constexpr uint32_t MakeFourCC(char a, char b, char c, char d) {
    return static_cast<uint32_t>(a) |
           (static_cast<uint32_t>(b) << 8) |
           (static_cast<uint32_t>(c) << 16) |
           (static_cast<uint32_t>(d) << 24);
}

std::shared_ptr<RHITexture> LoadDDS(RHIDevice* device,
                                    const std::string& displayPath,
                                    const std::string& resolved,
                                    bool isSRGB) {
    std::FILE* f = nullptr;
#if defined(_WIN32)
    fopen_s(&f, resolved.c_str(), "rb");
#else
    f = std::fopen(resolved.c_str(), "rb");
#endif
    if (!f) {
        ARK_LOG_WARN("Rendering", "DDS: cannot open '" + resolved + "'");
        return nullptr;
    }

    std::fseek(f, 0, SEEK_END);
    long fsize = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (fsize < static_cast<long>(4 + sizeof(DDSHeader))) {
        std::fclose(f);
        ARK_LOG_WARN("Rendering", "DDS: too small '" + displayPath + "'");
        return nullptr;
    }

    std::vector<uint8_t> buf(static_cast<size_t>(fsize));
    if (std::fread(buf.data(), 1, buf.size(), f) != buf.size()) {
        std::fclose(f);
        ARK_LOG_WARN("Rendering", "DDS: short read '" + displayPath + "'");
        return nullptr;
    }
    std::fclose(f);

    if (std::memcmp(buf.data(), "DDS ", 4) != 0) {
        ARK_LOG_WARN("Rendering", "DDS: bad magic '" + displayPath + "'");
        return nullptr;
    }

    DDSHeader hdr{};
    std::memcpy(&hdr, buf.data() + 4, sizeof(DDSHeader));

    int width  = static_cast<int>(hdr.dwWidth);
    int height = static_cast<int>(hdr.dwHeight);
    int mipCount = std::max(1, static_cast<int>(hdr.dwMipMapCount));

    size_t dataOffset = 4 + sizeof(DDSHeader);
    const uint32_t fourCC = hdr.ddspf.dwFourCC;
    const uint32_t FOURCC_DXT1 = MakeFourCC('D','X','T','1');
    const uint32_t FOURCC_DXT3 = MakeFourCC('D','X','T','3');
    const uint32_t FOURCC_DXT5 = MakeFourCC('D','X','T','5');
    const uint32_t FOURCC_ATI2 = MakeFourCC('A','T','I','2');
    const uint32_t FOURCC_BC5U = MakeFourCC('B','C','5','U');
    const uint32_t FOURCC_DX10 = MakeFourCC('D','X','1','0');

    CompressedFormat fmt;
    bool fmtOk = false;

    if (fourCC == FOURCC_DX10) {
        // DX10 extended header: 5 × uint32 (20 bytes) right after DDS_HEADER.
        if (buf.size() < dataOffset + 20) {
            ARK_LOG_WARN("Rendering", "DDS: DX10 header truncated '" + displayPath + "'");
            return nullptr;
        }
        uint32_t dxgi = 0;
        std::memcpy(&dxgi, buf.data() + dataOffset, 4);
        dataOffset += 20;

        // Map the subset of DXGI formats Bistro actually uses.
        //   BC1_UNORM       = 71   BC1_UNORM_SRGB = 72
        //   BC2_UNORM       = 74   BC2_UNORM_SRGB = 75
        //   BC3_UNORM       = 77   BC3_UNORM_SRGB = 78
        //   BC4_UNORM       = 80   BC4_SNORM      = 81
        //   BC5_UNORM       = 83   BC5_SNORM      = 84
        //   BC7_UNORM       = 98   BC7_UNORM_SRGB = 99
        switch (dxgi) {
            case 71: fmt = CompressedFormat::BC1_RGB;       fmtOk = true; break;
            case 72: fmt = CompressedFormat::BC1_RGB_sRGB;  fmtOk = true; break;
            case 74: fmt = CompressedFormat::BC3_RGBA;      fmtOk = true; break; // BC2 approx
            case 75: fmt = CompressedFormat::BC3_RGBA_sRGB; fmtOk = true; break;
            case 77: fmt = CompressedFormat::BC3_RGBA;      fmtOk = true; break;
            case 78: fmt = CompressedFormat::BC3_RGBA_sRGB; fmtOk = true; break;
            case 83: case 84: fmt = CompressedFormat::BC5_RG; fmtOk = true; break;
            case 98: fmt = CompressedFormat::BC7_RGBA;      fmtOk = true; break;
            case 99: fmt = CompressedFormat::BC7_RGBA_sRGB; fmtOk = true; break;
            default:
                ARK_LOG_WARN("Rendering", "DDS: unsupported DXGI " + std::to_string(dxgi) +
                    " in '" + displayPath + "'");
                return nullptr;
        }
        // If the asset didn't encode srgb-ness, honor the caller's isSRGB hint.
        if (!isSRGB) {
            if (fmt == CompressedFormat::BC1_RGB_sRGB)  fmt = CompressedFormat::BC1_RGB;
            if (fmt == CompressedFormat::BC3_RGBA_sRGB) fmt = CompressedFormat::BC3_RGBA;
            if (fmt == CompressedFormat::BC7_RGBA_sRGB) fmt = CompressedFormat::BC7_RGBA;
        }
    } else if (fourCC == FOURCC_DXT1) {
        fmt = isSRGB ? CompressedFormat::BC1_RGB_sRGB : CompressedFormat::BC1_RGB;
        fmtOk = true;
    } else if (fourCC == FOURCC_DXT5 || fourCC == FOURCC_DXT3) {
        fmt = isSRGB ? CompressedFormat::BC3_RGBA_sRGB : CompressedFormat::BC3_RGBA;
        fmtOk = true;
    } else if (fourCC == FOURCC_ATI2 || fourCC == FOURCC_BC5U) {
        fmt = CompressedFormat::BC5_RG;
        fmtOk = true;
    }

    if (!fmtOk) {
        char tag[5] = {0};
        std::memcpy(tag, &hdr.ddspf.dwFourCC, 4);
        ARK_LOG_WARN("Rendering",
            std::string("DDS: unsupported FourCC '") + tag + "' in '" + displayPath + "'");
        return nullptr;
    }

    if (dataOffset >= buf.size()) return nullptr;
    const uint8_t* pixels = buf.data() + dataOffset;
    size_t pixelsSize = buf.size() - dataOffset;

    auto texture = std::shared_ptr<RHITexture>(device->CreateTexture().release());
    if (!texture->UploadCompressed(width, height, fmt, pixels, pixelsSize, mipCount)) {
        ARK_LOG_WARN("Rendering", "DDS: UploadCompressed failed '" + displayPath + "'");
        return nullptr;
    }

    ARK_LOG_INFO("Rendering", "Loaded DDS '" + displayPath + "' (" +
        std::to_string(width) + "x" + std::to_string(height) + ", " +
        std::to_string(mipCount) + " mips, " + (isSRGB ? "sRGB" : "linear") + ")");

    return texture;
}

bool EndsWithIgnoreCase(const std::string& s, const char* suffix) {
    size_t sl = s.size();
    size_t tl = std::strlen(suffix);
    if (tl > sl) return false;
    for (size_t i = 0; i < tl; ++i) {
        char a = s[sl - tl + i];
        char b = suffix[i];
        if (a >= 'A' && a <= 'Z') a = static_cast<char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z') b = static_cast<char>(b - 'A' + 'a');
        if (a != b) return false;
    }
    return true;
}

} // namespace

std::shared_ptr<RHITexture> TextureLoader::Load(RHIDevice* device,
                                                const std::string& filepath,
                                                bool isSRGB) {
    if (!device) {
        ARK_LOG_ERROR("Rendering", "TextureLoader::Load: null device");
        return nullptr;
    }

    // v0.2 15.D — 走 MOD-aware VFS：mods/<name>/<logical> 优先，否则落回 content/
    auto resolved = Paths::ResolveResource(filepath).string();

    // DDS → compressed path (Bistro assets).
    if (EndsWithIgnoreCase(resolved, ".dds")) {
        return LoadDDS(device, filepath, resolved, isSRGB);
    }

    stbi_set_flip_vertically_on_load(true); // OpenGL expects bottom-left origin

    int width = 0, height = 0, channels = 0;
    unsigned char* data = stbi_load(resolved.c_str(), &width, &height, &channels, 0);
    if (!data) {
        // 降级为 warn——Bistro 的 DDS 贴图目前还不支持，不应刷屏 ERROR。
        ARK_LOG_WARN("Rendering", "TextureLoader: failed to load '" + filepath +
            "' (resolved: " + resolved + "): " + stbi_failure_reason());
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
