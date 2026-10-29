#pragma once

#include "render/RenderTypes.hpp"
#include "rhi/GpuResources.hpp"

#include <cstdint>
#include <span>
#include <string_view>

namespace mengine::render {

struct Scene;

// Frame graph resources shared between passes, by name.
inline constexpr std::string_view kResourceShadowMap = "ShadowMap";
inline constexpr std::string_view kResourceHdrColor = "HdrColor";
inline constexpr std::string_view kResourceSceneDepth = "SceneDepth";
inline constexpr std::string_view kResourceBackbuffer = "Backbuffer";

inline constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

// Per frame inputs of the frame graph passes, filled by Renderer::Render.
struct RenderView
{
    const Scene* scene = nullptr;
    // Indices into scene->draws that survived the camera frustum culling, in draw order.
    std::span<const uint32_t> visibleDraws;
    const RenderSettings* settings = nullptr;
    rhi::DescriptorSetHandle frameSet;
    // Dynamic offset of this frame's FrameConstants in the frame set.
    uint32_t frameOffset = 0;
    // The swapchain is UNORM: tone mapping applies the sRGB transfer function itself.
    bool encodeSrgb = false;
};

} // namespace mengine::render
