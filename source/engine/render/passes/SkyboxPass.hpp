#pragma once

#include "rhi/GpuResources.hpp"

#include <cstdint>

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// Draws the environment (specular mip 0) behind the scene as a far plane full screen triangle.
class SkyboxPass
{
public:
    void Init(rhi::GpuDevice& device, const rhi::RenderPassOutput& output, rhi::DescriptorSetLayoutHandle frameLayout);
    void Shutdown();

    // Inside the forward render pass, after the opaque geometry.
    void Record(rhi::CommandBuffer& commandBuffer, rhi::DescriptorSetHandle frameSet, uint32_t frameOffset);

private:
    rhi::GpuDevice* m_device = nullptr;
    rhi::PipelineHandle m_pipeline;
};

} // namespace mengine::render
