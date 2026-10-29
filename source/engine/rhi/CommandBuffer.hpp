#pragma once

#include "rhi/GpuResources.hpp"

#include <glm/vec4.hpp>

#include <array>
#include <cstdint>
#include <span>

namespace mengine::rhi {

class GpuDevice;

// Thin recording wrapper over a primary VkCommandBuffer that resolves handles through the device, as in Raptor.
//
// Differences from Raptor's CommandBuffer:
// - Barrier() refuses to run inside a render pass instead of silently ending it.
// - BindPass/EndPass keep the tracked state of the pass attachments, and BindPass inserts the barrier a pass needs when
//   an attachment was last used outside the stages its subpass dependency covers (compute, transfer).
// - Indirect draws honor drawCount and stride (Raptor's chapter 1 always records one draw).
class CommandBuffer
{
public:
    void Init(GpuDevice* device, VkCommandBuffer commandBuffer);
    void Reset();

    VkCommandBuffer Handle() const { return m_commandBuffer; }
    bool IsInsidePass() const { return m_insidePass; }

    void SetClearColor(uint32_t attachmentIndex, const glm::vec4& color);
    void SetClearDepth(float depth);

    // -- Render passes --------------------------------------------------------------------------------------------------
    // Begins the pass and sets viewport and scissor to its full extent.
    void BindPass(RenderPassHandle renderPass);
    void EndPass();

    // -- State ----------------------------------------------------------------------------------------------------------
    // Graphics or compute; the bind point follows the pipeline.
    void BindPipeline(PipelineHandle pipeline);
    void BindVertexBuffer(BufferHandle buffer, uint32_t binding, VkDeviceSize offset = 0);
    void BindIndexBuffer(BufferHandle buffer, VkDeviceSize offset, VkIndexType indexType);
    // setIndex is the set number in the currently bound pipeline's layout.
    void BindDescriptorSet(DescriptorSetHandle set, uint32_t setIndex, std::span<const uint32_t> dynamicOffsets = {});
    void PushConstants(const void* data, uint32_t size, uint32_t offset = 0);

    void SetViewport(float x, float y, float width, float height);
    void SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height);

    // -- Draws (inside a pass, graphics pipeline bound) -----------------------------------------------------------------
    void Draw(uint32_t vertexCount, uint32_t instanceCount = 1, uint32_t firstVertex = 0, uint32_t firstInstance = 0);
    void DrawIndexed(uint32_t indexCount, uint32_t instanceCount = 1, uint32_t firstIndex = 0, int32_t vertexOffset = 0,
        uint32_t firstInstance = 0);
    // The buffer needs VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT and holds VkDrawIndirectCommand / VkDrawIndexedIndirectCommand
    // records. drawCount > 1 needs GpuDevice::SupportsMultiDrawIndirect(); a non-zero firstInstance in a record needs
    // SupportsDrawIndirectFirstInstance().
    void DrawIndirect(BufferHandle buffer, VkDeviceSize offset = 0, uint32_t drawCount = 1,
        uint32_t stride = sizeof(VkDrawIndirectCommand));
    void DrawIndexedIndirect(BufferHandle buffer, VkDeviceSize offset = 0, uint32_t drawCount = 1,
        uint32_t stride = sizeof(VkDrawIndexedIndirectCommand));

    // -- Compute (outside a pass, compute pipeline bound) ---------------------------------------------------------------
    void Dispatch(uint32_t groupCountX, uint32_t groupCountY = 1, uint32_t groupCountZ = 1);
    // Reads a VkDispatchIndirectCommand from a buffer with VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT.
    void DispatchIndirect(BufferHandle buffer, VkDeviceSize offset = 0);

    // -- Transfer (outside a pass) --------------------------------------------------------------------------------------
    // Writes a repeated 32-bit value; needs VK_BUFFER_USAGE_TRANSFER_DST_BIT. Offset and size are multiples of 4.
    // Follow it with a barrier from kResourceStateCopyDest.
    void FillBuffer(BufferHandle buffer, uint32_t value, VkDeviceSize offset = 0, VkDeviceSize size = VK_WHOLE_SIZE);

    // -- Synchronization (outside a pass) -------------------------------------------------------------------------------
    // Records one vkCmdPipelineBarrier for every entry and updates the tracked state of fully covered textures.
    // Entries going from a read-only state to a subset of it (same layout) are dropped; nothing is recorded if all are.
    void Barrier(const ExecutionBarrier& barrier);
    void TextureBarrier(TextureHandle texture, ResourceStates newState);
    void BufferBarrier(BufferHandle buffer, ResourceStates oldState, ResourceStates newState);
    void GlobalBarrier(ResourceStates oldState, ResourceStates newState);

    // -- Debugging ------------------------------------------------------------------------------------------------------
    // Debug utils labels; no-ops without VK_EXT_debug_utils.
    void PushMarker(const char* name);
    void PopMarker();

    // GPU timestamp scope plus a debug label of the same name.
    void PushTimestamp(const char* name);
    void PopTimestamp();

private:
    // Barrier for the attachments of a pass about to begin; see BindPass.
    void PrepareAttachments(const RenderPass& renderPass);
    // States the pass's final layouts leave the attachments in.
    void UpdateAttachmentStates(const RenderPass& renderPass);
    const Buffer* AccessIndirectBuffer(BufferHandle handle, VkDeviceSize offset, VkDeviceSize recordSize, uint32_t drawCount,
        uint32_t stride, const char* command) const;

    GpuDevice* m_device = nullptr;
    VkCommandBuffer m_commandBuffer = VK_NULL_HANDLE;
    const Pipeline* m_currentPipeline = nullptr;
    RenderPassHandle m_currentPass;
    bool m_insidePass = false;

    std::array<VkClearValue, kMaxImageOutputs + 1> m_clearValues{};
};

} // namespace mengine::rhi
