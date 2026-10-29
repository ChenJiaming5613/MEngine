#include "rhi/DeviceCapabilities.hpp"

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <utility>

namespace mengine::rhi {

namespace {

// Not in every header version (beta / newer extensions); the names are all that is needed.
constexpr const char* kPortabilitySubsetName = "VK_KHR_portability_subset";
constexpr const char* kKhrCalibratedTimestampsName = "VK_KHR_calibrated_timestamps";

std::vector<ExtensionInfo> ToExtensionInfos(const std::vector<VkExtensionProperties>& properties)
{
    std::vector<ExtensionInfo> infos;
    infos.reserve(properties.size());
    for (const VkExtensionProperties& extension : properties)
    {
        infos.push_back({.name = extension.extensionName, .specVersion = extension.specVersion});
    }
    std::sort(infos.begin(), infos.end(), [](const ExtensionInfo& a, const ExtensionInfo& b) { return a.name < b.name; });
    return infos;
}

std::vector<VkExtensionProperties> EnumerateDeviceExtensions(VkPhysicalDevice physicalDevice)
{
    uint32_t count = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &count, extensions.data());
    extensions.resize(count);
    return extensions;
}

std::vector<VkExtensionProperties> EnumerateInstanceExtensions()
{
    uint32_t count = 0;
    vkEnumerateInstanceExtensionProperties(nullptr, &count, nullptr);
    std::vector<VkExtensionProperties> extensions(count);
    vkEnumerateInstanceExtensionProperties(nullptr, &count, extensions.data());
    extensions.resize(count);
    return extensions;
}

// Prepends structs to a pNext chain.
class Chain
{
public:
    template <typename T> void Add(T& structure)
    {
        structure.pNext = m_head;
        m_head = &structure;
    }
    void* Head() const { return m_head; }

private:
    void* m_head = nullptr;
};

bool True(VkBool32 value)
{
    return value == VK_TRUE;
}

void AppendMissing(std::string& detail, bool present, const char* name)
{
    if (!present)
    {
        detail += detail.empty() ? "missing: " : ", ";
        detail += name;
    }
}

constexpr std::array<std::pair<VkFormat, const char*>, 24> kReportedFormats = {{
    {VK_FORMAT_R8G8B8A8_UNORM, "R8G8B8A8_UNORM"},
    {VK_FORMAT_R8G8B8A8_SRGB, "R8G8B8A8_SRGB"},
    {VK_FORMAT_B8G8R8A8_SRGB, "B8G8R8A8_SRGB"},
    {VK_FORMAT_A2B10G10R10_UNORM_PACK32, "A2B10G10R10_UNORM"},
    {VK_FORMAT_B10G11R11_UFLOAT_PACK32, "B10G11R11_UFLOAT"},
    {VK_FORMAT_R16G16B16A16_SFLOAT, "R16G16B16A16_SFLOAT"},
    {VK_FORMAT_R32G32B32A32_SFLOAT, "R32G32B32A32_SFLOAT"},
    {VK_FORMAT_R16G16_SFLOAT, "R16G16_SFLOAT"},
    {VK_FORMAT_R16_SFLOAT, "R16_SFLOAT"},
    {VK_FORMAT_R32_SFLOAT, "R32_SFLOAT"},
    {VK_FORMAT_R32_UINT, "R32_UINT"},
    {VK_FORMAT_R8_UNORM, "R8_UNORM"},
    {VK_FORMAT_D16_UNORM, "D16_UNORM"},
    {VK_FORMAT_D32_SFLOAT, "D32_SFLOAT"},
    {VK_FORMAT_D24_UNORM_S8_UINT, "D24_UNORM_S8_UINT"},
    {VK_FORMAT_D32_SFLOAT_S8_UINT, "D32_SFLOAT_S8_UINT"},
    {VK_FORMAT_BC1_RGBA_SRGB_BLOCK, "BC1_RGBA_SRGB"},
    {VK_FORMAT_BC3_SRGB_BLOCK, "BC3_SRGB"},
    {VK_FORMAT_BC5_UNORM_BLOCK, "BC5_UNORM"},
    {VK_FORMAT_BC7_SRGB_BLOCK, "BC7_SRGB"},
    {VK_FORMAT_ETC2_R8G8B8A8_SRGB_BLOCK, "ETC2_R8G8B8A8_SRGB"},
    {VK_FORMAT_EAC_R11G11_UNORM_BLOCK, "EAC_R11G11_UNORM"},
    {VK_FORMAT_ASTC_4x4_SRGB_BLOCK, "ASTC_4x4_SRGB"},
    {VK_FORMAT_ASTC_8x8_SRGB_BLOCK, "ASTC_8x8_SRGB"},
}};

} // namespace

