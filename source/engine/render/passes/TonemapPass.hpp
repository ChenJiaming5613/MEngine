#pragma once

#include "render/graph/FrameGraph.hpp"
#include "rhi/GpuResources.hpp"

#include <cstdint>
#include <vector>

namespace mengine::rhi {
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// Exposure and tone mapping from kResourceHdrColor into the backbuffer.
class TonemapPass final : public FrameGraphPass
{
public:
    void Init(rhi::GpuDevice& device);
    void Shutdown();

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    // Rebinds the HDR input, which every compile may recreate.
    void OnCompiled(const FrameGraphResources& resources) override;

private:
    rhi::GpuDevice* m_device = nullptr;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_fragmentCode;
    rhi::DescriptorSetLayoutHandle m_layout;
    rhi::DescriptorSetHandle m_set;
    rhi::SamplerHandle m_sampler;
    rhi::RenderPassOutput m_output;
    rhi::PipelineHandle m_pipeline;
    FrameGraphResourceId m_input;
};

} // namespace mengine::render
