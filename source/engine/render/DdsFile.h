#pragma once

// DDS parser ported from NVIDIA Donut (see DdsFile.cpp); these types stand in for Donut's TextureCache.h and nvrhi ones.

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mengine::render {

// The DDS_ALPHA_MODE values of DirectXTK.
enum class TextureAlphaMode
{
    UNKNOWN = 0,
    STRAIGHT = 1,
    PREMULTIPLIED = 2,
    OPAQUE_ = 3,
    CUSTOM = 4,
};

// The dimensions a DDS file can describe.
enum class TextureDimension
{
    Unknown,
    Texture1D,
    Texture1DArray,
    Texture2D,
    Texture2DArray,
    TextureCube,
    TextureCubeArray,
    Texture3D,
};

// One mip level of one array slice; dataOffset is relative to TextureData::data.
struct TextureSubresourceData
{
    size_t rowPitch = 0;
    size_t depthPitch = 0;
    ptrdiff_t dataOffset = 0;
    size_t dataSize = 0;
};

// Input (data, forceSRGB) and output of LoadDDSTextureFromMemory.
struct TextureData
{
    std::span<const uint8_t> data; // not owned, must outlive the TextureData
    bool forceSRGB = false;

    VkFormat format = VK_FORMAT_UNDEFINED;
    uint32_t width = 1;
    uint32_t height = 1;
    uint32_t depth = 1;
    uint32_t arraySize = 1; // cube maps count 6 per cube
    uint32_t mipLevels = 1;
    TextureDimension dimension = TextureDimension::Unknown;
    TextureAlphaMode alphaMode = TextureAlphaMode::UNKNOWN;
    uint32_t originalBitsPerPixel = 0;
    std::vector<std::vector<TextureSubresourceData>> dataLayout; // [arraySlice][mipLevel]
};

// Parses the DDS file in textureInfo.data and fills in the rest; false for invalid or unsupported files.
bool LoadDDSTextureFromMemory(TextureData& textureInfo);

} // namespace mengine::render
