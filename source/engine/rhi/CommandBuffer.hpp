#pragma once

#include "rhi/GpuResources.hpp"

#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace mengine::rhi {

class GpuDevice;

// Thin recording wrapper over a primary VkCommandBuffer that resolves handles through the device, as in Raptor.
class CommandBuffer
{
public:
    void Init(GpuDevice* device, VkCommandBuffer commandBuffer);
    void Reset();

    VkCommandBuffer Handle() const { return m_commandBuffer; }

    void SetClearColor(uint32_t attachmentIndex, const glm::vec4& color);
    void SetClearDepth(float depth);

    // Begins the pass and sets viewport and scissor to its full extent.
    void BindPass(RenderPassHandle renderPass);
    void EndPass();

    void BindPipeline(PipelineHandle pipeline);
    void BindVertexBuffer(BufferHandle buffer, uint32_t binding, VkDeviceSize offset = 0);
    void BindIndexBuffer(BufferHandle buffer, VkDeviceSize offset, VkIndexType indexType);
    // setIndex is the set number in the currently bound pipeline's layout.
    void BindDescriptorSet(DescriptorSetHandle set, uint32_t setIndex, std::span<const uint32_t> dynamicOffsets = {});
    void PushConstants(const void* data, uint32_t size, uint32_t offset = 0);

    void SetViewport(float x, float y, float width, float height);
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
        uint32_t firstInstance = 0);

    // Debug utils labels; no-ops without VK_EXT_debug_utils.
    void PushMarker(const char* name);
    void PopMarker();

    // GPU timestamp scope plus a debug label of the same name.
    void PushTimestamp(const char* name);
    void PopTimestamp();

private:
    GpuDevice* m_device = nullptr;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    const Pipeline* m_currentPipeline = nullptr;
    bool m_insidePass = false;

    std::array<VkClearValue, kMaxImageOutputs + 1> m_clearValues{};
};

} // namespace mengine::rhi
