#pragma once

#include "render/graph/FrameGraph.hpp"
#include "rhi/GpuResources.hpp"

#include <cstdint>
#include <vector>

namespace mengine::rhi {
class GfxDevice;
} // namespace mengine::rhi

namespace mengine::render {

class DebugDraw;

// Draws DebugDraw's lines after the skybox, which would otherwise cover them (lines do not write depth). Loads HdrColor
// and SceneDepth, so it shares the forward render pass. Inactive in frames without lines.
class DebugDrawPass final : public FrameGraphPass
{
public:
    void Init(rhi::GfxDevice& device, rhi::DescriptorSetLayoutHandle frameLayout, const DebugDraw& lines);
    void Shutdown();

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    bool IsActive() const override;
    void OnCompiled(const FrameGraphResources& resources) override;
    // PassInspectors.cpp.
    const char* Description() const override;
    bool DrawInspector(RenderSettings& settings) override;

private:
    rhi::GfxDevice* m_device = nullptr;
    const DebugDraw* m_lines = nullptr;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_fragmentCode;
    rhi::RenderPassOutput m_output;
    rhi::PipelineHandle m_pipeline;
};

} // namespace mengine::render