// =====================================================================================================================
// Names

const char* DeviceTierName(DeviceTier tier)
{
    switch (tier)
    {
    case DeviceTier::T0: return "T0";
    case DeviceTier::T1: return "T1";
    case DeviceTier::T2: return "T2";
    }
    return "?";
}

const char* DeviceTierDescription(DeviceTier tier)
{
    switch (tier)
    {
    case DeviceTier::T0: return "Baseline";
    case DeviceTier::T1: return "Extensions";
    case DeviceTier::T2: return "Desktop";
    }
    return "?";
}

const char* VendorName(uint32_t vendorId)
{
    switch (vendorId)
    {
    case 0x1002: return "AMD";
    case 0x1010: return "ImgTec";
    case 0x10DE: return "NVIDIA";
    case 0x13B5: return "ARM";
    case 0x5143: return "Qualcomm";
    case 0x8086: return "Intel";
    case 0x106B: return "Apple";
    case 0x144D: return "Samsung";
    case 0x19E5: return "Huawei";
    case 0x1AE0: return "Google";
    case 0x10005: return "Mesa";
    default: return "Unknown";
    }
}

const char* DeviceTypeName(VkPhysicalDeviceType type)
{
    switch (type)
    {
    case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU: return "Integrated GPU";
    case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU: return "Discrete GPU";
    case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU: return "Virtual GPU";
    case VK_PHYSICAL_DEVICE_TYPE_CPU: return "CPU";
    default: return "Other";
    }
}

std::string ApiVersionString(uint32_t version)
{
    char text[32];
    std::snprintf(text, sizeof(text), "%u.%u.%u", VK_API_VERSION_MAJOR(version), VK_API_VERSION_MINOR(version),
        VK_API_VERSION_PATCH(version));
    return text;
}

std::string DriverVersionString(uint32_t vendorId, uint32_t driverVersion)
{
    char text[32];
    if (vendorId == 0x10DE)
    {
        std::snprintf(text, sizeof(text), "%u.%u.%u.%u", (driverVersion >> 22) & 0x3FFu, (driverVersion >> 14) & 0xFFu,
            (driverVersion >> 6) & 0xFFu, driverVersion & 0x3Fu);
    }
#if defined(_WIN32)
    else if (vendorId == 0x8086)
    {
        std::snprintf(text, sizeof(text), "%u.%u", driverVersion >> 14, driverVersion & 0x3FFFu);
    }
#endif
    else
    {
        return ApiVersionString(driverVersion);
    }
    return text;
}

void MarkEnabledExtensions(std::vector<ExtensionInfo>& extensions, const std::vector<const char*>& enabled)
{
    for (ExtensionInfo& extension : extensions)
    {
        extension.enabled = std::any_of(enabled.begin(), enabled.end(),
            [&](const char* name) { return name != nullptr && extension.name == name; });
    }
}

// =====================================================================================================================
// DeviceCapabilities

bool DeviceCapabilities::HasDeviceExtension(std::string_view name) const
{
    // Sorted by name in QueryDeviceCapabilities.
    const auto it = std::lower_bound(deviceExtensions.begin(), deviceExtensions.end(), name,
        [](const ExtensionInfo& extension, std::string_view value) { return std::string_view(extension.name) < value; });
    return it != deviceExtensions.end() && it->name == name;
}

