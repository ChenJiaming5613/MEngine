#include "asset/ImageLoader.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#endif

// The only stb_image implementation in the project.
#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <limits>
#include <memory>

namespace mengine::asset {

namespace {

bool FitsStbSize(std::span<const uint8_t> encoded, std::string* error)
{
    if (encoded.size() > static_cast<size_t>(std::numeric_limits<int>::max()))
    {
        if (error != nullptr)
        {
            *error = "encoded image larger than 2 GB";
        }
        return false;
    }
    return true;
}

} // namespace

std::optional<Image8> DecodeImageRgba8(std::span<const uint8_t> encoded, std::string* error)
{
    if (!FitsStbSize(encoded, error))
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
        stbi_load_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &sourceChannels, STBI_rgb_alpha),
        &stbi_image_free);
    if (pixels == nullptr)
    {
        if (error != nullptr)
        {
            *error = stbi_failure_reason();
        }
        return std::nullopt;
    }

    Image8 image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    return image;
}

std::optional<ImageFloat> DecodeImageHdr(std::span<const uint8_t> encoded, std::string* error)
{
    if (!FitsStbSize(encoded, error))
    {
        return std::nullopt;
    }

    int width = 0;
    int height = 0;
    int sourceChannels = 0;
    std::unique_ptr<float, decltype(&stbi_image_free)> pixels(
        stbi_loadf_from_memory(encoded.data(), static_cast<int>(encoded.size()), &width, &height, &sourceChannels, STBI_rgb_alpha),
        &stbi_image_free);
    if (pixels == nullptr)
    {
        if (error != nullptr)
        {
            *error = stbi_failure_reason();
        }
        return std::nullopt;
    }

    ImageFloat image;
    image.width = static_cast<uint32_t>(width);
    image.height = static_cast<uint32_t>(height);
    image.rgba.assign(pixels.get(), pixels.get() + static_cast<size_t>(width) * static_cast<size_t>(height) * 4);
    return image;
}

} // namespace mengine::asset
