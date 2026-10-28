#pragma once

#include "rhi/GpuResources.hpp"

#include <cstdint>

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// Exposure and tone mapping from the HDR target into the swapchain.
class TonemapPass
{
public:
    void Init(rhi::GpuDevice& device, const rhi::RenderPassOutput& output);
    void Shutdown();

    // Must be called again after the HDR texture is resized.
    void SetInput(rhi::TextureHandle hdrColor);

    // Inside the swapchain pass.
    void Record(rhi::CommandBuffer& commandBuffer, float exposure, bool encodeSrgb);

private:
    rhi::GpuDevice* m_device = nullptr;
    rhi::DescriptorSetLayoutHandle m_layout;
    rhi::DescriptorSetHandle m_set;
    rhi::SamplerHandle m_sampler;
    rhi::PipelineHandle m_pipeline;
};

} // namespace mengine::render
