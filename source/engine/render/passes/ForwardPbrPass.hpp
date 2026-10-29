#pragma once

#include "render/RenderTypes.hpp"
#include "render/graph/FrameGraph.hpp"
#include "rhi/GpuResources.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

struct Scene;

// Direct lighting (point light, shadowed directional light) plus IBL for every glTF draw. Creates the HDR target
// (kResourceHdrColor) and the scene depth (kResourceSceneDepth) and clears both; samples kResourceShadowMap. Pipeline
// variants are keyed by PipelineKeyBits and created on first use.
class ForwardPbrPass final : public FrameGraphPass
{
public:
    void Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout, rhi::DescriptorSetLayoutHandle materialLayout);
    void Shutdown();

    // Creates the variants the scene's draws use, so the first frame does not stall on pipeline creation.
    void WarmPipelines(const Scene& scene);

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    void OnCompiled(const FrameGraphResources& resources) override;
    // PassInspectors.cpp.
    const char* Description() const override;
    bool DrawInspector(RenderSettings& settings) override;

private:
    rhi::PipelineHandle Pipeline(uint32_t key);
    void DestroyPipelines();

    rhi::GpuDevice* m_device = nullptr;
    rhi::RenderPassOutput m_output;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_vertexTangentCode;
    std::vector<uint32_t> m_fragmentCode;
    std::array<rhi::PipelineHandle, kPipelineKeyCount> m_pipelines{};
};

} // namespace mengine::render
