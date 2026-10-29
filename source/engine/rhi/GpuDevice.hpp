#pragma once

#include "core/Assert.hpp"
#include "core/ResourcePool.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/DeviceCapabilities.hpp"
#include "rhi/GpuProfiler.hpp"
#include "rhi/GpuResources.hpp"
#include "rhi/Swapchain.hpp"

#include <array>
#include <cstdint>
#include <functional>
#include <span>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

struct SDL_Window;

namespace mengine::rhi {

struct GpuDeviceCreation
{
    SDL_Window* window = nullptr;
    bool vsync = true;
    // Size of each frame's slice of the dynamic uniform ring.
    uint32_t dynamicBufferBytesPerFrame = 1u << 20;
    uint32_t timestampQueriesPerFrame = 64;
    // File the VkPipelineCache is loaded from and saved to; empty keeps the cache in memory only.
    std::string pipelineCachePath;
};

struct PipelineStats
{
    // Excludes ImGui's pipelines.
    uint32_t created = 0;
    double totalMilliseconds = 0.0;
    double lastMilliseconds = 0.0;
    // Bytes of cache data handed to the driver at startup; 0 when there was no usable file.
    size_t loadedCacheBytes = 0;
};

struct DynamicAllocation
{
    void* data = nullptr;
    // Dynamic offset to pass to BindDescriptorSet for the dynamic uniform buffer.
    uint32_t offset = 0;
};

// Instance, device, VMA, frame synchronization, deferred destruction and resource pools, after Raptor's GpuDevice.
// Vulkan 1.1 is required, but only 1.0 entry points are linked: the Android API 24 loader exports nothing newer.
class GpuDevice
{
public:
    GpuDevice() = default;
    ~GpuDevice() = default;
    GpuDevice(const GpuDevice&) = delete;
    GpuDevice& operator=(const GpuDevice&) = delete;

    // Throws std::runtime_error on failure.
    void Init(const GpuDeviceCreation& creation);
    void Shutdown();

    // Creation throws on Vulkan errors. Initial data is uploaded in the current upload batch, or in a one-off batch.
    BufferHandle CreateBuffer(const BufferCreation& creation);
    TextureHandle CreateTexture(const TextureCreation& creation);
    SamplerHandle CreateSampler(const SamplerCreation& creation);
    DescriptorSetLayoutHandle CreateDescriptorSetLayout(const DescriptorSetLayoutCreation& creation);
    DescriptorSetHandle CreateDescriptorSet(const DescriptorSetCreation& creation);
    PipelineHandle CreatePipeline(const PipelineCreation& creation);
    RenderPassHandle CreateRenderPass(const RenderPassCreation& creation);

    // Handles become stale at once; the Vulkan objects are destroyed after the frames that may use them complete.
    void DestroyBuffer(BufferHandle handle);
    void DestroyTexture(TextureHandle handle);
    void DestroySampler(SamplerHandle handle);
    void DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle);
    void DestroyDescriptorSet(DescriptorSetHandle handle);
    void DestroyPipeline(PipelineHandle handle);
    void DestroyRenderPass(RenderPassHandle handle);

    Buffer* AccessBuffer(BufferHandle handle) { return m_buffers.Access(handle); }
    // Makes host writes through a dynamic buffer's mapping visible to the device (no-op on coherent memory). The
    // uniform ring is flushed by EndFrame; other dynamic buffers flush what they wrote before the submit.
    void FlushBuffer(BufferHandle handle, VkDeviceSize offset, VkDeviceSize size);
    Texture* AccessTexture(TextureHandle handle) { return m_textures.Access(handle); }
    Sampler* AccessSampler(SamplerHandle handle) { return m_samplers.Access(handle); }
    DescriptorSetLayout* AccessDescriptorSetLayout(DescriptorSetLayoutHandle handle) { return m_descriptorSetLayouts.Access(handle); }
    DescriptorSet* AccessDescriptorSet(DescriptorSetHandle handle) { return m_descriptorSets.Access(handle); }
    Pipeline* AccessPipeline(PipelineHandle handle) { return m_pipelines.Access(handle); }
    RenderPass* AccessRenderPass(RenderPassHandle handle) { return m_renderPasses.Access(handle); }

