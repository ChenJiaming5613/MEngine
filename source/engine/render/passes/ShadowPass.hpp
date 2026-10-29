#pragma once

#include "render/RenderTypes.hpp"
#include "render/graph/FrameGraph.hpp"
#include "rhi/GpuResources.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

struct Scene;

// D16 depth only shadow map of the directional light, rendered with FrameConstants::lightViewProjection and imported
// into the graph as kResourceShadowMap (Forward samples it through a compare sampler). The map persists across frames
// and is re-rendered only when the light, the scene or the shadow toggle changes.
class ShadowPass final : public FrameGraphPass
{
public:
    static constexpr uint32_t kSize = 2048;
    static constexpr VkFormat kFormat = VK_FORMAT_D16_UNORM;

    void Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout, rhi::DescriptorSetLayoutHandle materialLayout);
    void Shutdown();

    // Creates the variants the scene's draws use, so the first frame does not stall on pipeline creation.
    void WarmPipelines(const Scene& scene);
    // Called every frame before the graph runs; decides whether the map is re-rendered. Without casters the map is
    // only cleared (once), so it is always sampleable.
    void Prepare(bool castShadows, const glm::vec3& direction, uint64_t sceneVersion);

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    bool IsActive() const override { return m_dirty; }
    void OnCompiled(const FrameGraphResources& resources) override;
    // PassInspectors.cpp.
    const char* Description() const override;
    bool DrawInspector(RenderSettings& settings) override;

    rhi::TextureHandle Texture() const { return m_texture; }
    rhi::SamplerHandle Sampler() const { return m_sampler; }

private:
    static constexpr uint32_t kKeyMask = kPipelineKeyAlphaMask | kPipelineKeyDoubleSided | kPipelineKeyMirrored;

    rhi::PipelineHandle GetOrCreatePipeline(uint32_t key);
    void DestroyPipelines();

    rhi::GpuDevice* m_device = nullptr;
    rhi::TextureHandle m_texture;
    rhi::SamplerHandle m_sampler;
    rhi::RenderPassOutput m_output;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_vertexMaskCode;
    std::vector<uint32_t> m_fragmentMaskCode;
    std::array<rhi::PipelineHandle, kKeyMask + 1> m_pipelines{};
    bool m_missingPipelineReported = false;

    // This frame's decision (Prepare) and the state the map was last rendered with.
    bool m_dirty = true;
    bool m_drawScene = false;
    bool m_rendered = false;
    bool m_castersDrawn = false;
    glm::vec3 m_direction{0.0f};
    uint64_t m_sceneVersion = ~0ull;
    uint64_t m_pendingSceneVersion = 0;
    glm::vec3 m_pendingDirection{0.0f};
};

} // namespace mengine::render