std::vector<CapabilityCheck> DeviceCapabilities::TierChecks(DeviceTier target) const
{
    std::vector<CapabilityCheck> checks;
    if (target == DeviceTier::T1)
    {
        CapabilityCheck indexing{.name = "Descriptor indexing (bindless)", .met = descriptorIndexing.Bindless()};
        if (!descriptorIndexing.extension)
        {
            indexing.detail = "VK_EXT_descriptor_indexing not supported";
        }
        else
        {
            AppendMissing(indexing.detail, descriptorIndexing.runtimeDescriptorArray, "runtimeDescriptorArray");
            AppendMissing(indexing.detail, descriptorIndexing.partiallyBound, "partiallyBound");
            AppendMissing(indexing.detail, descriptorIndexing.sampledImageUpdateAfterBind, "sampledImageUpdateAfterBind");
            AppendMissing(indexing.detail, descriptorIndexing.sampledImageArrayNonUniformIndexing,
                "sampledImageArrayNonUniformIndexing");
        }
        checks.push_back(std::move(indexing));
        checks.push_back({.name = "Draw indirect count", .met = drawIndirectCount});
        checks.push_back({.name = "Timeline semaphores", .met = timelineSemaphore});
        checks.push_back({.name = "Sampler filter min/max", .met = samplerFilterMinmax});
        checks.push_back({.name = "Create render pass 2", .met = createRenderPass2});
        checks.push_back({.name = "imageCubeArray", .met = True(features.imageCubeArray)});
        checks.push_back({.name = "multiDrawIndirect", .met = True(features.multiDrawIndirect)});
    }
    else if (target == DeviceTier::T2)
    {
        checks.push_back({.name = "Dedicated compute queue", .met = dedicatedComputeQueue});
        CapabilityCheck mesh{.name = "Mesh shading (EXT / NV)", .met = meshShading.Available()};
        if (!mesh.met && (meshShading.extTask || meshShading.extMesh))
        {
            AppendMissing(mesh.detail, meshShading.extTask, "taskShader");
            AppendMissing(mesh.detail, meshShading.extMesh, "meshShader");
        }
        checks.push_back(std::move(mesh));
        checks.push_back({.name = "Fragment shading rate", .met = fragmentShadingRate.Available()});
        CapabilityCheck sparse{.name = "Sparse residency", .met = SparseResidency()};
        AppendMissing(sparse.detail, True(features.sparseBinding), "sparseBinding");
        AppendMissing(sparse.detail, True(features.sparseResidencyBuffer), "sparseResidencyBuffer");
        AppendMissing(sparse.detail, True(features.sparseResidencyImage2D), "sparseResidencyImage2D");
        checks.push_back(std::move(sparse));
        CapabilityCheck rayTracing{.name = "Ray tracing", .met = accelerationStructure && rayTracingPipeline && rayQuery};
        AppendMissing(rayTracing.detail, accelerationStructure, "accelerationStructure");
        AppendMissing(rayTracing.detail, rayTracingPipeline, "rayTracingPipeline");
        AppendMissing(rayTracing.detail, rayQuery, "rayQuery");
        checks.push_back(std::move(rayTracing));
        checks.push_back({.name = "Buffer device address", .met = bufferDeviceAddress});
    }
    return checks;
}

std::vector<CapabilityCheck> DeviceCapabilities::BaselineChecks() const
{
    std::vector<CapabilityCheck> checks;
    checks.push_back({.name = "Compute on the graphics queue", .met = graphicsQueueCompute});
    checks.push_back({.name = "multiDrawIndirect", .met = True(features.multiDrawIndirect)});
    checks.push_back({.name = "drawIndirectFirstInstance", .met = True(features.drawIndirectFirstInstance)});
    checks.push_back({.name = "imageCubeArray", .met = True(features.imageCubeArray)});
    checks.push_back({.name = "pipelineStatisticsQuery", .met = True(features.pipelineStatisticsQuery)});
    checks.push_back({.name = "samplerAnisotropy", .met = True(features.samplerAnisotropy)});
    checks.push_back({.name = "Timestamps on all queues", .met = True(properties.limits.timestampComputeAndGraphics)});
    checks.push_back({.name = "textureCompressionBC", .met = True(features.textureCompressionBC)});
    checks.push_back({.name = "textureCompressionETC2", .met = True(features.textureCompressionETC2)});
    checks.push_back({.name = "textureCompressionASTC_LDR", .met = True(features.textureCompressionASTC_LDR)});
    return checks;
}