    // Default sampler used by descriptor sets that do not name one.
    void LinkTextureSampler(TextureHandle texture, SamplerHandle sampler);
    // Declares the state of every subresource, after barriers on partial ranges (per-mip transitions) left them uniform.
    void SetTextureState(TextureHandle texture, ResourceStates state);
    // Rewrites every descriptor of the set under the same handle; the old VkDescriptorSet is freed later.
    void UpdateDescriptorSet(DescriptorSetHandle handle, const DescriptorSetCreation& creation);
    // Recreates the pass's output textures (same handles) and framebuffer. Descriptor sets sampling them must be updated.
    void ResizeOutputTextures(RenderPassHandle handle, uint32_t width, uint32_t height);

    // Batches staging copies and mip generation into one submission; nestable. Prefer UploadBatchScope.
    void BeginUploadBatch();
    void EndUploadBatch();

    // Waits for the frame slot, recreates the swapchain if needed and acquires an image.
    // Returns false when nothing can be rendered this iteration (minimized, out of date, surface lost).
    bool BeginFrame();
    // The frame's command buffer, already begun. Valid between BeginFrame and EndFrame.
    CommandBuffer& GetCommandBuffer() { return m_commandBuffer; }
    void EndFrame();

    // Per-frame uniform data in the shared UNIFORM_BUFFER_DYNAMIC ring. Valid between BeginFrame and EndFrame.
    DynamicAllocation DynamicAllocate(uint32_t size);
    BufferHandle GetDynamicBuffer() const { return m_dynamicBuffer; }

    // True once after the swapchain was (re)created with a possibly different extent.
    bool ConsumeSwapchainResized() { return std::exchange(m_swapchainResized, false); }

    void SetSuspended(bool suspended);
    void MarkSwapchainDirty() { m_swapchainDirty = true; }
    void SetVsync(bool enabled);
    bool IsVsync() const { return m_vsync; }
    VkPresentModeKHR GetPresentMode() const { return m_swapchain.PresentMode(); }

    RenderPassHandle GetSwapchainPass() const { return m_swapchainPass; }
    const RenderPassOutput& GetSwapchainOutput() const { return m_swapchainOutput; }
    VkExtent2D GetSwapchainExtent() const { return m_swapchain.Extent(); }
    bool IsSwapchainSrgb() const { return IsSrgbFormat(m_swapchain.SurfaceFormat().format); }

    VkFormat GetDepthFormat() const { return m_depthFormat; }
    // BC1-BC7 sampling: desktop GPUs, rare on Android.
    bool SupportsTextureCompressionBc() const { return m_textureCompressionBc; }
    bool SupportsLinearBlit(VkFormat format) const;
    // Linear filtering of sampled images; for depth formats this enables hardware 2x2 PCF with compare samplers.
    bool SupportsLinearFilter(VkFormat format) const;
    // kTextureFlagStorage needs this for the texture's format.
    bool SupportsStorageImage(VkFormat format) const;
    // The graphics queue also runs compute; compute pipelines and Dispatch need it.
    bool SupportsCompute() const { return m_computeSupported; }
    // Indirect draws with drawCount > 1.
    bool SupportsMultiDrawIndirect() const { return m_multiDrawIndirect; }
    // Indirect draw records with a non-zero firstInstance.
    bool SupportsDrawIndirectFirstInstance() const { return m_drawIndirectFirstInstance; }

    TextureHandle GetDummyTexture() const { return m_dummyTexture; }
    TextureHandle GetDummyCubeTexture() const { return m_dummyCubeTexture; }
    SamplerHandle GetDefaultSampler() const { return m_defaultSampler; }

    GpuProfiler& GetProfiler() { return m_profiler; }
    uint64_t GetAbsoluteFrame() const { return m_absoluteFrame; }
    uint32_t GetFrameSlot() const { return m_frameSlot; }
    const VkPhysicalDeviceProperties& GetProperties() const { return m_properties; }
    // What the physical device supports, queried once at Init. Supported is not enabled: GetEnabledFeatures and
    // ExtensionInfo::enabled say what the device was created with.
    const DeviceCapabilities& GetCapabilities() const { return m_capabilities; }
    const VkPhysicalDeviceFeatures& GetEnabledFeatures() const { return m_enabledFeatures; }
    // Per memory heap: the driver's budget and usage (VK_EXT_memory_budget, else 0) and MEngine's VMA statistics.
    std::vector<HeapUsage> QueryHeapUsage() const;

