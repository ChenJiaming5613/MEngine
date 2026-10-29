#pragma once

#include "render/graph/FrameGraph.hpp"
#include "rhi/GpuResources.hpp"

#include <cstdint>
#include <vector>

namespace mengine::rhi {
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// Draws the environment (specular mip 0) behind the scene as a far plane full screen triangle. Loads the forward
// pass's HDR target and depth, so the frame graph merges it into the forward render pass.
class SkyboxPass final : public FrameGraphPass
{
public:
    void Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout);
    void Shutdown();

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    void OnCompiled(const FrameGraphResources& resources) override;

private:
    rhi::GpuDevice* m_device = nullptr;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_fragmentCode;
    rhi::RenderPassOutput m_output;
    rhi::PipelineHandle m_pipeline;
};

} // namespace mengine::render
