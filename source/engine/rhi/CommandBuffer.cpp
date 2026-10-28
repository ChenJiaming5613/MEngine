#include "rhi/CommandBuffer.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "rhi/GpuDevice.hpp"

namespace mengine::rhi {

namespace {

constexpr VkClearColorValue kDefaultClearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};

} // namespace

void CommandBuffer::Init(GpuDevice* device, VkCommandBuffer commandBuffer)
{
    m_device = device;
    m_commandBuffer = commandBuffer;
    m_currentPipeline = nullptr;
    m_insidePass = false;
    for (uint32_t index = 0; index < kMaxImageOutputs; ++index)
    {
        m_clearValues[index].color = kDefaultClearColor;
    }
    m_clearValues[kMaxImageOutputs].depthStencil = VkClearDepthStencilValue{.depth = 1.0f, .stencil = 0};
}

void CommandBuffer::Reset()
{
    MENGINE_ASSERT(!m_insidePass);
    m_currentPipeline = nullptr;
    m_insidePass = false;
}

void CommandBuffer::SetClearColor(uint32_t attachmentIndex, const glm::vec4& color)
{
    if (attachmentIndex < kMaxImageOutputs)
    {
        m_clearValues[attachmentIndex].color = VkClearColorValue{{color.r, color.g, color.b, color.a}};
    }
}

void CommandBuffer::SetClearDepth(float depth)
{
    // Stored after the color slots; BindPass moves it right behind the pass's color attachments.
    m_clearValues[kMaxImageOutputs].depthStencil = VkClearDepthStencilValue{.depth = depth, .stencil = 0};
}

void CommandBuffer::BindPass(RenderPassHandle renderPassHandle)
{
    MENGINE_ASSERT(!m_insidePass);
    const RenderPass* renderPass = m_device->AccessRenderPass(renderPassHandle);
    if (renderPass == nullptr)
    {
        MENGINE_LOG_ERROR("BindPass: invalid render pass handle");
        return;
    }

    std::array<VkClearValue, kMaxImageOutputs + 1> clearValues{};
    uint32_t clearCount = 0;
    for (uint32_t index = 0; index < renderPass->output.colorCount; ++index)
    {
        clearValues[clearCount++] = m_clearValues[index];
    }
    if (renderPass->output.depthFormat != VK_FORMAT_UNDEFINED)
    {
        clearValues[clearCount++] = m_clearValues[kMaxImageOutputs];
    }

    const VkRect2D renderArea{.offset = {0, 0}, .extent = {renderPass->width, renderPass->height}};
    const VkRenderPassBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
        .renderPass = renderPass->renderPass,
        .framebuffer = m_device->GetFramebuffer(*renderPass),
        .renderArea = renderArea,
        .clearValueCount = clearCount,
        .pClearValues = clearValues.data(),
    };
    vkCmdBeginRenderPass(m_commandBuffer, &beginInfo, VK_SUBPASS_CONTENTS_INLINE);
    m_insidePass = true;
    m_currentPipeline = nullptr;

    SetViewport(0.0f, 0.0f, static_cast<float>(renderPass->width), static_cast<float>(renderPass->height));
    SetScissor(0, 0, renderPass->width, renderPass->height);
}

void CommandBuffer::EndPass()
{
    if (!m_insidePass)
    {
        return;
    }
    vkCmdEndRenderPass(m_commandBuffer);
    m_insidePass = false;
    m_currentPipeline = nullptr;
}

void CommandBuffer::BindPipeline(PipelineHandle pipelineHandle)
{
    const Pipeline* pipeline = m_device->AccessPipeline(pipelineHandle);
    if (pipeline == nullptr)
    {
        MENGINE_LOG_ERROR("BindPipeline: invalid pipeline handle");
        return;
    }
    vkCmdBindPipeline(m_commandBuffer, pipeline->bindPoint, pipeline->pipeline);
    m_currentPipeline = pipeline;
}

void CommandBuffer::BindVertexBuffer(BufferHandle bufferHandle, uint32_t binding, VkDeviceSize offset)
{
    const Buffer* buffer = m_device->AccessBuffer(bufferHandle);
    if (buffer == nullptr)
    {
        MENGINE_LOG_ERROR("BindVertexBuffer: invalid buffer handle");
        return;
    }
    vkCmdBindVertexBuffers(m_commandBuffer, binding, 1, &buffer->buffer, &offset);
}

void CommandBuffer::BindIndexBuffer(BufferHandle bufferHandle, VkDeviceSize offset, VkIndexType indexType)
{
    const Buffer* buffer = m_device->AccessBuffer(bufferHandle);
    if (buffer == nullptr)
    {
        MENGINE_LOG_ERROR("BindIndexBuffer: invalid buffer handle");
        return;
    }
    vkCmdBindIndexBuffer(m_commandBuffer, buffer->buffer, offset, indexType);
}

void CommandBuffer::BindDescriptorSet(DescriptorSetHandle setHandle, uint32_t setIndex, std::span<const uint32_t> dynamicOffsets)
{
    const DescriptorSet* set = m_device->AccessDescriptorSet(setHandle);
    if (set == nullptr || m_currentPipeline == nullptr)
    {
        MENGINE_LOG_ERROR("BindDescriptorSet: invalid set handle or no pipeline bound");
        return;
    }
    vkCmdBindDescriptorSets(m_commandBuffer, m_currentPipeline->bindPoint, m_currentPipeline->layout, setIndex, 1, &set->set,
        static_cast<uint32_t>(dynamicOffsets.size()), dynamicOffsets.data());
}

void CommandBuffer::PushConstants(const void* data, uint32_t size, uint32_t offset)
{
    if (m_currentPipeline == nullptr)
    {
        MENGINE_LOG_ERROR("PushConstants: no pipeline bound");
        return;
    }
    MENGINE_ASSERT(offset + size <= m_currentPipeline->pushConstantSize);
    vkCmdPushConstants(m_commandBuffer, m_currentPipeline->layout, m_currentPipeline->pushConstantStages, offset, size, data);
}

void CommandBuffer::SetViewport(float x, float y, float width, float height)
{
    const VkViewport viewport{.x = x, .y = y, .width = width, .height = height, .minDepth = 0.0f, .maxDepth = 1.0f};
    vkCmdSetViewport(m_commandBuffer, 0, 1, &viewport);
}

void CommandBuffer::SetScissor(int32_t x, int32_t y, uint32_t width, uint32_t height)
{
    const VkRect2D scissor{.offset = {x, y}, .extent = {width, height}};
    vkCmdSetScissor(m_commandBuffer, 0, 1, &scissor);
}

void CommandBuffer::Draw(uint32_t vertexCount, uint32_t instanceCount, uint32_t firstVertex, uint32_t firstInstance)
{
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

void CommandBuffer::PushMarker(const char* name)
{
    m_device->CmdBeginLabel(m_commandBuffer, name);
}

void CommandBuffer::PopMarker()
{
    m_device->CmdEndLabel(m_commandBuffer);
}

void CommandBuffer::PushTimestamp(const char* name)
{
    PushMarker(name);
    m_device->GetProfiler().Push(m_commandBuffer, name);
}

void CommandBuffer::PopTimestamp()
{
    m_device->GetProfiler().Pop(m_commandBuffer);
    PopMarker();
}

} // namespace mengine::rhi