    // One cache shared by every pipeline, including ImGui's.
    VkPipelineCache GetPipelineCache() const { return m_pipelineCache; }
    // Writes the cache file (atomically); a no-op without a path or when the data size did not change since the last save.
    void SavePipelineCache();
    const PipelineStats& GetPipelineStats() const { return m_pipelineStats; }

    // Cached per distinct output; owned by the device.
    VkRenderPass GetVulkanRenderPass(const RenderPassOutput& output);
    VkFramebuffer GetFramebuffer(const RenderPass& renderPass) const;

    VkInstance GetInstance() const { return m_instance; }
    VkPhysicalDevice GetPhysicalDevice() const { return m_physicalDevice; }
    VkDevice GetDevice() const { return m_device; }
    VkQueue GetQueue() const { return m_queue; }
    uint32_t GetQueueFamily() const { return m_queueFamily; }
    VmaAllocator GetAllocator() const { return m_allocator; }

    void WaitIdle();
    // Outside a frame: waits for the GPU and destroys every deferred object at once, so a scene switch does not hold
    // the old and the new scene in memory together.
    void WaitIdleAndReleaseDeferred();

    // VK_EXT_debug_utils helpers; no-ops when the extension is not enabled.
    void SetResourceName(VkObjectType type, uint64_t handle, const char* name);
    void CmdBeginLabel(VkCommandBuffer commandBuffer, const char* name);
    void CmdEndLabel(VkCommandBuffer commandBuffer);

private:
    void AssertOwnerThread() const { MENGINE_ASSERT(std::this_thread::get_id() == m_ownerThread); }

    struct FrameData
    {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    struct PendingDestruction
    {
        uint64_t frame = 0;
        std::function<void()> destroy;
    };

    // A range of the staging ring, or a dedicated buffer for uploads larger than the ring. mappedData points at offset.
    struct StagingBuffer
    {
        VkBuffer buffer = VK_NULL_HANDLE;
        VmaAllocation allocation = VK_NULL_HANDLE;
        void* mappedData = nullptr;
        VkDeviceSize offset = 0;
    };

    void CreateInstance();
    void PickPhysicalDevice();
    void CreateLogicalDevice();
    void CreateAllocator();
    void CreateFrameData();
    void CreateDescriptorPool();
    void CreateDefaultResources();
    void CreatePipelineCache();
    void LoadDebugUtilsFunctions();

    bool RecreateSwapchain();
    void HandleSurfaceLost();

    void Defer(std::function<void()> destroy);
    void ProcessPendingDestructions(bool all);

    // Valid until the current upload batch is submitted.
    StagingBuffer CreateStagingBuffer(VkDeviceSize size);
    StagingBuffer AllocateStagingBuffer(VkDeviceSize size, const char* name);
    VkCommandBuffer UploadCommandBuffer() const;
    void BeginUploadCommands();
    void SubmitUploadCommands();

    void CreateTextureObjects(Texture& texture);
    void DeferTextureObjects(Texture& texture);
    void UploadTexture(Texture& texture, const void* data, size_t dataSize, uint32_t providedLevels, bool generateMips);
    void TransitionToShaderRead(Texture& texture);
    void CreateFramebuffer(RenderPass& renderPass);
    // Fill pipeline.pipeline; pipeline.layout and the stages already exist.
    void CreateComputePipeline(const VkPipelineShaderStageCreateInfo& stage, Pipeline& pipeline);
    void CreateGraphicsPipeline(const PipelineCreation& creation, std::span<const VkPipelineShaderStageCreateInfo> stages, Pipeline& pipeline);
    // Compares the reflected shader interface with the layouts, push constants and specialization constants of the
    // creation. Throws in builds with validation layers, logs an error otherwise.
    void ValidatePipelineInterface(const PipelineCreation& creation, VkShaderStageFlags pushConstantStages);
    void RecordPipelineCreation(uint64_t startTicks);
    void WriteDescriptorSet(VkDescriptorSet set, const DescriptorSetLayout& layout, const DescriptorSetCreation& creation);
    VkDescriptorSet AllocateDescriptorSet(VkDescriptorSetLayout layout);

