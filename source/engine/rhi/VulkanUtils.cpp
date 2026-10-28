#include "rhi/VulkanUtils.hpp"

#include "core/FileSystem.hpp"

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>
#include <stdexcept>

namespace mengine::rhi {

const char* VkResultName(VkResult result)
{
    switch (result)
    {
    case VK_SUCCESS: return "VK_SUCCESS";
    case VK_NOT_READY: return "VK_NOT_READY";
    case VK_TIMEOUT: return "VK_TIMEOUT";
    case VK_INCOMPLETE: return "VK_INCOMPLETE";
    case VK_SUBOPTIMAL_KHR: return "VK_SUBOPTIMAL_KHR";
    case VK_ERROR_OUT_OF_HOST_MEMORY: return "VK_ERROR_OUT_OF_HOST_MEMORY";
    case VK_ERROR_OUT_OF_DEVICE_MEMORY: return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
    case VK_ERROR_INITIALIZATION_FAILED: return "VK_ERROR_INITIALIZATION_FAILED";
    case VK_ERROR_DEVICE_LOST: return "VK_ERROR_DEVICE_LOST";
    case VK_ERROR_MEMORY_MAP_FAILED: return "VK_ERROR_MEMORY_MAP_FAILED";
    case VK_ERROR_LAYER_NOT_PRESENT: return "VK_ERROR_LAYER_NOT_PRESENT";
    case VK_ERROR_EXTENSION_NOT_PRESENT: return "VK_ERROR_EXTENSION_NOT_PRESENT";
    case VK_ERROR_FEATURE_NOT_PRESENT: return "VK_ERROR_FEATURE_NOT_PRESENT";
    case VK_ERROR_INCOMPATIBLE_DRIVER: return "VK_ERROR_INCOMPATIBLE_DRIVER";
    case VK_ERROR_TOO_MANY_OBJECTS: return "VK_ERROR_TOO_MANY_OBJECTS";
    case VK_ERROR_FORMAT_NOT_SUPPORTED: return "VK_ERROR_FORMAT_NOT_SUPPORTED";
    case VK_ERROR_FRAGMENTED_POOL: return "VK_ERROR_FRAGMENTED_POOL";
    case VK_ERROR_OUT_OF_POOL_MEMORY: return "VK_ERROR_OUT_OF_POOL_MEMORY";
    case VK_ERROR_SURFACE_LOST_KHR: return "VK_ERROR_SURFACE_LOST_KHR";
    case VK_ERROR_NATIVE_WINDOW_IN_USE_KHR: return "VK_ERROR_NATIVE_WINDOW_IN_USE_KHR";
    case VK_ERROR_OUT_OF_DATE_KHR: return "VK_ERROR_OUT_OF_DATE_KHR";
    default: return "VK_RESULT_UNKNOWN";
    }
}

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(operation) + " failed with " + VkResultName(result) + " ("
            + std::to_string(static_cast<int>(result)) + ")");
    }
}

bool IsDepthFormat(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_D16_UNORM:
    case VK_FORMAT_X8_D24_UNORM_PACK32:
    case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:
    case VK_FORMAT_D24_UNORM_S8_UINT:
    case VK_FORMAT_D32_SFLOAT_S8_UINT:
        return true;
    default:
        return false;
    }
}

bool HasStencil(VkFormat format)
{
    return format == VK_FORMAT_D16_UNORM_S8_UINT || format == VK_FORMAT_D24_UNORM_S8_UINT || format == VK_FORMAT_D32_SFLOAT_S8_UINT;
}

bool IsSrgbFormat(VkFormat format)
{
    return format == VK_FORMAT_R8G8B8A8_SRGB || format == VK_FORMAT_B8G8R8A8_SRGB || format == VK_FORMAT_A8B8G8R8_SRGB_PACK32;
}

uint32_t FormatTexelSize(VkFormat format)
{
    switch (format)
    {
    case VK_FORMAT_R8_UNORM:
        return 1;
    case VK_FORMAT_R8G8_UNORM:
    case VK_FORMAT_R16_SFLOAT:
        return 2;
    case VK_FORMAT_R8G8B8A8_UNORM:
    case VK_FORMAT_R8G8B8A8_SRGB:
    case VK_FORMAT_B8G8R8A8_UNORM:
    case VK_FORMAT_B8G8R8A8_SRGB:
    case VK_FORMAT_R16G16_SFLOAT:
    case VK_FORMAT_R32_SFLOAT:
        return 4;
    case VK_FORMAT_R16G16B16A16_SFLOAT:
    case VK_FORMAT_R32G32_SFLOAT:
        return 8;
    case VK_FORMAT_R32G32B32A32_SFLOAT:
        return 16;
    default:
        return 0;
    }
}

bool IsBlockCompressedFormat(VkFormat format)
{
    return format >= VK_FORMAT_BC1_RGB_UNORM_BLOCK && format <= VK_FORMAT_BC7_SRGB_BLOCK;
}

VkDeviceSize FormatLevelSize(VkFormat format, uint32_t width, uint32_t height)
{
    if (IsBlockCompressedFormat(format))
    {
        // BC1 and BC4 use 8 bytes per 4x4 block, the other BC formats 16.
        const bool eightBytes = format <= VK_FORMAT_BC1_RGBA_SRGB_BLOCK || format == VK_FORMAT_BC4_UNORM_BLOCK ||
                                format == VK_FORMAT_BC4_SNORM_BLOCK;
        return VkDeviceSize{(width + 3) / 4} * ((height + 3) / 4) * (eightBytes ? 8u : 16u);
    }
    return VkDeviceSize{width} * height * FormatTexelSize(format);
}

uint32_t ComputeMipCount(uint32_t width, uint32_t height)
{
    const uint32_t largest = std::max(std::max(width, height), 1u);
    return static_cast<uint32_t>(std::bit_width(largest));
}

VkAttachmentLoadOp ToVkLoadOp(RenderPassOperation operation)
{
    switch (operation)
    {
    case RenderPassOperation::Load:
        return VK_ATTACHMENT_LOAD_OP_LOAD;
    case RenderPassOperation::Clear:
        return VK_ATTACHMENT_LOAD_OP_CLEAR;
    case RenderPassOperation::DontCare:
    default:
        return VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    }
}

std::vector<uint32_t> LoadSpirvAsset(const std::string& assetPath)
{
    const std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(assetPath);
    if (!bytes.has_value())
    {
        throw std::runtime_error("Failed to load " + assetPath);
    }
    if (bytes->empty() || bytes->size() % sizeof(uint32_t) != 0)
    {
        throw std::runtime_error(assetPath + " is not a SPIR-V binary");
    }
    std::vector<uint32_t> words(bytes->size() / sizeof(uint32_t));
    std::memcpy(words.data(), bytes->data(), bytes->size());
    return words;
}

} // namespace mengine::rhi
