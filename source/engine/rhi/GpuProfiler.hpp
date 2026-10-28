#pragma once

#include "rhi/GpuEnums.hpp"
#include "rhi/VulkanUtils.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace mengine::rhi {

struct GpuTiming
{
    std::string name;
    uint32_t depth = 0;
    double milliseconds = 0.0;
};

// Timestamp query pool split into one slot per frame in flight, as in Raptor's GPUTimestampManager.
// A slot is read back right after its frame fence is waited on, so results never stall the CPU.
class GpuProfiler
{
public:
    void Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamily, uint32_t queriesPerFrame);
    void Shutdown();

    bool IsSupported() const { return m_queryPool != VK_NULL_HANDLE; }

    // Called at the start of a frame's command buffer, after its fence wait. Must be outside a render pass.
    void BeginFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot);

    void Push(VkCommandBuffer commandBuffer, const char* name);
    void Pop(VkCommandBuffer commandBuffer);

    // Timings of the most recently resolved frame, in push order.
    const std::vector<GpuTiming>& Timings() const { return m_resolvedTimings; }

private:
    struct Scope
    {
        std::string name;
        uint32_t depth = 0;
        uint32_t startQuery = 0;
        uint32_t endQuery = 0;
    };

    struct FrameSlot
    {
        std::vector<Scope> scopes;
        uint32_t queryCount = 0;
        bool hasResults = false;
    };

    void Resolve(uint32_t frameSlot);

    VkDevice m_device = VK_NULL_HANDLE;
    VkQueryPool m_queryPool = VK_NULL_HANDLE;
    uint32_t m_queriesPerFrame = 0;
    double m_nanosecondsPerTick = 1.0;
    uint64_t m_validMask = ~0ull;

    std::array<FrameSlot, kMaxFramesInFlight> m_slots{};
    uint32_t m_currentSlot = 0;
    std::vector<uint32_t> m_openScopes;
    std::vector<uint64_t> m_readback;
    std::vector<GpuTiming> m_resolvedTimings;
};

} // namespace mengine::rhi
