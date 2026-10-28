#pragma once

#include <cstdint>

namespace mengine::rhi {

constexpr uint32_t kMaxFramesInFlight = 2;

enum class ResourceUsage : uint8_t
{
    Immutable, // device local, filled once through a staging upload
    Dynamic,   // host visible and persistently mapped
};

enum class TextureType : uint8_t
{
    Texture2D,
    TextureCube,
};

enum class RenderPassOperation : uint8_t
{
    DontCare,
    Load,
    Clear,
};

enum TextureFlags : uint8_t
{
    kTextureFlagNone = 0,
    kTextureFlagRenderTarget = 1 << 0,
    // Only level 0 is provided; the remaining levels are generated with linear blits.
    kTextureFlagGenerateMips = 1 << 1,
};

} // namespace mengine::rhi
