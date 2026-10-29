#pragma once

#include "rhi/VulkanUtils.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace mengine::rhi {

// Capability tiers of docs/Roadmap.md section 1. Every feature names the tier it needs; T0 always has a path.
enum class DeviceTier : uint8_t
{
    T0, // baseline: Vulkan 1.1 core, one graphics queue (target phones)
    T1, // extensions: descriptor indexing, draw indirect count, timeline semaphores, ... (newer phones, every desktop)
    T2, // desktop: dedicated compute queue, mesh shading, shading rate, sparse residency, ray tracing
};

const char* DeviceTierName(DeviceTier tier);
// "Baseline", "Extensions", "Desktop".
const char* DeviceTierDescription(DeviceTier tier);

// One requirement of a tier, with what the device has.
struct CapabilityCheck
{
    const char* name = "";
    bool met = false;
    // Details such as the missing feature bits; may be empty.
    std::string detail;
};

struct ExtensionInfo
{
    std::string name;
    uint32_t specVersion = 0;
    // Enabled on the instance / device MEngine created.
    bool enabled = false;
};

struct QueueFamilyInfo
{
    VkQueueFlags flags = 0;
    uint32_t queueCount = 0;
    uint32_t timestampValidBits = 0;
    VkExtent3D minImageTransferGranularity{};
    bool present = false; // can present to the window surface
};

struct FormatSupport
{
    VkFormat format = VK_FORMAT_UNDEFINED;
    const char* name = "";
    VkFormatFeatureFlags optimalTiling = 0;
};

// Everything MEngine wants to know about the physical device, queried once at startup. Only Vulkan 1.0 symbols are
// linked: Features2 / Properties2 are fetched with vkGetInstanceProcAddr, and every extension struct is chained only
// when the extension is listed by the device, because MEngine runs at API 1.1 and uses promoted functionality through
// its extensions. Querying enables nothing; a supported capability still has to be enabled at device creation.
struct DeviceCapabilities
{
    // -- Identity ----------------------------------------------------------------------------------------------------
    VkPhysicalDeviceProperties properties{};
    uint32_t instanceVersion = 0; // loader
    // VK_KHR_driver_properties; empty / 0 without it.
    VkDriverId driverId = static_cast<VkDriverId>(0);
    std::string driverName;
    std::string driverInfo;
    VkConformanceVersion conformanceVersion{};

    // -- Core features / memory ----------------------------------------------------------------------------------------
    VkPhysicalDeviceFeatures features{};
    VkPhysicalDeviceMemoryProperties memory{};

    // -- Queues --------------------------------------------------------------------------------------------------------
    std::vector<QueueFamilyInfo> queueFamilies;
    uint32_t graphicsQueueFamily = 0; // the one MEngine uses
    bool graphicsQueueCompute = false;
    // A family with compute but no graphics (async compute), and one with transfer only (DMA).
    bool dedicatedComputeQueue = false;
    bool dedicatedTransferQueue = false;

    // -- Vulkan 1.1 core -----------------------------------------------------------------------------------------------
    uint32_t subgroupSize = 0;
    VkShaderStageFlags subgroupStages = 0;
    VkSubgroupFeatureFlags subgroupOperations = 0;
    bool subgroupQuadOperationsInAllStages = false;
    bool multiview = false;
    uint32_t maxMultiviewViewCount = 0;
    bool shaderDrawParameters = false;
    bool storageBuffer16BitAccess = false;
    bool uniformAndStorageBuffer16BitAccess = false;
    bool samplerYcbcrConversion = false;
    uint32_t maxPerSetDescriptors = 0;
    VkDeviceSize maxMemoryAllocationSize = 0;

