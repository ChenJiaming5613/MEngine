#include "rhi/CommandBuffer.hpp"

#include "core/Assert.hpp"
#include "core/Log.hpp"
#include "rhi/GpuDevice.hpp"

namespace mengine::rhi {

namespace {

constexpr VkClearColorValue kDefaultClearColor = {{0.0f, 0.0f, 0.0f, 1.0f}};

// States whose accesses the external subpass dependency of every render pass already orders before the pass's
// attachment writes (GpuDevice::GetVulkanRenderPass: attachment stages and fragment shader reads).
constexpr ResourceStates kStatesCoveredByPassDependency =
    kResourceStateRenderTarget | kResourceStateDepthWrite | kResourceStateDepthRead | kResourceStatePixelShaderResource;

// Must match the final layouts chosen by GpuDevice::GetVulkanRenderPass.
ResourceStates AttachmentFinalState(const RenderPassOutput& output, bool depth)
{
    if (depth)
    {
        return output.sampleDepth ? kResourceStatePixelShaderResource : kResourceStateDepthWrite;
    }
    return output.presentable ? kResourceStatePresent : kResourceStatePixelShaderResource;
}

bool CoversWholeTexture(const ImageBarrier& barrier, const Texture& texture)
{
    const bool allLevels = barrier.baseMipLevel == 0
        && (barrier.mipLevelCount == VK_REMAINING_MIP_LEVELS || barrier.mipLevelCount >= texture.mipLevels);
    const bool allLayers = barrier.baseArrayLayer == 0
        && (barrier.arrayLayerCount == VK_REMAINING_ARRAY_LAYERS || barrier.arrayLayerCount >= texture.layerCount);
    return allLevels && allLayers;
}

bool Writes(ResourceStates states)
{
    return (states & kResourceStateWriteMask) != 0;
}

// Read-only to read-only with no new reader: whatever the old state made visible already serves the new one.
bool IsRedundant(ResourceStates oldState, ResourceStates newState)
{
    return !Writes(oldState) && !Writes(newState) && (newState & ~oldState) == 0;
}

} // namespace

void CommandBuffer::Init(GpuDevice* device, VkCommandBuffer commandBuffer)
{
    m_device = device;
    m_commandBuffer = commandBuffer;
    m_currentPipeline = nullptr;
    m_currentPass = {};
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
    m_currentPass = {};
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

void CommandBuffer::PrepareAttachments(const RenderPass& renderPass)
{
    if (renderPass.isSwapchain)
    {
        return;
    }

    ExecutionBarrier barrier;
    const auto prepare = [&](TextureHandle handle, bool depth) {
        const Texture* texture = m_device->AccessTexture(handle);
        if (texture == nullptr)
        {
            return;
        }
        const RenderPassOperation operation = depth ? renderPass.output.depthOperation : renderPass.output.colorOperation;
        const bool load = operation == RenderPassOperation::Load;
        const ResourceStates finalState = AttachmentFinalState(renderPass.output, depth);

        // A loading pass starts in its final layout (the layout the previous pass left the image in). A clearing pass
        // starts from UNDEFINED and accepts any layout, but still has to wait for accesses its dependency misses.
        const bool covered = (texture->state & ~kStatesCoveredByPassDependency) == 0;
        const bool layoutMatches = !load || ToVkImageLayout(texture->state) == ToVkImageLayout(finalState);
        if (covered && layoutMatches)
        {
            return;
        }
        barrier.AddImage(ImageBarrier{
            .texture = handle,
            .newState = load ? finalState : (depth ? kResourceStateDepthWrite : kResourceStateRenderTarget),
        });
    };

    for (uint32_t index = 0; index < renderPass.renderTargetCount; ++index)
    {
        prepare(renderPass.outputTextures[index], false);
    }
    if (renderPass.depthStencilTexture.IsValid())
    {
        prepare(renderPass.depthStencilTexture, true);
    }
    if (!barrier.IsEmpty())
    {
        Barrier(barrier);
    }
}

void CommandBuffer::UpdateAttachmentStates(const RenderPass& renderPass)
{
    if (renderPass.isSwapchain)
    {
        return;
    }
    for (uint32_t index = 0; index < renderPass.renderTargetCount; ++index)
    {
        if (Texture* texture = m_device->AccessTexture(renderPass.outputTextures[index]))
        {
            texture->state = AttachmentFinalState(renderPass.output, false);
        }
    }
    if (Texture* depth = m_device->AccessTexture(renderPass.depthStencilTexture))
    {
        depth->state = AttachmentFinalState(renderPass.output, true);
    }
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

    PrepareAttachments(*renderPass);

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
    m_currentPass = renderPassHandle;
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
    if (const RenderPass* renderPass = m_device->AccessRenderPass(m_currentPass))
    {
        UpdateAttachmentStates(*renderPass);
    }
    m_insidePass = false;
    m_currentPass = {};
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
    MENGINE_ASSERT(m_insidePass);
    vkCmdDraw(m_commandBuffer, vertexCount, instanceCount, firstVertex, firstInstance);
}

void CommandBuffer::DrawIndexed(uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance)
{
    MENGINE_ASSERT(m_insidePass);
    vkCmdDrawIndexed(m_commandBuffer, indexCount, instanceCount, firstIndex, vertexOffset, firstInstance);
}

const Buffer* CommandBuffer::AccessIndirectBuffer(BufferHandle handle, VkDeviceSize offset, VkDeviceSize recordSize, uint32_t drawCount,
    uint32_t stride, const char* command) const
{
    const Buffer* buffer = m_device->AccessBuffer(handle);
    if (buffer == nullptr)
    {
        MENGINE_LOG_ERROR("%s: invalid buffer handle", command);
        return nullptr;
    }
    if ((buffer->usage & VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT) == 0)
    {
        MENGINE_LOG_ERROR("%s: buffer %s lacks VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT", command, buffer->name.c_str());
        return nullptr;
    }
    if (offset % 4 != 0)
    {
        MENGINE_LOG_ERROR("%s: offset %llu is not a multiple of 4", command, static_cast<unsigned long long>(offset));
        return nullptr;
    }
    if (drawCount > 1)
    {
        if (!m_device->SupportsMultiDrawIndirect())
        {
            MENGINE_LOG_ERROR("%s: drawCount %u needs the multiDrawIndirect feature", command, drawCount);
            return nullptr;
        }
        if (stride % 4 != 0 || stride < recordSize)
        {
            MENGINE_LOG_ERROR("%s: stride %u must be a multiple of 4 and at least %llu", command, stride,
                static_cast<unsigned long long>(recordSize));
            return nullptr;
        }
    }
    const VkDeviceSize end = drawCount == 0 ? offset : offset + VkDeviceSize{drawCount - 1} * stride + recordSize;
    if (end > buffer->size)
    {
        MENGINE_LOG_ERROR("%s: records end at byte %llu of %s, which holds %llu", command, static_cast<unsigned long long>(end),
            buffer->name.c_str(), static_cast<unsigned long long>(buffer->size));
        return nullptr;
    }
    return buffer;
}

void CommandBuffer::DrawIndirect(BufferHandle bufferHandle, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MENGINE_ASSERT(m_insidePass);
    const Buffer* buffer = AccessIndirectBuffer(bufferHandle, offset, sizeof(VkDrawIndirectCommand), drawCount, stride, "DrawIndirect");
    if (buffer != nullptr)
    {
        vkCmdDrawIndirect(m_commandBuffer, buffer->buffer, offset, drawCount, stride);
    }
}

void CommandBuffer::DrawIndexedIndirect(BufferHandle bufferHandle, VkDeviceSize offset, uint32_t drawCount, uint32_t stride)
{
    MENGINE_ASSERT(m_insidePass);
    const Buffer* buffer =
        AccessIndirectBuffer(bufferHandle, offset, sizeof(VkDrawIndexedIndirectCommand), drawCount, stride, "DrawIndexedIndirect");
    if (buffer != nullptr)
    {
        vkCmdDrawIndexedIndirect(m_commandBuffer, buffer->buffer, offset, drawCount, stride);
    }
}

void CommandBuffer::Dispatch(uint32_t groupCountX, uint32_t groupCountY, uint32_t groupCountZ)
{
    if (m_insidePass || m_currentPipeline == nullptr || m_currentPipeline->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        MENGINE_LOG_ERROR("Dispatch: needs a bound compute pipeline outside render passes");
        return;
    }
    vkCmdDispatch(m_commandBuffer, groupCountX, groupCountY, groupCountZ);
}

void CommandBuffer::DispatchIndirect(BufferHandle bufferHandle, VkDeviceSize offset)
{
    if (m_insidePass || m_currentPipeline == nullptr || m_currentPipeline->bindPoint != VK_PIPELINE_BIND_POINT_COMPUTE)
    {
        MENGINE_LOG_ERROR("DispatchIndirect: needs a bound compute pipeline outside render passes");
        return;
    }
    const Buffer* buffer =
        AccessIndirectBuffer(bufferHandle, offset, sizeof(VkDispatchIndirectCommand), 1, sizeof(VkDispatchIndirectCommand), "DispatchIndirect");
    if (buffer != nullptr)
    {
        vkCmdDispatchIndirect(m_commandBuffer, buffer->buffer, offset);
    }
}

void CommandBuffer::FillBuffer(BufferHandle bufferHandle, uint32_t value, VkDeviceSize offset, VkDeviceSize size)
{
    const Buffer* buffer = m_device->AccessBuffer(bufferHandle);
    if (m_insidePass || buffer == nullptr)
    {
        MENGINE_LOG_ERROR("FillBuffer: needs a valid buffer outside render passes");
        return;
    }
    if ((buffer->usage & VK_BUFFER_USAGE_TRANSFER_DST_BIT) == 0 || offset % 4 != 0 || (size != VK_WHOLE_SIZE && size % 4 != 0)
        || offset >= buffer->size || (size != VK_WHOLE_SIZE && size > buffer->size - offset))
    {
        MENGINE_LOG_ERROR("FillBuffer: %s needs TRANSFER_DST usage and a 4-byte aligned range inside the buffer", buffer->name.c_str());
        return;
    }
    vkCmdFillBuffer(m_commandBuffer, buffer->buffer, offset, size, value);
}

void CommandBuffer::Barrier(const ExecutionBarrier& barrier)
{
    if (m_insidePass)
    {
        // Raptor ends the pass here; a barrier in the middle of a pass is almost always a recording order bug.
        MENGINE_LOG_ERROR("Barrier: not allowed inside a render pass");
        return;
    }

    std::array<VkImageMemoryBarrier, kMaxBarriersPerBatch> imageBarriers{};
    std::array<VkBufferMemoryBarrier, kMaxBarriersPerBatch> bufferBarriers{};
    VkMemoryBarrier memoryBarrier{.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER};
    uint32_t imageCount = 0;
    uint32_t bufferCount = 0;
    uint32_t memoryCount = 0;
    VkPipelineStageFlags sourceStages = 0;
    VkPipelineStageFlags destinationStages = 0;

    for (uint32_t index = 0; index < barrier.imageCount; ++index)
    {
        const ImageBarrier& entry = barrier.images[index];
        Texture* texture = m_device->AccessTexture(entry.texture);
        if (texture == nullptr)
        {
            MENGINE_LOG_ERROR("Barrier: invalid texture handle");
            continue;
        }
        const bool wholeTexture = CoversWholeTexture(entry, *texture);
        if (!wholeTexture && !entry.oldState.has_value())
        {
            MENGINE_LOG_ERROR("Barrier: a partial barrier on %s must name its old state", texture->name.c_str());
            continue;
        }
        if (entry.newState == kResourceStateUndefined)
        {
            MENGINE_LOG_ERROR("Barrier: %s cannot transition to kResourceStateUndefined", texture->name.c_str());
            continue;
        }

        const ResourceStates oldState = entry.oldState.value_or(texture->state);
        const VkImageLayout oldLayout = ToVkImageLayout(oldState);
        const VkImageLayout newLayout = ToVkImageLayout(entry.newState);
        if (oldLayout == newLayout && IsRedundant(oldState, entry.newState))
        {
            continue;
        }

        imageBarriers[imageCount++] = VkImageMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = ToVkAccessFlags(oldState),
            .dstAccessMask = ToVkAccessFlags(entry.newState),
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture->image,
            .subresourceRange = {
                .aspectMask = BarrierAspectOf(texture->format),
                .baseMipLevel = entry.baseMipLevel,
                .levelCount = entry.mipLevelCount,
                .baseArrayLayer = entry.baseArrayLayer,
                .layerCount = entry.arrayLayerCount,
            },
        };
        sourceStages |= ToVkPipelineStages(oldState);
        destinationStages |= ToVkPipelineStages(entry.newState);
        if (wholeTexture)
        {
            // A read-to-read transition in place adds readers; the earlier ones may still be reading.
            const bool addsReaders = oldLayout == newLayout && !Writes(oldState) && !Writes(entry.newState);
            texture->state = addsReaders ? oldState | entry.newState : entry.newState;
        }
    }

    for (uint32_t index = 0; index < barrier.bufferCount; ++index)
    {
        // rhi::BufferBarrier is hidden by the member function of the same name.
        const rhi::BufferBarrier& entry = barrier.buffers[index];
        const Buffer* buffer = m_device->AccessBuffer(entry.buffer);
        if (buffer == nullptr)
        {
            MENGINE_LOG_ERROR("Barrier: invalid buffer handle");
            continue;
        }
        if (IsRedundant(entry.oldState, entry.newState))
        {
            continue;
        }
        bufferBarriers[bufferCount++] = VkBufferMemoryBarrier{
            .sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER,
            .srcAccessMask = ToVkAccessFlags(entry.oldState),
            .dstAccessMask = ToVkAccessFlags(entry.newState),
            // Single queue: no ownership transfer.
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .buffer = buffer->buffer,
            .offset = entry.offset,
            .size = entry.size,
        };
        sourceStages |= ToVkPipelineStages(entry.oldState);
        destinationStages |= ToVkPipelineStages(entry.newState);
    }

    if (barrier.global.has_value() && !IsRedundant(barrier.global->oldState, barrier.global->newState))
    {
        memoryBarrier.srcAccessMask = ToVkAccessFlags(barrier.global->oldState);
        memoryBarrier.dstAccessMask = ToVkAccessFlags(barrier.global->newState);
        memoryCount = 1;
        sourceStages |= ToVkPipelineStages(barrier.global->oldState);
        destinationStages |= ToVkPipelineStages(barrier.global->newState);
    }

    if (imageCount == 0 && bufferCount == 0 && memoryCount == 0)
    {
        return;
    }
    vkCmdPipelineBarrier(m_commandBuffer, sourceStages, destinationStages, 0, memoryCount, &memoryBarrier, bufferCount,
        bufferBarriers.data(), imageCount, imageBarriers.data());
}

void CommandBuffer::TextureBarrier(TextureHandle texture, ResourceStates newState)
{
    Barrier(ExecutionBarrier{}.AddImage(texture, newState));
}

void CommandBuffer::BufferBarrier(BufferHandle buffer, ResourceStates oldState, ResourceStates newState)
{
    Barrier(ExecutionBarrier{}.AddBuffer(buffer, oldState, newState));
}

void CommandBuffer::GlobalBarrier(ResourceStates oldState, ResourceStates newState)
{
    Barrier(ExecutionBarrier{}.SetGlobal(oldState, newState));
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
