#pragma once

#include "render/RenderTypes.hpp"
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

// Depth only shadow map of the directional light (MEngine's addition to the UE4 2013 notes). One D16 map rendered
// with FrameConstants::lightViewProjection; it ends in SHADER_READ_ONLY and is sampled through a compare sampler.
// Alpha MASK materials are cut out, double sided and mirrored draws keep the forward pass's culling.
class ShadowPass
{
public:
    static constexpr uint32_t kSize = 2048;
    static constexpr VkFormat kFormat = VK_FORMAT_D16_UNORM;

    void Init(rhi::GpuDevice& device, rhi::DescriptorSetLayoutHandle frameLayout, rhi::DescriptorSetLayoutHandle materialLayout);
    void Shutdown();

    // Creates the variants the scene's draws use, so the first frame does not stall on pipeline creation.
    void WarmPipelines(const Scene& scene);
    // Records the whole render pass (clear, then every draw unless drawScene is false).
    void Record(rhi::CommandBuffer& commandBuffer, const Scene& scene, bool drawScene, rhi::DescriptorSetHandle frameSet,
        uint32_t frameOffset);

    rhi::TextureHandle Texture() const { return m_texture; }
    rhi::SamplerHandle Sampler() const { return m_sampler; }

private:
    static constexpr uint32_t kKeyMask = kPipelineKeyAlphaMask | kPipelineKeyDoubleSided | kPipelineKeyMirrored;

    rhi::PipelineHandle Pipeline(uint32_t key);

    rhi::GpuDevice* m_device = nullptr;
    rhi::TextureHandle m_texture;
    rhi::SamplerHandle m_sampler;
    rhi::RenderPassHandle m_renderPass;
    rhi::RenderPassOutput m_output;
    rhi::DescriptorSetLayoutHandle m_frameLayout;
    rhi::DescriptorSetLayoutHandle m_materialLayout;
    std::vector<uint32_t> m_vertexCode;
    std::vector<uint32_t> m_vertexMaskCode;
    std::vector<uint32_t> m_fragmentMaskCode;
    std::array<rhi::PipelineHandle, kKeyMask + 1> m_pipelines{};
};

} // namespace mengine::render
