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
    // Usable as a STORAGE_IMAGE (compute writes). The format must support VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT.
    kTextureFlagStorage = 1 << 2,
};

// How a resource is about to be (or was last) accessed, after Raptor's ResourceState (itself D3D12's
// D3D12_RESOURCE_STATES). Read-only states can be combined. A barrier turns a pair of states into layouts, access masks
// and pipeline stages (VulkanUtils). Textures track their current state; buffer barriers name both ends.
enum ResourceState : uint32_t
{
    kResourceStateUndefined = 0,
    kResourceStateVertexAndConstantBuffer = 1u << 0,
    kResourceStateIndexBuffer = 1u << 1,
    kResourceStateRenderTarget = 1u << 2,
    kResourceStateUnorderedAccess = 1u << 3, // storage buffer or storage image, read and write
    kResourceStateDepthWrite = 1u << 4,
    kResourceStateDepthRead = 1u << 5,              // read-only depth attachment (sampling uses a shader resource state)
    kResourceStateNonPixelShaderResource = 1u << 6, // read in vertex or compute shaders
    kResourceStatePixelShaderResource = 1u << 7,    // read in fragment shaders
    kResourceStateShaderResource = kResourceStateNonPixelShaderResource | kResourceStatePixelShaderResource,
    kResourceStateIndirectArgument = 1u << 8,
    kResourceStateCopyDest = 1u << 9,
    kResourceStateCopySource = 1u << 10,
    kResourceStatePresent = 1u << 11,
    kResourceStateCommon = 1u << 12, // GENERAL layout and every access: a heavy fallback
    kResourceStateGenericRead = kResourceStateVertexAndConstantBuffer | kResourceStateIndexBuffer | kResourceStateShaderResource
        | kResourceStateIndirectArgument | kResourceStateCopySource,
};

// A combination of ResourceState bits.
using ResourceStates = uint32_t;

// States that write. A barrier between two equal states is only needed when they write (write-after-write).
constexpr ResourceStates kResourceStateWriteMask = kResourceStateRenderTarget | kResourceStateUnorderedAccess
    | kResourceStateDepthWrite | kResourceStateCopyDest | kResourceStateCommon;

} // namespace mengine::rhi