    SDL_Window* m_window = nullptr;
    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkPhysicalDeviceProperties m_properties{};
    uint32_t m_instanceVersion = 0;
    std::vector<const char*> m_enabledInstanceExtensions;
    DeviceCapabilities m_capabilities;
    VkPhysicalDeviceFeatures m_enabledFeatures{};
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_queueFamily = 0;
    VkQueue m_queue = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    bool m_validationEnabled = false;
    bool m_debugUtilsEnabled = false;
    bool m_samplerAnisotropy = false;
    bool m_textureCompressionBc = false;
    bool m_computeSupported = false;
    bool m_multiDrawIndirect = false;
    bool m_drawIndirectFirstInstance = false;

    VkPipelineCache m_pipelineCache = VK_NULL_HANDLE;
    std::string m_pipelineCachePath;
    size_t m_lastSavedCacheSize = 0;
    PipelineStats m_pipelineStats;

    PFN_vkSetDebugUtilsObjectNameEXT m_setObjectName = nullptr;
    PFN_vkCmdBeginDebugUtilsLabelEXT m_cmdBeginLabel = nullptr;
    PFN_vkCmdEndDebugUtilsLabelEXT m_cmdEndLabel = nullptr;

    Swapchain m_swapchain;
    RenderPassOutput m_swapchainOutput;
    RenderPassHandle m_swapchainPass;
    bool m_vsync = true;
    bool m_swapchainDirty = true;
    bool m_swapchainResized = false;
    bool m_suspended = false;

    std::array<FrameData, kMaxFramesInFlight> m_frames{};
    uint32_t m_frameSlot = 0;
    uint64_t m_absoluteFrame = 0;
    uint32_t m_imageIndex = 0;
    bool m_frameActive = false;
    CommandBuffer m_commandBuffer;
    GpuProfiler m_profiler;

    BufferHandle m_dynamicBuffer;
    uint32_t m_dynamicBytesPerFrame = 0;
    uint32_t m_dynamicFrameStart = 0;
    uint32_t m_dynamicUsed = 0;

    VkCommandPool m_uploadPool = VK_NULL_HANDLE;
    VkCommandBuffer m_uploadCommandBuffer = VK_NULL_HANDLE;
    VkFence m_uploadFence = VK_NULL_HANDLE;
    uint32_t m_uploadDepth = 0;
    bool m_uploadHasBufferCopies = false;
    // Persistent and reused by every batch (each one waits for its fence), so uploads do not allocate memory.
    StagingBuffer m_stagingRing;
    VkDeviceSize m_stagingRingUsed = 0;
    std::vector<StagingBuffer> m_dedicatedStaging;
    VkDeviceSize m_dedicatedStagingBytes = 0;

    ResourcePool<Buffer, BufferHandle> m_buffers;
    ResourcePool<Texture, TextureHandle> m_textures;
    ResourcePool<Sampler, SamplerHandle> m_samplers;
    ResourcePool<DescriptorSetLayout, DescriptorSetLayoutHandle> m_descriptorSetLayouts;
    ResourcePool<DescriptorSet, DescriptorSetHandle> m_descriptorSets;
    ResourcePool<Pipeline, PipelineHandle> m_pipelines;
    ResourcePool<RenderPass, RenderPassHandle> m_renderPasses;
    std::unordered_map<RenderPassOutput, VkRenderPass, RenderPassOutputHash> m_renderPassCache;
    std::vector<PendingDestruction> m_pendingDestructions;

    // Every call that creates, destroys, uploads or submits must come from the thread that called Init; recording
    // threads only read resources while the main thread waits for them.
    std::thread::id m_ownerThread;
    TextureHandle m_dummyTexture;
    TextureHandle m_dummyCubeTexture;
    SamplerHandle m_defaultSampler;
};

// Keeps Begin/EndUploadBatch balanced. Leaving the scope by an exception still submits the batch: the commands recorded
// so far reference resources that exist (failed creations defer their destruction), and an open batch would swallow
// every later upload.
class UploadBatchScope
{
public:
    explicit UploadBatchScope(GpuDevice& device) : m_device(device) { m_device.BeginUploadBatch(); }
    ~UploadBatchScope();
    // Submits now (for the outermost scope), so submission errors reach the caller.
    void End();

    UploadBatchScope(const UploadBatchScope&) = delete;
    UploadBatchScope& operator=(const UploadBatchScope&) = delete;

private:
    GpuDevice& m_device;
    bool m_open = true;
};

} // namespace mengine::rhi
