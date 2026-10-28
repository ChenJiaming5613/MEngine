#include "rhi/GpuProfiler.hpp"

#include "core/Log.hpp"

#include <vector>

namespace mengine::rhi {

void GpuProfiler::Init(VkDevice device, VkPhysicalDevice physicalDevice, uint32_t queueFamily, uint32_t queriesPerFrame)
{
    m_device = device;
    m_queriesPerFrame = queriesPerFrame;

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice, &properties);

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    const uint32_t validBits = queueFamily < familyCount ? families[queueFamily].timestampValidBits : 0;

    if (validBits == 0 || properties.limits.timestampPeriod <= 0.0f || queriesPerFrame == 0)
    {
        MENGINE_LOG_WARN("GPU timestamps are not supported on this queue; pass timings are disabled");
        return;
    }

    m_nanosecondsPerTick = properties.limits.timestampPeriod;
    m_validMask = validBits >= 64 ? ~0ull : ((1ull << validBits) - 1ull);

    const VkQueryPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO,
        .queryType = VK_QUERY_TYPE_TIMESTAMP,
        .queryCount = queriesPerFrame * kMaxFramesInFlight,
    };
    CheckVk(vkCreateQueryPool(m_device, &poolInfo, nullptr, &m_queryPool), "vkCreateQueryPool");
    m_readback.resize(queriesPerFrame);
}

void GpuProfiler::Shutdown()
{
    if (m_queryPool != VK_NULL_HANDLE)
    {
        vkDestroyQueryPool(m_device, m_queryPool, nullptr);
        m_queryPool = VK_NULL_HANDLE;
    }
    m_slots = {};
    m_openScopes.clear();
    m_resolvedTimings.clear();
}

void GpuProfiler::BeginFrame(VkCommandBuffer commandBuffer, uint32_t frameSlot)
{
    if (!IsSupported())
    {
        return;
    }

    Resolve(frameSlot);

    m_currentSlot = frameSlot;
    FrameSlot& slot = m_slots[frameSlot];
    slot.scopes.clear();
    slot.queryCount = 0;
    slot.hasResults = false;
    m_openScopes.clear();

    // vkCmdResetQueryPool is Vulkan 1.0; the host-side vkResetQueryPool would need 1.2.
    vkCmdResetQueryPool(commandBuffer, m_queryPool, frameSlot * m_queriesPerFrame, m_queriesPerFrame);
}

void GpuProfiler::Push(VkCommandBuffer commandBuffer, const char* name)
{
    if (!IsSupported())
    {
        return;
    }
    FrameSlot& slot = m_slots[m_currentSlot];
    if (slot.queryCount + 2 > m_queriesPerFrame)
    {
        // Still track nesting so Pop stays balanced.
        m_openScopes.push_back(~0u);
        return;
    }

    const uint32_t query = slot.queryCount;
    slot.queryCount += 2;
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, m_queryPool, m_currentSlot * m_queriesPerFrame + query);

    slot.scopes.push_back(Scope{
        .name = name,
        .depth = static_cast<uint32_t>(m_openScopes.size()),
        .startQuery = query,
        .endQuery = query + 1,
    });
    m_openScopes.push_back(static_cast<uint32_t>(slot.scopes.size() - 1));
}

void GpuProfiler::Pop(VkCommandBuffer commandBuffer)
{
    if (!IsSupported() || m_openScopes.empty())
    {
        return;
    }
    const uint32_t scopeIndex = m_openScopes.back();
    m_openScopes.pop_back();
    if (scopeIndex == ~0u)
    {
        return;
    }

    const Scope& scope = m_slots[m_currentSlot].scopes[scopeIndex];
    vkCmdWriteTimestamp(commandBuffer, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, m_queryPool, m_currentSlot * m_queriesPerFrame + scope.endQuery);
    m_slots[m_currentSlot].hasResults = true;
}

void GpuProfiler::Resolve(uint32_t frameSlot)
{
    FrameSlot& slot = m_slots[frameSlot];
    // Unwritten queries would never become available, so slots that recorded nothing are skipped.
    if (!slot.hasResults || slot.queryCount == 0)
    {
        return;
    }

    const VkResult result = vkGetQueryPoolResults(m_device, m_queryPool, frameSlot * m_queriesPerFrame, slot.queryCount,
        slot.queryCount * sizeof(uint64_t), m_readback.data(), sizeof(uint64_t), VK_QUERY_RESULT_64_BIT);
    if (result != VK_SUCCESS)
    {
        return;
    }

    m_resolvedTimings.clear();
    for (const Scope& scope : slot.scopes)
    {
        const uint64_t start = m_readback[scope.startQuery] & m_validMask;
        const uint64_t end = m_readback[scope.endQuery] & m_validMask;
        const uint64_t ticks = end >= start ? end - start : 0;
        m_resolvedTimings.push_back(GpuTiming{
            .name = scope.name,
            .depth = scope.depth,
            .milliseconds = static_cast<double>(ticks) * m_nanosecondsPerTick * 1e-6,
        });
    }
}

} // namespace mengine::rhi