// =====================================================================================================================
// Query

DeviceCapabilities QueryDeviceCapabilities(VkInstance instance, VkPhysicalDevice physicalDevice, uint32_t instanceVersion,
    uint32_t graphicsQueueFamily, VkSurfaceKHR surface)
{
    DeviceCapabilities caps;
    caps.instanceVersion = instanceVersion;
    caps.graphicsQueueFamily = graphicsQueueFamily;
    vkGetPhysicalDeviceProperties(physicalDevice, &caps.properties);
    vkGetPhysicalDeviceFeatures(physicalDevice, &caps.features);
    vkGetPhysicalDeviceMemoryProperties(physicalDevice, &caps.memory);
    caps.deviceExtensions = ToExtensionInfos(EnumerateDeviceExtensions(physicalDevice));
    caps.instanceExtensions = ToExtensionInfos(EnumerateInstanceExtensions());
    const auto has = [&caps](const char* name) { return caps.HasDeviceExtension(name); };

    // -- Queues --------------------------------------------------------------------------------------------------------
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());
    for (uint32_t index = 0; index < familyCount; ++index)
    {
        const VkQueueFamilyProperties& family = families[index];
        VkBool32 present = VK_FALSE;
        if (surface != VK_NULL_HANDLE)
        {
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, index, surface, &present);
        }
        caps.queueFamilies.push_back({
            .flags = family.queueFlags,
            .queueCount = family.queueCount,
            .timestampValidBits = family.timestampValidBits,
            .minImageTransferGranularity = family.minImageTransferGranularity,
            .present = present == VK_TRUE,
        });
        const bool graphics = (family.queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        const bool compute = (family.queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
        const bool transfer = (family.queueFlags & VK_QUEUE_TRANSFER_BIT) != 0;
        caps.dedicatedComputeQueue |= compute && !graphics;
        caps.dedicatedTransferQueue |= transfer && !compute && !graphics;
    }
    caps.graphicsQueueCompute = graphicsQueueFamily < familyCount
        && (families[graphicsQueueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;

    // -- Features2 / Properties2 ---------------------------------------------------------------------------------------
    // Vulkan 1.1 entry points; the Android API 24 loader stub does not export them.
    auto getFeatures2 = reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceFeatures2"));
    auto getProperties2 = reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(
        vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceProperties2"));

    // Vulkan 1.1 core.
    VkPhysicalDeviceMultiviewFeatures multiviewFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_FEATURES};
    VkPhysicalDeviceShaderDrawParametersFeatures drawParameters{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_DRAW_PARAMETERS_FEATURES};
    VkPhysicalDevice16BitStorageFeatures storage16{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_16BIT_STORAGE_FEATURES};
    VkPhysicalDeviceSamplerYcbcrConversionFeatures ycbcr{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_YCBCR_CONVERSION_FEATURES};
    VkPhysicalDeviceSubgroupProperties subgroup{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SUBGROUP_PROPERTIES};
    VkPhysicalDeviceMultiviewProperties multiviewProperties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MULTIVIEW_PROPERTIES};
    VkPhysicalDeviceMaintenance3Properties maintenance3{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MAINTENANCE_3_PROPERTIES};

    // Extensions, chained only when listed.
    VkPhysicalDeviceDescriptorIndexingFeaturesEXT indexingFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES_EXT};
    VkPhysicalDeviceDescriptorIndexingPropertiesEXT indexingProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_PROPERTIES_EXT};
    VkPhysicalDeviceTimelineSemaphoreFeaturesKHR timelineFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES_KHR};
    VkPhysicalDeviceTimelineSemaphorePropertiesKHR timelineProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_PROPERTIES_KHR};
    VkPhysicalDeviceSamplerFilterMinmaxPropertiesEXT minmaxProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SAMPLER_FILTER_MINMAX_PROPERTIES_EXT};
    VkPhysicalDeviceMeshShaderFeaturesEXT meshFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_FEATURES_EXT};
    VkPhysicalDeviceMeshShaderPropertiesEXT meshProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MESH_SHADER_PROPERTIES_EXT};
    VkPhysicalDeviceFragmentShadingRateFeaturesKHR shadingRateFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FRAGMENT_SHADING_RATE_FEATURES_KHR};
    VkPhysicalDeviceAccelerationStructureFeaturesKHR accelerationFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ACCELERATION_STRUCTURE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelineFeaturesKHR rayTracingFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_FEATURES_KHR};
    VkPhysicalDeviceRayTracingPipelinePropertiesKHR rayTracingProperties{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_TRACING_PIPELINE_PROPERTIES_KHR};
    VkPhysicalDeviceRayQueryFeaturesKHR rayQueryFeatures{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_RAY_QUERY_FEATURES_KHR};
    VkPhysicalDeviceBufferDeviceAddressFeaturesKHR addressFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_BUFFER_DEVICE_ADDRESS_FEATURES_KHR};
    VkPhysicalDeviceDriverPropertiesKHR driverProperties{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DRIVER_PROPERTIES_KHR};
    VkPhysicalDeviceSynchronization2FeaturesKHR sync2Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES_KHR};
    VkPhysicalDeviceDynamicRenderingFeaturesKHR dynamicRenderingFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DYNAMIC_RENDERING_FEATURES_KHR};
    VkPhysicalDeviceShaderFloat16Int8FeaturesKHR float16Int8Features{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SHADER_FLOAT16_INT8_FEATURES_KHR};
    VkPhysicalDeviceHostQueryResetFeaturesEXT hostQueryResetFeatures{
        .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_HOST_QUERY_RESET_FEATURES_EXT};

    const bool hasIndexing = has(VK_EXT_DESCRIPTOR_INDEXING_EXTENSION_NAME);
    const bool hasTimeline = has(VK_KHR_TIMELINE_SEMAPHORE_EXTENSION_NAME);
    const bool hasMinmax = has(VK_EXT_SAMPLER_FILTER_MINMAX_EXTENSION_NAME);
    const bool hasMesh = has(VK_EXT_MESH_SHADER_EXTENSION_NAME);
    const bool hasShadingRate = has(VK_KHR_FRAGMENT_SHADING_RATE_EXTENSION_NAME);
    const bool hasAcceleration = has(VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME);
    const bool hasRayTracing = has(VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME);
    const bool hasRayQuery = has(VK_KHR_RAY_QUERY_EXTENSION_NAME);
    const bool hasAddress = has(VK_KHR_BUFFER_DEVICE_ADDRESS_EXTENSION_NAME);
    const bool hasDriverProperties = has(VK_KHR_DRIVER_PROPERTIES_EXTENSION_NAME);
    const bool hasSync2 = has(VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME);
    const bool hasDynamicRendering = has(VK_KHR_DYNAMIC_RENDERING_EXTENSION_NAME);
    const bool hasFloat16Int8 = has(VK_KHR_SHADER_FLOAT16_INT8_EXTENSION_NAME);
    const bool hasHostQueryReset = has(VK_EXT_HOST_QUERY_RESET_EXTENSION_NAME);

    if (getFeatures2 != nullptr)
    {
        Chain chain;
        chain.Add(multiviewFeatures);
        chain.Add(drawParameters);
        chain.Add(storage16);
        chain.Add(ycbcr);
        if (hasIndexing)
        {
            chain.Add(indexingFeatures);
        }
        if (hasTimeline)
        {
            chain.Add(timelineFeatures);
        }
        if (hasMesh)
        {
            chain.Add(meshFeatures);
        }
        if (hasShadingRate)
        {
            chain.Add(shadingRateFeatures);
        }
        if (hasAcceleration)
        {
            chain.Add(accelerationFeatures);
        }
        if (hasRayTracing)
        {
            chain.Add(rayTracingFeatures);
        }
        if (hasRayQuery)
        {
            chain.Add(rayQueryFeatures);
        }
        if (hasAddress)
        {
            chain.Add(addressFeatures);
        }
        if (hasSync2)
        {
            chain.Add(sync2Features);
        }
        if (hasDynamicRendering)
        {
            chain.Add(dynamicRenderingFeatures);
        }
        if (hasFloat16Int8)
        {
            chain.Add(float16Int8Features);
        }
        if (hasHostQueryReset)
        {
            chain.Add(hostQueryResetFeatures);
        }
        VkPhysicalDeviceFeatures2 features2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2, .pNext = chain.Head()};
        getFeatures2(physicalDevice, &features2);
    }

    if (getProperties2 != nullptr)
    {
        Chain chain;
        chain.Add(subgroup);
        chain.Add(multiviewProperties);
        chain.Add(maintenance3);
        if (hasIndexing)
        {
            chain.Add(indexingProperties);
        }
        if (hasTimeline)
        {
            chain.Add(timelineProperties);
        }
        if (hasMinmax)
        {
            chain.Add(minmaxProperties);
        }
        if (hasMesh)
        {
            chain.Add(meshProperties);
        }
        if (hasRayTracing)
        {
            chain.Add(rayTracingProperties);
        }
        if (hasDriverProperties)
        {
            chain.Add(driverProperties);
        }
        VkPhysicalDeviceProperties2 properties2{.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2, .pNext = chain.Head()};
        getProperties2(physicalDevice, &properties2);
    }

    // -- Vulkan 1.1 core -----------------------------------------------------------------------------------------------
    caps.subgroupSize = subgroup.subgroupSize;
    caps.subgroupStages = subgroup.supportedStages;
    caps.subgroupOperations = subgroup.supportedOperations;
    caps.subgroupQuadOperationsInAllStages = True(subgroup.quadOperationsInAllStages);
    caps.multiview = True(multiviewFeatures.multiview);
    caps.maxMultiviewViewCount = multiviewProperties.maxMultiviewViewCount;
    caps.shaderDrawParameters = True(drawParameters.shaderDrawParameters);
    caps.storageBuffer16BitAccess = True(storage16.storageBuffer16BitAccess);
    caps.uniformAndStorageBuffer16BitAccess = True(storage16.uniformAndStorageBuffer16BitAccess);
    caps.samplerYcbcrConversion = True(ycbcr.samplerYcbcrConversion);
    caps.maxPerSetDescriptors = maintenance3.maxPerSetDescriptors;
    caps.maxMemoryAllocationSize = maintenance3.maxMemoryAllocationSize;

    // -- T1 ------------------------------------------------------------------------------------------------------------
    DeviceCapabilities::DescriptorIndexing& indexing = caps.descriptorIndexing;
    indexing.extension = hasIndexing;
    indexing.runtimeDescriptorArray = True(indexingFeatures.runtimeDescriptorArray);
    indexing.partiallyBound = True(indexingFeatures.descriptorBindingPartiallyBound);
    indexing.variableDescriptorCount = True(indexingFeatures.descriptorBindingVariableDescriptorCount);
    indexing.sampledImageUpdateAfterBind = True(indexingFeatures.descriptorBindingSampledImageUpdateAfterBind);
    indexing.storageBufferUpdateAfterBind = True(indexingFeatures.descriptorBindingStorageBufferUpdateAfterBind);
    indexing.sampledImageArrayNonUniformIndexing = True(indexingFeatures.shaderSampledImageArrayNonUniformIndexing);
    indexing.maxUpdateAfterBindDescriptorsInAllPools = indexingProperties.maxUpdateAfterBindDescriptorsInAllPools;
    indexing.maxPerStageUpdateAfterBindSampledImages = indexingProperties.maxPerStageDescriptorUpdateAfterBindSampledImages;
    indexing.maxSetUpdateAfterBindSampledImages = indexingProperties.maxDescriptorSetUpdateAfterBindSampledImages;
    caps.drawIndirectCount = has(VK_KHR_DRAW_INDIRECT_COUNT_EXTENSION_NAME) || has(VK_AMD_DRAW_INDIRECT_COUNT_EXTENSION_NAME);
    caps.timelineSemaphore = True(timelineFeatures.timelineSemaphore);
    caps.maxTimelineSemaphoreValueDifference = timelineProperties.maxTimelineSemaphoreValueDifference;
    caps.samplerFilterMinmax = hasMinmax;
    caps.filterMinmaxSingleComponentFormats = True(minmaxProperties.filterMinmaxSingleComponentFormats);
    caps.createRenderPass2 = has(VK_KHR_CREATE_RENDERPASS_2_EXTENSION_NAME);

    // -- T2 ------------------------------------------------------------------------------------------------------------
    caps.meshShading.extTask = True(meshFeatures.taskShader);
    caps.meshShading.extMesh = True(meshFeatures.meshShader);
    caps.meshShading.nvExtension = has(VK_NV_MESH_SHADER_EXTENSION_NAME);
    caps.meshShading.maxMeshOutputVertices = meshProperties.maxMeshOutputVertices;
    caps.meshShading.maxMeshOutputPrimitives = meshProperties.maxMeshOutputPrimitives;
    caps.meshShading.maxMeshWorkGroupInvocations = meshProperties.maxMeshWorkGroupInvocations;
    caps.meshShading.maxTaskWorkGroupInvocations = meshProperties.maxTaskWorkGroupInvocations;
    caps.fragmentShadingRate.pipeline = True(shadingRateFeatures.pipelineFragmentShadingRate);
    caps.fragmentShadingRate.primitive = True(shadingRateFeatures.primitiveFragmentShadingRate);
    caps.fragmentShadingRate.attachment = True(shadingRateFeatures.attachmentFragmentShadingRate);
    caps.accelerationStructure = True(accelerationFeatures.accelerationStructure);
    caps.rayTracingPipeline = True(rayTracingFeatures.rayTracingPipeline);
    caps.rayQuery = True(rayQueryFeatures.rayQuery);
    caps.bufferDeviceAddress = True(addressFeatures.bufferDeviceAddress);
    caps.shaderGroupHandleSize = rayTracingProperties.shaderGroupHandleSize;
    caps.maxRayRecursionDepth = rayTracingProperties.maxRayRecursionDepth;

    // -- Others --------------------------------------------------------------------------------------------------------
    caps.memoryBudget = has(VK_EXT_MEMORY_BUDGET_EXTENSION_NAME);
    caps.synchronization2 = True(sync2Features.synchronization2);
    caps.dynamicRendering = True(dynamicRenderingFeatures.dynamicRendering);
    caps.shaderFloat16 = True(float16Int8Features.shaderFloat16);
    caps.shaderInt8 = True(float16Int8Features.shaderInt8);
    caps.hostQueryReset = True(hostQueryResetFeatures.hostQueryReset);
    caps.calibratedTimestamps = has(VK_EXT_CALIBRATED_TIMESTAMPS_EXTENSION_NAME) || has(kKhrCalibratedTimestampsName);
    caps.portabilitySubset = has(kPortabilitySubsetName);
    if (hasDriverProperties)
    {
        caps.driverId = driverProperties.driverID;
        caps.driverName = driverProperties.driverName;
        caps.driverInfo = driverProperties.driverInfo;
        caps.conformanceVersion = driverProperties.conformanceVersion;
    }

    // -- Formats -------------------------------------------------------------------------------------------------------
    for (const auto& [format, name] : kReportedFormats)
    {
        VkFormatProperties formatProperties{};
        vkGetPhysicalDeviceFormatProperties(physicalDevice, format, &formatProperties);
        caps.formats.push_back({.format = format, .name = name, .optimalTiling = formatProperties.optimalTilingFeatures});
    }

    // -- Tier ----------------------------------------------------------------------------------------------------------
    const auto allMet = [](const std::vector<CapabilityCheck>& checks) {
        return std::all_of(checks.begin(), checks.end(), [](const CapabilityCheck& check) { return check.met; });
    };
    if (allMet(caps.TierChecks(DeviceTier::T1)))
    {
        caps.tier = allMet(caps.TierChecks(DeviceTier::T2)) ? DeviceTier::T2 : DeviceTier::T1;
    }
    return caps;
}

} // namespace mengine::rhi
