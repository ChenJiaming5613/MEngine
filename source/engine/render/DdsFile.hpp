#pragma once

// MEngine: port of NVIDIA Donut's DDS loader, see DdsFile.cpp for the source and the list of changes.
// The types below replace the parts of donut/engine/TextureCache.h and nvrhi that the loader uses.

#include <vulkan/vulkan.h>

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace mengine::render {

// donut::engine::TextureAlphaMode (the DDS_ALPHA_MODE values of DirectXTK).
enum class TextureAlphaMode
{
    UNKNOWN = 0,
    STRAIGHT = 1,
    PREMULTIPLIED = 2,
    OPAQUE_ = 3,
    CUSTOM = 4,
};

// Subset of nvrhi::TextureDimension that a DDS file can describe.
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

// donut::engine::TextureSubresourceData: one mip level of one array slice inside TextureData::data.
struct TextureSubresourceData
{
    size_t rowPitch = 0;
    size_t depthPitch = 0;
    ptrdiff_t dataOffset = 0;
    size_t dataSize = 0;
};

// The members of donut::engine::TextureData that LoadDDSTextureFromMemory reads and writes.
struct TextureData
{
    std::span<const uint8_t> data; // MEngine: was std::shared_ptr<vfs::IBlob>; not owned, must outlive the TextureData
    bool forceSRGB = false;

    VkFormat format = VK_FORMAT_UNDEFINED; // MEngine: was nvrhi::Format
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

// Initialized the TextureInfo from the 'data' array, which must be populated with DDS data
bool LoadDDSTextureFromMemory(TextureData& textureInfo);

} // namespace mengine::render