    // -- T1 ------------------------------------------------------------------------------------------------------------
    struct DescriptorIndexing
    {
        bool extension = false;
        bool runtimeDescriptorArray = false;
        bool partiallyBound = false;
        bool variableDescriptorCount = false;
        bool sampledImageUpdateAfterBind = false;
        bool storageBufferUpdateAfterBind = false;
        bool sampledImageArrayNonUniformIndexing = false;
        uint32_t maxUpdateAfterBindDescriptorsInAllPools = 0;
        uint32_t maxPerStageUpdateAfterBindSampledImages = 0;
        uint32_t maxSetUpdateAfterBindSampledImages = 0;
        // What bindless textures need (Raptor chapter 2).
        bool Bindless() const
        {
            return extension && runtimeDescriptorArray && partiallyBound && sampledImageUpdateAfterBind
                && sampledImageArrayNonUniformIndexing;
        }
    } descriptorIndexing;
    bool drawIndirectCount = false; // VK_KHR_draw_indirect_count or VK_AMD_draw_indirect_count
    bool timelineSemaphore = false;
    uint64_t maxTimelineSemaphoreValueDifference = 0;
    bool samplerFilterMinmax = false;
    bool filterMinmaxSingleComponentFormats = false;
    bool createRenderPass2 = false;

    // -- T2 ------------------------------------------------------------------------------------------------------------
    struct MeshShading
    {
        bool extTask = false;
        bool extMesh = false;
        bool nvExtension = false;
        uint32_t maxMeshOutputVertices = 0;
        uint32_t maxMeshOutputPrimitives = 0;
        uint32_t maxMeshWorkGroupInvocations = 0;
        uint32_t maxTaskWorkGroupInvocations = 0;
        bool Available() const { return (extTask && extMesh) || nvExtension; }
    } meshShading;
    struct FragmentShadingRate
    {
        bool pipeline = false;
        bool primitive = false;
        bool attachment = false;
        bool Available() const { return pipeline || primitive || attachment; }
    } fragmentShadingRate;
    bool accelerationStructure = false;
    bool rayTracingPipeline = false;
    bool rayQuery = false;
    bool bufferDeviceAddress = false;
    uint32_t shaderGroupHandleSize = 0;
    uint32_t maxRayRecursionDepth = 0;
    bool SparseResidency() const
    {
        return features.sparseBinding && features.sparseResidencyBuffer && features.sparseResidencyImage2D;
    }

    // -- Other extensions of interest ----------------------------------------------------------------------------------
    bool memoryBudget = false;
    bool synchronization2 = false;
    bool dynamicRendering = false; // reported only: MEngine always uses VkRenderPass
    bool shaderFloat16 = false;
    bool shaderInt8 = false;
    bool hostQueryReset = false;
    bool calibratedTimestamps = false;
    bool portabilitySubset = false;

    // -- Lists ---------------------------------------------------------------------------------------------------------
    std::vector<ExtensionInfo> instanceExtensions;
    std::vector<ExtensionInfo> deviceExtensions;
    std::vector<FormatSupport> formats;

    DeviceTier tier = DeviceTier::T0;

    bool HasDeviceExtension(std::string_view name) const;
    // Requirements of T1 / T2 over the T0 baseline (T2 also needs all of T1).
    std::vector<CapabilityCheck> TierChecks(DeviceTier tier) const;
    // T0 optional capabilities that are not guaranteed on the baseline; render paths check them.
    std::vector<CapabilityCheck> BaselineChecks() const;
};

// Queries everything above; `graphicsQueueFamily` is the family MEngine renders on, `surface` the window surface.
DeviceCapabilities QueryDeviceCapabilities(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t instanceVersion,
    uint32_t graphicsQueueFamily, VkSurfaceKHR surface);

// Marks extensions that are enabled. Names not in the list are ignored.
void MarkEnabledExtensions(std::vector<ExtensionInfo>& extensions, const std::vector<const char*>& enabled);

const char* VendorName(uint32_t vendorId);
const char* DeviceTypeName(VkPhysicalDeviceType type);
// Decodes the vendor specific driver version encoding (NVIDIA, Intel on Windows; otherwise the Vulkan encoding).
std::string DriverVersionString(uint32_t vendorId, uint32_t driverVersion);
std::string ApiVersionString(uint32_t version);

// One memory heap with the driver's current budget (VK_EXT_memory_budget) and what MEngine's allocator holds.
struct HeapUsage
{
    VkDeviceSize size = 0;
    VkMemoryHeapFlags flags = 0;
    // 0 when the device has no VK_EXT_memory_budget.
    VkDeviceSize budget = 0;
    VkDeviceSize usage = 0;
    // VMA: memory allocated from Vulkan and the part of it used by allocations.
    VkDeviceSize blockBytes = 0;
    VkDeviceSize allocationBytes = 0;
    uint32_t allocationCount = 0;
};

} // namespace mengine::rhi
