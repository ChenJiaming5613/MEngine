#include "rhi/GpuDevice.hpp"

#include "core/Log.hpp"
#include "rhi/PipelineCacheFile.hpp"
#include "rhi/ShaderReflection.hpp"

#include <SDL3/SDL_timer.h>
#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>

namespace mengine::rhi {

namespace {

constexpr uint32_t kRequiredVulkanVersion = VK_API_VERSION_1_1;
[[maybe_unused]] constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";

constexpr uint32_t kBufferPoolSize = 16384;
constexpr uint32_t kTexturePoolSize = 1024;
constexpr uint32_t kSamplerPoolSize = 64;
constexpr uint32_t kDescriptorSetLayoutPoolSize = 64;
constexpr uint32_t kDescriptorSetPoolSize = 2048;
constexpr uint32_t kPipelinePoolSize = 128;
constexpr uint32_t kRenderPassPoolSize = 32;

// Sized for scenes with a few hundred materials (six combined image samplers each).
constexpr uint32_t kDescriptorsPerType = 4096;
constexpr uint32_t kMaxDescriptorSets = 2048;
constexpr uint32_t kRecreateRetryDelayMilliseconds = 16;
constexpr VkDeviceSize kStagingLevelAlignment = 16;
// An upload batch is submitted early once its staging buffers exceed this, so loading a multi-GB scene does not keep
// all of it in host visible memory at once.
constexpr VkDeviceSize kUploadStagingBudget = VkDeviceSize{256} << 20;

VKAPI_ATTR VkBool32 VKAPI_CALL OnValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
    VkDebugUtilsMessageTypeFlagsEXT, const VkDebugUtilsMessengerCallbackDataEXT* callbackData, void*)
{
    SDL_LogPriority priority = SDL_LOG_PRIORITY_INFO;
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT)
    {
        priority = SDL_LOG_PRIORITY_ERROR;
    }
    else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT)
    {
        priority = SDL_LOG_PRIORITY_WARN;
    }
    SDL_LogMessage(SDL_LOG_CATEGORY_GPU, priority, "[Validation] %s", callbackData->pMessage);
    return VK_FALSE;
}

VkDebugUtilsMessengerCreateInfoEXT MakeDebugMessengerCreateInfo()
{
    return VkDebugUtilsMessengerCreateInfoEXT{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT,
        .messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT,
        .messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT
            | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT,
        .pfnUserCallback = OnValidationMessage,
    };
}

[[maybe_unused]] bool IsInstanceLayerAvailable(std::string_view layerName)
{
    uint32_t layerCount = 0;
    vkEnumerateInstanceLayerProperties(&layerCount, nullptr);
    std::vector<VkLayerProperties> layers(layerCount);
    vkEnumerateInstanceLayerProperties(&layerCount, layers.data());
    return std::any_of(layers.begin(), layers.end(), [&](const VkLayerProperties& layer) { return layerName == layer.layerName; });
}

[[maybe_unused]] bool IsInstanceExtensionAvailable(std::string_view extensionName, const char* layerName)
{
    uint32_t extensionCount = 0;
    vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateInstanceExtensionProperties(layerName, &extensionCount, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
        [&](const VkExtensionProperties& extension) { return extensionName == extension.extensionName; });
}

bool IsDeviceExtensionAvailable(VkPhysicalDevice physicalDevice, std::string_view extensionName)
{
    uint32_t extensionCount = 0;
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, nullptr);
    std::vector<VkExtensionProperties> extensions(extensionCount);
    vkEnumerateDeviceExtensionProperties(physicalDevice, nullptr, &extensionCount, extensions.data());
    return std::any_of(extensions.begin(), extensions.end(),
        [&](const VkExtensionProperties& extension) { return extensionName == extension.extensionName; });
}

std::optional<uint32_t> FindGraphicsPresentQueueFamily(VkPhysicalDevice physicalDevice, VkSurfaceKHR surface)
{
    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

    // A family that also runs compute is preferred, so Dispatch works on the single queue.
    std::optional<uint32_t> graphicsOnly;
    for (uint32_t familyIndex = 0; familyIndex < familyCount; ++familyIndex)
    {
        VkBool32 presentSupported = VK_FALSE;
        vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, surface, &presentSupported);
        if ((families[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) == 0 || presentSupported != VK_TRUE)
        {
            continue;
        }
        if ((families[familyIndex].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0)
        {
            return familyIndex;
        }
        if (!graphicsOnly.has_value())
        {
            graphicsOnly = familyIndex;
        }
    }
    return graphicsOnly;
}

std::string StageNames(VkShaderStageFlags stages)
{
    std::string names;
    const auto append = [&](VkShaderStageFlags bit, const char* name) {
        if ((stages & bit) != 0)
        {
            names += names.empty() ? "" : "|";
            names += name;
            stages &= ~bit;
        }
    };
    append(VK_SHADER_STAGE_VERTEX_BIT, "VERTEX");
    append(VK_SHADER_STAGE_FRAGMENT_BIT, "FRAGMENT");
    append(VK_SHADER_STAGE_COMPUTE_BIT, "COMPUTE");
    append(VK_SHADER_STAGE_GEOMETRY_BIT, "GEOMETRY");
    append(VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT, "TESS_CONTROL");
    append(VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT, "TESS_EVALUATION");
    if (stages != 0)
    {
        names += names.empty() ? "" : "|";
        names += "0x" + std::to_string(stages);
    }
    return names.empty() ? "NONE" : names;
}

VkDeviceSize AlignUp(VkDeviceSize value, VkDeviceSize alignment)
{
    return alignment == 0 ? value : (value + alignment - 1) / alignment * alignment;
}

uint32_t MipExtent(uint32_t extent, uint32_t level)
{
    return std::max(extent >> level, 1u);
}

VkImageAspectFlags AspectOf(VkFormat format)
{
    if (!IsDepthFormat(format))
    {
        return VK_IMAGE_ASPECT_COLOR_BIT;
    }
    // Views used for sampling and as attachments only need the depth aspect.
    return VK_IMAGE_ASPECT_DEPTH_BIT;
}

bool IsBufferDescriptor(VkDescriptorType type)
{
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC
        || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}

// Every stage that may read a freshly uploaded resource: uploads finish before any frame uses them.
constexpr VkPipelineStageFlags kUploadConsumerStages = VK_PIPELINE_STAGE_DRAW_INDIRECT_BIT | VK_PIPELINE_STAGE_VERTEX_INPUT_BIT
    | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;

} // namespace

// =====================================================================================================================
// Initialization

void GpuDevice::Init(const GpuDeviceCreation& creation)
{
    m_window = creation.window;
    m_vsync = creation.vsync;
    m_pipelineCachePath = creation.pipelineCachePath;

    m_buffers.Init(kBufferPoolSize);
    m_textures.Init(kTexturePoolSize);
    m_samplers.Init(kSamplerPoolSize);
    m_descriptorSetLayouts.Init(kDescriptorSetLayoutPoolSize);
    m_descriptorSets.Init(kDescriptorSetPoolSize);
    m_pipelines.Init(kPipelinePoolSize);
    m_renderPasses.Init(kRenderPassPoolSize);

    CreateInstance();
    m_swapchain.Init(SwapchainContext{.window = m_window, .instance = m_instance});
    m_swapchain.CreateSurface();
    PickPhysicalDevice();
    CreateLogicalDevice();
    LoadDebugUtilsFunctions();
    CreateAllocator();
    CreatePipelineCache();
    m_swapchain.Init(SwapchainContext{
        .window = m_window,
        .instance = m_instance,
        .physicalDevice = m_physicalDevice,
        .device = m_device,
        .queueFamily = m_queueFamily,
    });
    m_swapchain.SetSurfaceFormat(m_swapchain.ChooseSurfaceFormat());

    constexpr std::array<VkFormat, 3> kDepthCandidates = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
    for (VkFormat format : kDepthCandidates)
    {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
        if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
        {
            m_depthFormat = format;
            break;
        }
    }
    if (m_depthFormat == VK_FORMAT_UNDEFINED)
    {
        throw std::runtime_error("No supported depth format");
    }

    CreateFrameData();
    CreateDescriptorPool();
    m_profiler.Init(m_device, m_physicalDevice, m_queueFamily, creation.timestampQueriesPerFrame);

    // The swapchain pass has a single color attachment and no depth: the HDR scene is resolved into it by later passes.
    m_swapchainOutput = RenderPassOutput{};
    m_swapchainOutput.AddColor(m_swapchain.SurfaceFormat().format).SetOperations(RenderPassOperation::Clear, RenderPassOperation::DontCare);
    m_swapchainOutput.presentable = true;
    m_swapchainPass = m_renderPasses.Obtain();
    RenderPass* swapchainPass = m_renderPasses.Access(m_swapchainPass);
    swapchainPass->renderPass = GetVulkanRenderPass(m_swapchainOutput);
    swapchainPass->output = m_swapchainOutput;
    swapchainPass->isSwapchain = true;
    swapchainPass->name = "Swapchain";

    m_dynamicBytesPerFrame = static_cast<uint32_t>(AlignUp(creation.dynamicBufferBytesPerFrame, m_properties.limits.minUniformBufferOffsetAlignment));
    m_dynamicBuffer = CreateBuffer(BufferCreation{
        .usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        .resourceUsage = ResourceUsage::Dynamic,
        .size = VkDeviceSize{m_dynamicBytesPerFrame} * kMaxFramesInFlight,
        .name = "DynamicUniformRing",
    });

    CreateDefaultResources();

    m_swapchainDirty = true;
    RecreateSwapchain();
}

void GpuDevice::CreateInstance()
{
    // Looked up at runtime because the Android API 24 loader stub only exports Vulkan 1.0 symbols.
    auto enumerateInstanceVersion =
        reinterpret_cast<PFN_vkEnumerateInstanceVersion>(vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
    uint32_t loaderVersion = VK_API_VERSION_1_0;
    if (enumerateInstanceVersion != nullptr)
    {
        CheckVk(enumerateInstanceVersion(&loaderVersion), "vkEnumerateInstanceVersion");
    }
    if (loaderVersion < kRequiredVulkanVersion)
    {
        throw std::runtime_error("A Vulkan 1.1 loader is required");
    }

    Uint32 sdlExtensionCount = 0;
    const char* const* sdlExtensions = SDL_Vulkan_GetInstanceExtensions(&sdlExtensionCount);
    if (sdlExtensions == nullptr)
    {
        throw std::runtime_error(std::string("SDL_Vulkan_GetInstanceExtensions failed: ") + SDL_GetError());
    }
    std::vector<const char*> extensions(sdlExtensions, sdlExtensions + sdlExtensionCount);
    std::vector<const char*> layers;

#if defined(MENGINE_ENABLE_VALIDATION_LAYERS)
    const bool debugUtilsAvailable = IsInstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, nullptr)
        || IsInstanceExtensionAvailable(VK_EXT_DEBUG_UTILS_EXTENSION_NAME, kValidationLayerName);
    if (IsInstanceLayerAvailable(kValidationLayerName) && debugUtilsAvailable)
    {
        m_validationEnabled = true;
        m_debugUtilsEnabled = true;
        layers.push_back(kValidationLayerName);
        extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
    }
    else
    {
        MENGINE_LOG_WARN("Validation layer requested but not available");
    }
#endif

    const VkApplicationInfo applicationInfo{
        .sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
        .pApplicationName = "MEngine",
        .applicationVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .pEngineName = "MEngine",
        .engineVersion = VK_MAKE_API_VERSION(0, 0, 1, 0),
        .apiVersion = kRequiredVulkanVersion,
    };

    const VkDebugUtilsMessengerCreateInfoEXT debugMessengerInfo = MakeDebugMessengerCreateInfo();

    const VkInstanceCreateInfo instanceInfo{
        .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
        .pNext = m_validationEnabled ? &debugMessengerInfo : nullptr,
        .pApplicationInfo = &applicationInfo,
        .enabledLayerCount = static_cast<uint32_t>(layers.size()),
        .ppEnabledLayerNames = layers.data(),
        .enabledExtensionCount = static_cast<uint32_t>(extensions.size()),
        .ppEnabledExtensionNames = extensions.data(),
    };
    CheckVk(vkCreateInstance(&instanceInfo, nullptr, &m_instance), "vkCreateInstance");

    if (m_validationEnabled)
    {
        auto createDebugMessenger =
            reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
        if (createDebugMessenger != nullptr)
        {
            CheckVk(createDebugMessenger(m_instance, &debugMessengerInfo, nullptr, &m_debugMessenger), "vkCreateDebugUtilsMessengerEXT");
        }
    }
}

void GpuDevice::PickPhysicalDevice()
{
    uint32_t deviceCount = 0;
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, nullptr);
    std::vector<VkPhysicalDevice> devices(deviceCount);
    vkEnumeratePhysicalDevices(m_instance, &deviceCount, devices.data());

    int bestScore = -1;
    for (VkPhysicalDevice device : devices)
    {
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(device, &properties);
        if (properties.apiVersion < kRequiredVulkanVersion || !IsDeviceExtensionAvailable(device, VK_KHR_SWAPCHAIN_EXTENSION_NAME))
        {
            continue;
        }

        const std::optional<uint32_t> queueFamily = FindGraphicsPresentQueueFamily(device, m_swapchain.Surface());
        if (!queueFamily.has_value())
        {
            continue;
        }

        int score = 0;
        if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU)
        {
            score = 2;
        }
        else if (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU)
        {
            score = 1;
        }

        if (score > bestScore)
        {
            bestScore = score;
            m_physicalDevice = device;
            m_queueFamily = *queueFamily;
        }
    }

    if (m_physicalDevice == VK_NULL_HANDLE)
    {
        throw std::runtime_error("No GPU supports Vulkan 1.1 with graphics, present and swapchain");
    }

    vkGetPhysicalDeviceProperties(m_physicalDevice, &m_properties);
    MENGINE_LOG_INFO("GPU: %s (Vulkan %u.%u.%u)", m_properties.deviceName, VK_API_VERSION_MAJOR(m_properties.apiVersion),
        VK_API_VERSION_MINOR(m_properties.apiVersion), VK_API_VERSION_PATCH(m_properties.apiVersion));
}

void GpuDevice::CreateLogicalDevice()
{
    const float queuePriority = 1.0f;
    const VkDeviceQueueCreateInfo queueInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
        .queueFamilyIndex = m_queueFamily,
        .queueCount = 1,
        .pQueuePriorities = &queuePriority,
    };

    const std::array<const char*, 1> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

    // vkGetPhysicalDeviceFeatures2 is Vulkan 1.1 and not exported on Android API 24; the 1.0 query is enough here.
    VkPhysicalDeviceFeatures supported{};
    vkGetPhysicalDeviceFeatures(m_physicalDevice, &supported);
    VkPhysicalDeviceFeatures enabled{};
    m_samplerAnisotropy = supported.samplerAnisotropy == VK_TRUE;
    enabled.samplerAnisotropy = supported.samplerAnisotropy;
    m_textureCompressionBc = supported.textureCompressionBC == VK_TRUE;
    enabled.textureCompressionBC = supported.textureCompressionBC;
    // Optional GPU-driven rendering features; CommandBuffer checks them before recording indirect draws.
    m_multiDrawIndirect = supported.multiDrawIndirect == VK_TRUE;
    enabled.multiDrawIndirect = supported.multiDrawIndirect;
    m_drawIndirectFirstInstance = supported.drawIndirectFirstInstance == VK_TRUE;
    enabled.drawIndirectFirstInstance = supported.drawIndirectFirstInstance;

    uint32_t familyCount = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, nullptr);
    std::vector<VkQueueFamilyProperties> families(familyCount);
    vkGetPhysicalDeviceQueueFamilyProperties(m_physicalDevice, &familyCount, families.data());
    m_computeSupported = (families[m_queueFamily].queueFlags & VK_QUEUE_COMPUTE_BIT) != 0;
    MENGINE_LOG_INFO("Queue family %u: compute %s, multiDrawIndirect %s, drawIndirectFirstInstance %s", m_queueFamily,
        m_computeSupported ? "yes" : "no", m_multiDrawIndirect ? "yes" : "no", m_drawIndirectFirstInstance ? "yes" : "no");

    const VkDeviceCreateInfo deviceInfo{
        .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
        .queueCreateInfoCount = 1,
        .pQueueCreateInfos = &queueInfo,
        .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
        .ppEnabledExtensionNames = deviceExtensions.data(),
        .pEnabledFeatures = &enabled,
    };
    CheckVk(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "vkCreateDevice");
    vkGetDeviceQueue(m_device, m_queueFamily, 0, &m_queue);
}

void GpuDevice::LoadDebugUtilsFunctions()
{
    if (!m_debugUtilsEnabled)
    {
        return;
    }
    m_setObjectName = reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(m_instance, "vkSetDebugUtilsObjectNameEXT"));
    m_cmdBeginLabel = reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkCmdBeginDebugUtilsLabelEXT"));
    m_cmdEndLabel = reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetInstanceProcAddr(m_instance, "vkCmdEndDebugUtilsLabelEXT"));
}

void GpuDevice::CreateAllocator()
{
    // VMA_DYNAMIC_VULKAN_FUNCTIONS: Vulkan 1.1 entry points are fetched at runtime, never linked.
    const VmaVulkanFunctions vulkanFunctions{
        .vkGetInstanceProcAddr = vkGetInstanceProcAddr,
        .vkGetDeviceProcAddr = vkGetDeviceProcAddr,
    };

    const VmaAllocatorCreateInfo allocatorInfo{
        .physicalDevice = m_physicalDevice,
        .device = m_device,
        .pVulkanFunctions = &vulkanFunctions,
        .instance = m_instance,
        .vulkanApiVersion = kRequiredVulkanVersion,
    };
    CheckVk(vmaCreateAllocator(&allocatorInfo, &m_allocator), "vmaCreateAllocator");
}

void GpuDevice::CreatePipelineCache()
{
    std::vector<uint8_t> initialData;
    if (!m_pipelineCachePath.empty())
    {
        initialData = LoadPipelineCacheBlob(m_pipelineCachePath, m_properties);
    }
    VkPipelineCacheCreateInfo cacheInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO,
        .initialDataSize = initialData.size(),
        .pInitialData = initialData.empty() ? nullptr : initialData.data(),
    };
    if (vkCreatePipelineCache(m_device, &cacheInfo, nullptr, &m_pipelineCache) != VK_SUCCESS && !initialData.empty())
    {
        MENGINE_LOG_WARN("Pipeline cache: the driver rejected %s, starting empty", m_pipelineCachePath.c_str());
        initialData.clear();
        cacheInfo.initialDataSize = 0;
        cacheInfo.pInitialData = nullptr;
        CheckVk(vkCreatePipelineCache(m_device, &cacheInfo, nullptr, &m_pipelineCache), "vkCreatePipelineCache");
    }
    if (m_pipelineCache == VK_NULL_HANDLE)
    {
        throw std::runtime_error("vkCreatePipelineCache failed");
    }
    m_pipelineStats.loadedCacheBytes = initialData.size();
    // A cache that loaded fine needs no rewrite until it grows.
    m_lastSavedCacheSize = initialData.size();
}

void GpuDevice::SavePipelineCache()
{
    if (m_pipelineCachePath.empty() || m_pipelineCache == VK_NULL_HANDLE)
    {
        return;
    }
    size_t size = 0;
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, nullptr) != VK_SUCCESS || size == 0)
    {
        return;
    }
    if (size == m_lastSavedCacheSize)
    {
        return;
    }
    std::vector<uint8_t> data(size);
    // VK_INCOMPLETE cannot happen: nothing else touches the cache in between (single threaded pipeline creation).
    if (vkGetPipelineCacheData(m_device, m_pipelineCache, &size, data.data()) != VK_SUCCESS)
    {
        MENGINE_LOG_WARN("Pipeline cache: vkGetPipelineCacheData failed, not saved");
        return;
    }
    data.resize(size);
    if (SavePipelineCacheBlob(m_pipelineCachePath, m_properties, data))
    {
        m_lastSavedCacheSize = size;
        MENGINE_LOG_INFO("Pipeline cache: saved %zu bytes to %s", size, m_pipelineCachePath.c_str());
    }
}

void GpuDevice::CreateFrameData()
{
    const VkCommandPoolCreateInfo framePoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_queueFamily,
    };
    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    const VkFenceCreateInfo signaledFenceInfo{
        .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
        .flags = VK_FENCE_CREATE_SIGNALED_BIT,
    };

    for (FrameData& frame : m_frames)
    {
        CheckVk(vkCreateCommandPool(m_device, &framePoolInfo, nullptr, &frame.commandPool), "vkCreateCommandPool");
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = frame.commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        CheckVk(vkAllocateCommandBuffers(m_device, &allocateInfo, &frame.commandBuffer), "vkAllocateCommandBuffers");
        CheckVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAvailable), "vkCreateSemaphore");
        CheckVk(vkCreateFence(m_device, &signaledFenceInfo, nullptr, &frame.inFlight), "vkCreateFence");
    }

    const VkCommandPoolCreateInfo uploadPoolInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
        .flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT,
        .queueFamilyIndex = m_queueFamily,
    };
    CheckVk(vkCreateCommandPool(m_device, &uploadPoolInfo, nullptr, &m_uploadPool), "vkCreateCommandPool");
    const VkCommandBufferAllocateInfo uploadAllocateInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
        .commandPool = m_uploadPool,
        .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
        .commandBufferCount = 1,
    };
    CheckVk(vkAllocateCommandBuffers(m_device, &uploadAllocateInfo, &m_uploadCommandBuffer), "vkAllocateCommandBuffers");
    const VkFenceCreateInfo uploadFenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
    CheckVk(vkCreateFence(m_device, &uploadFenceInfo, nullptr, &m_uploadFence), "vkCreateFence");
}

void GpuDevice::CreateDescriptorPool()
{
    constexpr std::array<VkDescriptorType, 7> kTypes = {
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
        VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC,
        VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
        VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,
        VK_DESCRIPTOR_TYPE_SAMPLER,
        VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
        VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
    };
    std::array<VkDescriptorPoolSize, kTypes.size()> sizes{};
    for (size_t index = 0; index < kTypes.size(); ++index)
    {
        sizes[index] = VkDescriptorPoolSize{.type = kTypes[index], .descriptorCount = kDescriptorsPerType};
    }
    const VkDescriptorPoolCreateInfo poolInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
        .flags = VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT,
        .maxSets = kMaxDescriptorSets,
        .poolSizeCount = static_cast<uint32_t>(sizes.size()),
        .pPoolSizes = sizes.data(),
    };
    CheckVk(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "vkCreateDescriptorPool");
}

void GpuDevice::CreateDefaultResources()
{
    m_defaultSampler = CreateSampler(SamplerCreation{.name = "DefaultSampler"});

    BeginUploadBatch();
    const std::array<uint8_t, 4> white = {255, 255, 255, 255};
    m_dummyTexture = CreateTexture(TextureCreation{
        .width = 1,
        .height = 1,
        .format = VK_FORMAT_R8G8B8A8_UNORM,
        .initialData = white.data(),
        .initialDataSize = white.size(),
        .name = "DummyTexture",
    });
    const std::array<uint16_t, 4 * 6> blackFaces{};
    m_dummyCubeTexture = CreateTexture(TextureCreation{
        .width = 1,
        .height = 1,
        .type = TextureType::TextureCube,
        .format = VK_FORMAT_R16G16B16A16_SFLOAT,
        .initialData = blackFaces.data(),
        .initialDataSize = sizeof(blackFaces),
        .name = "DummyCubeTexture",
    });
    EndUploadBatch();
}

void GpuDevice::Shutdown()
{
    if (m_device == VK_NULL_HANDLE)
    {
        if (m_instance != VK_NULL_HANDLE)
        {
            m_swapchain.DestroySurface();
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }
        return;
    }

    vkDeviceWaitIdle(m_device);

    SavePipelineCache();
    vkDestroyPipelineCache(m_device, m_pipelineCache, nullptr);
    m_pipelineCache = VK_NULL_HANDLE;

    DestroyTexture(m_dummyTexture);
    DestroyTexture(m_dummyCubeTexture);
    DestroySampler(m_defaultSampler);
    DestroyBuffer(m_dynamicBuffer);
    m_renderPasses.Release(m_swapchainPass);

    // Anything the application forgot is reported and released.
    const auto reportLeaks = [](const char* kind, uint32_t count) {
        if (count > 0)
        {
            MENGINE_LOG_WARN("GpuDevice shutdown: %u %s still alive", count, kind);
        }
    };
    reportLeaks("buffers", m_buffers.UsedCount());
    reportLeaks("textures", m_textures.UsedCount());
    reportLeaks("samplers", m_samplers.UsedCount());
    reportLeaks("descriptor set layouts", m_descriptorSetLayouts.UsedCount());
    reportLeaks("descriptor sets", m_descriptorSets.UsedCount());
    reportLeaks("pipelines", m_pipelines.UsedCount());
    reportLeaks("render passes", m_renderPasses.UsedCount());
    {
        std::vector<BufferHandle> buffers;
        m_buffers.ForEachAlive([&](BufferHandle handle, Buffer&) { buffers.push_back(handle); });
        for (BufferHandle handle : buffers) DestroyBuffer(handle);
        std::vector<TextureHandle> textures;
        m_textures.ForEachAlive([&](TextureHandle handle, Texture&) { textures.push_back(handle); });
        for (TextureHandle handle : textures) DestroyTexture(handle);
        std::vector<SamplerHandle> samplers;
        m_samplers.ForEachAlive([&](SamplerHandle handle, Sampler&) { samplers.push_back(handle); });
        for (SamplerHandle handle : samplers) DestroySampler(handle);
        std::vector<DescriptorSetHandle> sets;
        m_descriptorSets.ForEachAlive([&](DescriptorSetHandle handle, DescriptorSet&) { sets.push_back(handle); });
        for (DescriptorSetHandle handle : sets) DestroyDescriptorSet(handle);
        std::vector<DescriptorSetLayoutHandle> layouts;
        m_descriptorSetLayouts.ForEachAlive([&](DescriptorSetLayoutHandle handle, DescriptorSetLayout&) { layouts.push_back(handle); });
        for (DescriptorSetLayoutHandle handle : layouts) DestroyDescriptorSetLayout(handle);
        std::vector<PipelineHandle> pipelines;
        m_pipelines.ForEachAlive([&](PipelineHandle handle, Pipeline&) { pipelines.push_back(handle); });
        for (PipelineHandle handle : pipelines) DestroyPipeline(handle);
        std::vector<RenderPassHandle> passes;
        m_renderPasses.ForEachAlive([&](RenderPassHandle handle, RenderPass&) { passes.push_back(handle); });
        for (RenderPassHandle handle : passes) DestroyRenderPass(handle);
    }
    ProcessPendingDestructions(true);

    m_profiler.Shutdown();

    for (FrameData& frame : m_frames)
    {
        vkDestroyFence(m_device, frame.inFlight, nullptr);
        vkDestroySemaphore(m_device, frame.imageAvailable, nullptr);
        vkDestroyCommandPool(m_device, frame.commandPool, nullptr);
        frame = {};
    }
    vkDestroyFence(m_device, m_uploadFence, nullptr);
    vkDestroyCommandPool(m_device, m_uploadPool, nullptr);
    m_uploadFence = VK_NULL_HANDLE;
    m_uploadPool = VK_NULL_HANDLE;
    m_uploadCommandBuffer = VK_NULL_HANDLE;

    m_swapchain.Destroy();

    for (const auto& [hash, renderPass] : m_renderPassCache)
    {
        vkDestroyRenderPass(m_device, renderPass, nullptr);
    }
    m_renderPassCache.clear();

    vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
    m_descriptorPool = VK_NULL_HANDLE;

    vmaDestroyAllocator(m_allocator);
    m_allocator = VK_NULL_HANDLE;

    vkDestroyDevice(m_device, nullptr);
    m_device = VK_NULL_HANDLE;

    m_swapchain.DestroySurface();

    if (m_debugMessenger != VK_NULL_HANDLE)
    {
        auto destroyDebugMessenger =
            reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
        if (destroyDebugMessenger != nullptr)
        {
            destroyDebugMessenger(m_instance, m_debugMessenger, nullptr);
        }
        m_debugMessenger = VK_NULL_HANDLE;
    }

    vkDestroyInstance(m_instance, nullptr);
    m_instance = VK_NULL_HANDLE;
    m_window = nullptr;
}

void GpuDevice::WaitIdle()
{
    if (m_device != VK_NULL_HANDLE)
    {
        CheckVk(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
    }
}

void GpuDevice::WaitIdleAndReleaseDeferred()
{
    MENGINE_ASSERT(!m_frameActive);
    WaitIdle();
    ProcessPendingDestructions(true);
}

// =====================================================================================================================
// Deferred destruction

void GpuDevice::Defer(std::function<void()> destroy)
{
    m_pendingDestructions.push_back(PendingDestruction{.frame = m_absoluteFrame, .destroy = std::move(destroy)});
}

void GpuDevice::ProcessPendingDestructions(bool all)
{
    // Work recorded in frame F is complete once frame F + kMaxFramesInFlight has waited on the same slot's fence.
    auto ready = [&](const PendingDestruction& entry) { return all || entry.frame + kMaxFramesInFlight <= m_absoluteFrame; };
    for (PendingDestruction& entry : m_pendingDestructions)
    {
        if (ready(entry))
        {
            entry.destroy();
            entry.destroy = nullptr;
        }
    }
    std::erase_if(m_pendingDestructions, [](const PendingDestruction& entry) { return !entry.destroy; });
}

// =====================================================================================================================
// Uploads

void GpuDevice::BeginUploadBatch()
{
    if (m_uploadDepth++ > 0)
    {
        return;
    }
    BeginUploadCommands();
}

void GpuDevice::BeginUploadCommands()
{
    CheckVk(vkResetCommandPool(m_device, m_uploadPool, 0), "vkResetCommandPool");
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CheckVk(vkBeginCommandBuffer(m_uploadCommandBuffer, &beginInfo), "vkBeginCommandBuffer");
    m_uploadHasBufferCopies = false;
}

void GpuDevice::EndUploadBatch()
{
    if (m_uploadDepth == 0)
    {
        MENGINE_ASSERT(!"EndUploadBatch without BeginUploadBatch");
        return;
    }
    if (--m_uploadDepth > 0)
    {
        return;
    }
    SubmitUploadCommands();
}

void GpuDevice::SubmitUploadCommands()
{
    if (m_uploadHasBufferCopies)
    {
        const VkMemoryBarrier buffersReady{
            .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
            .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_INDIRECT_COMMAND_READ_BIT | VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT
                | VK_ACCESS_UNIFORM_READ_BIT | VK_ACCESS_SHADER_READ_BIT,
        };
        vkCmdPipelineBarrier(m_uploadCommandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT, kUploadConsumerStages, 0, 1, &buffersReady, 0, nullptr,
            0, nullptr);
    }

    CheckVk(vkEndCommandBuffer(m_uploadCommandBuffer), "vkEndCommandBuffer");
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .commandBufferCount = 1,
        .pCommandBuffers = &m_uploadCommandBuffer,
    };
    CheckVk(vkQueueSubmit(m_queue, 1, &submitInfo, m_uploadFence), "vkQueueSubmit");
    CheckVk(vkWaitForFences(m_device, 1, &m_uploadFence, VK_TRUE, std::numeric_limits<uint64_t>::max()), "vkWaitForFences");
    CheckVk(vkResetFences(m_device, 1, &m_uploadFence), "vkResetFences");

    for (const StagingBuffer& staging : m_uploadStaging)
    {
        vmaDestroyBuffer(m_allocator, staging.buffer, staging.allocation);
    }
    m_uploadStaging.clear();
    m_uploadStagingBytes = 0;
}

GpuDevice::StagingBuffer GpuDevice::CreateStagingBuffer(VkDeviceSize size)
{
    // Every command recorded so far is complete, so the batch can be submitted here and continued in a new one.
    if (!m_uploadStaging.empty() && m_uploadStagingBytes + size > kUploadStagingBudget)
    {
        SubmitUploadCommands();
        BeginUploadCommands();
    }

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = size,
        .usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo allocationInfo{
        .flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT,
        .usage = VMA_MEMORY_USAGE_AUTO,
    };
    StagingBuffer staging;
    VmaAllocationInfo info{};
    CheckVk(vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &staging.buffer, &staging.allocation, &info), "vmaCreateBuffer (staging)");
    staging.mappedData = info.pMappedData;
    m_uploadStaging.push_back(staging);
    m_uploadStagingBytes += size;
    return staging;
}

VkCommandBuffer GpuDevice::UploadCommandBuffer() const
{
    MENGINE_ASSERT(m_uploadDepth > 0);
    return m_uploadCommandBuffer;
}

// =====================================================================================================================
// Buffers

BufferHandle GpuDevice::CreateBuffer(const BufferCreation& creation)
{
    if (creation.size == 0)
    {
        throw std::runtime_error(std::string("CreateBuffer: zero size for ") + (creation.name ? creation.name : "<unnamed>"));
    }

    const bool dynamic = creation.resourceUsage == ResourceUsage::Dynamic;
    const bool upload = !dynamic && creation.initialData != nullptr;
    const VkBufferUsageFlags usage = creation.usage | (upload ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : 0);

    const VkBufferCreateInfo bufferInfo{
        .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
        .size = creation.size,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
    };
    const VmaAllocationCreateInfo allocationInfo{
        .flags = dynamic ? VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT : 0u,
        .usage = dynamic ? VMA_MEMORY_USAGE_AUTO : VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };

    Buffer buffer;
    VmaAllocationInfo info{};
    CheckVk(vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &buffer.buffer, &buffer.allocation, &info), "vmaCreateBuffer");
    buffer.size = creation.size;
    buffer.usage = usage;
    buffer.resourceUsage = creation.resourceUsage;
    buffer.mappedData = dynamic ? info.pMappedData : nullptr;
    buffer.name = creation.name ? creation.name : "";
    SetResourceName(VK_OBJECT_TYPE_BUFFER, reinterpret_cast<uint64_t>(buffer.buffer), creation.name);

    if (dynamic && creation.initialData != nullptr)
    {
        std::memcpy(buffer.mappedData, creation.initialData, creation.size);
        vmaFlushAllocation(m_allocator, buffer.allocation, 0, creation.size);
    }
    else if (upload)
    {
        BeginUploadBatch();
        const StagingBuffer staging = CreateStagingBuffer(creation.size);
        std::memcpy(staging.mappedData, creation.initialData, creation.size);
        vmaFlushAllocation(m_allocator, staging.allocation, 0, creation.size);
        const VkBufferCopy copy{.size = creation.size};
        vkCmdCopyBuffer(UploadCommandBuffer(), staging.buffer, buffer.buffer, 1, &copy);
        m_uploadHasBufferCopies = true;
        EndUploadBatch();
    }

    const BufferHandle handle = m_buffers.Obtain();
    Buffer* slot = m_buffers.Access(handle);
    if (slot == nullptr)
    {
        vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
        throw std::runtime_error("Buffer pool exhausted");
    }
    *slot = std::move(buffer);
    return handle;
}

void GpuDevice::DestroyBuffer(BufferHandle handle)
{
    Buffer* buffer = m_buffers.Access(handle);
    if (buffer == nullptr)
    {
        return;
    }
    Defer([allocator = m_allocator, vkBuffer = buffer->buffer, allocation = buffer->allocation] {
        vmaDestroyBuffer(allocator, vkBuffer, allocation);
    });
    m_buffers.Release(handle);
}

// =====================================================================================================================
// Textures

bool GpuDevice::SupportsLinearBlit(VkFormat format) const
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
    constexpr VkFormatFeatureFlags kRequired =
        VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    return (properties.optimalTilingFeatures & kRequired) == kRequired;
}

bool GpuDevice::SupportsLinearFilter(VkFormat format) const
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

bool GpuDevice::SupportsStorageImage(VkFormat format) const
{
    VkFormatProperties properties{};
    vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
    return (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) != 0;
}

void GpuDevice::CreateTextureObjects(Texture& texture)
{
    const bool cube = texture.type == TextureType::TextureCube;
    const bool renderTarget = (texture.flags & kTextureFlagRenderTarget) != 0;
    const bool generateMips = (texture.flags & kTextureFlagGenerateMips) != 0 && texture.mipLevels > 1;
    const bool depth = IsDepthFormat(texture.format);

    VkImageUsageFlags usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    if (generateMips)
    {
        usage |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
    }
    if (renderTarget)
    {
        usage |= depth ? VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT : VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    }
    if ((texture.flags & kTextureFlagStorage) != 0)
    {
        usage |= VK_IMAGE_USAGE_STORAGE_BIT;
    }

    const VkImageCreateInfo imageInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
        .flags = cube ? VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT : 0u,
        .imageType = VK_IMAGE_TYPE_2D,
        .format = texture.format,
        .extent = {texture.width, texture.height, 1},
        .mipLevels = texture.mipLevels,
        .arrayLayers = texture.layerCount,
        .samples = VK_SAMPLE_COUNT_1_BIT,
        .tiling = VK_IMAGE_TILING_OPTIMAL,
        .usage = usage,
        .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
    };
    const VmaAllocationCreateInfo allocationInfo{
        // Dedicated memory for attachments helps drivers that compress render targets.
        .flags = renderTarget ? VMA_ALLOCATION_CREATE_DEDICATED_MEMORY_BIT : 0u,
        .usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE,
    };
    CheckVk(vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &texture.image, &texture.allocation, nullptr), "vmaCreateImage");

    const VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
        .image = texture.image,
        .viewType = cube ? VK_IMAGE_VIEW_TYPE_CUBE : VK_IMAGE_VIEW_TYPE_2D,
        .format = texture.format,
        .subresourceRange = {
            .aspectMask = AspectOf(texture.format),
            .baseMipLevel = 0,
            .levelCount = texture.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = texture.layerCount,
        },
    };
    const VkResult viewResult = vkCreateImageView(m_device, &viewInfo, nullptr, &texture.view);
    if (viewResult != VK_SUCCESS)
    {
        vmaDestroyImage(m_allocator, texture.image, texture.allocation);
        texture.image = VK_NULL_HANDLE;
        texture.allocation = VK_NULL_HANDLE;
        CheckVk(viewResult, "vkCreateImageView");
    }

    SetResourceName(VK_OBJECT_TYPE_IMAGE, reinterpret_cast<uint64_t>(texture.image), texture.name.c_str());
    SetResourceName(VK_OBJECT_TYPE_IMAGE_VIEW, reinterpret_cast<uint64_t>(texture.view), texture.name.c_str());
}

void GpuDevice::DeferTextureObjects(Texture& texture)
{
    Defer([device = m_device, allocator = m_allocator, image = texture.image, allocation = texture.allocation, view = texture.view] {
        vkDestroyImageView(device, view, nullptr);
        vmaDestroyImage(allocator, image, allocation);
    });
    texture.image = VK_NULL_HANDLE;
    texture.allocation = VK_NULL_HANDLE;
    texture.view = VK_NULL_HANDLE;
}

TextureHandle GpuDevice::CreateTexture(const TextureCreation& creation)
{
    const std::string name = creation.name ? creation.name : "<unnamed>";
    if (creation.width == 0 || creation.height == 0 || creation.format == VK_FORMAT_UNDEFINED)
    {
        throw std::runtime_error("CreateTexture: invalid size or format for " + name);
    }
    if ((creation.flags & kTextureFlagStorage) != 0 && !SupportsStorageImage(creation.format))
    {
        throw std::runtime_error("CreateTexture: format " + std::to_string(static_cast<int>(creation.format)) + " of " + name
            + " cannot be a storage image on this GPU");
    }

    Texture texture;
    texture.format = creation.format;
    texture.width = creation.width;
    texture.height = creation.height;
    texture.type = creation.type;
    texture.layerCount = creation.type == TextureType::TextureCube ? 6u : 1u;
    texture.flags = creation.flags;
    texture.name = name;

    const uint32_t fullChain = ComputeMipCount(creation.width, creation.height);
    texture.mipLevels = creation.mipLevels == 0 ? fullChain : std::min(creation.mipLevels, fullChain);

    bool generateMips = (creation.flags & kTextureFlagGenerateMips) != 0 && texture.mipLevels > 1 && creation.initialData != nullptr;
    if (generateMips && !SupportsLinearBlit(creation.format))
    {
        MENGINE_LOG_WARN("Format %d of %s does not support linear blits; keeping level 0 only", static_cast<int>(creation.format), name.c_str());
        generateMips = false;
        texture.mipLevels = 1;
    }
    if (!generateMips)
    {
        texture.flags = static_cast<uint8_t>(texture.flags & ~kTextureFlagGenerateMips);
    }

    CreateTextureObjects(texture);

    try
    {
        if (creation.initialData != nullptr)
        {
            UploadTexture(texture, creation.initialData, creation.initialDataSize, generateMips ? 1u : texture.mipLevels, generateMips);
        }
        else if ((creation.flags & kTextureFlagRenderTarget) == 0)
        {
            TransitionToShaderRead(texture);
        }
    }
    catch (...)
    {
        vkDestroyImageView(m_device, texture.view, nullptr);
        vmaDestroyImage(m_allocator, texture.image, texture.allocation);
        throw;
    }

    const TextureHandle handle = m_textures.Obtain();
    Texture* slot = m_textures.Access(handle);
    if (slot == nullptr)
    {
        DeferTextureObjects(texture);
        throw std::runtime_error("Texture pool exhausted");
    }
    *slot = std::move(texture);
    return handle;
}

void GpuDevice::UploadTexture(Texture& texture, const void* data, size_t dataSize, uint32_t providedLevels, bool generateMips)
{
    if (FormatLevelSize(texture.format, 1, 1) == 0)
    {
        throw std::runtime_error("UploadTexture: unsupported upload format for " + texture.name);
    }

    // Source levels are tightly packed; staging levels are aligned so every copy offset is texel (or 16 byte block)
    // and 4-byte aligned.
    std::vector<VkDeviceSize> levelSizes(providedLevels);
    VkDeviceSize sourceSize = 0;
    VkDeviceSize stagingSize = 0;
    for (uint32_t level = 0; level < providedLevels; ++level)
    {
        levelSizes[level] = FormatLevelSize(texture.format, MipExtent(texture.width, level), MipExtent(texture.height, level))
            * texture.layerCount;
        sourceSize += levelSizes[level];
        stagingSize = AlignUp(stagingSize, kStagingLevelAlignment) + levelSizes[level];
    }
    if (dataSize < sourceSize)
    {
        throw std::runtime_error("UploadTexture: " + texture.name + " needs " + std::to_string(sourceSize) + " bytes, got "
            + std::to_string(dataSize));
    }

    BeginUploadBatch();
    const VkCommandBuffer commandBuffer = UploadCommandBuffer();
    const StagingBuffer staging = CreateStagingBuffer(stagingSize);

    std::vector<VkBufferImageCopy> copies;
    copies.reserve(size_t{providedLevels} * texture.layerCount);
    const auto* source = static_cast<const uint8_t*>(data);
    VkDeviceSize sourceOffset = 0;
    VkDeviceSize stagingOffset = 0;
    for (uint32_t level = 0; level < providedLevels; ++level)
    {
        stagingOffset = AlignUp(stagingOffset, kStagingLevelAlignment);
        std::memcpy(static_cast<uint8_t*>(staging.mappedData) + stagingOffset, source + sourceOffset, levelSizes[level]);
        const VkDeviceSize faceSize = levelSizes[level] / texture.layerCount;
        for (uint32_t layer = 0; layer < texture.layerCount; ++layer)
        {
            copies.push_back(VkBufferImageCopy{
                .bufferOffset = stagingOffset + faceSize * layer,
                .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = layer, .layerCount = 1},
                .imageExtent = {MipExtent(texture.width, level), MipExtent(texture.height, level), 1},
            });
        }
        sourceOffset += levelSizes[level];
        stagingOffset += levelSizes[level];
    }
    vmaFlushAllocation(m_allocator, staging.allocation, 0, stagingSize);

    const auto barrier = [&](uint32_t baseLevel, uint32_t levelCount, VkImageLayout oldLayout, VkImageLayout newLayout,
                             VkAccessFlags srcAccess, VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage) {
        const VkImageMemoryBarrier imageBarrier{
            .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
            .srcAccessMask = srcAccess,
            .dstAccessMask = dstAccess,
            .oldLayout = oldLayout,
            .newLayout = newLayout,
            .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
            .image = texture.image,
            .subresourceRange = {
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                .baseMipLevel = baseLevel,
                .levelCount = levelCount,
                .baseArrayLayer = 0,
                .layerCount = texture.layerCount,
            },
        };
        vkCmdPipelineBarrier(commandBuffer, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &imageBarrier);
    };

    barrier(0, texture.mipLevels, VK_IMAGE_LAYOUT_UNDEFINED, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0, VK_ACCESS_TRANSFER_WRITE_BIT,
        VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
    vkCmdCopyBufferToImage(commandBuffer, staging.buffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
        static_cast<uint32_t>(copies.size()), copies.data());

    if (generateMips)
    {
        for (uint32_t level = 1; level < texture.mipLevels; ++level)
        {
            barrier(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
                VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);

            const VkImageBlit blit{
                .srcSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level - 1, .baseArrayLayer = 0, .layerCount = texture.layerCount},
                .srcOffsets = {{0, 0, 0},
                    {static_cast<int32_t>(MipExtent(texture.width, level - 1)), static_cast<int32_t>(MipExtent(texture.height, level - 1)), 1}},
                .dstSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = level, .baseArrayLayer = 0, .layerCount = texture.layerCount},
                .dstOffsets = {{0, 0, 0},
                    {static_cast<int32_t>(MipExtent(texture.width, level)), static_cast<int32_t>(MipExtent(texture.height, level)), 1}},
            };
            vkCmdBlitImage(commandBuffer, texture.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, texture.image,
                VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &blit, VK_FILTER_LINEAR);

            barrier(level - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
                VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, kUploadConsumerStages);
        }
        barrier(texture.mipLevels - 1, 1, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, kUploadConsumerStages);
    }
    else
    {
        barrier(0, texture.mipLevels, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT, VK_ACCESS_SHADER_READ_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, kUploadConsumerStages);
    }
    texture.state = kResourceStateShaderResource;

    EndUploadBatch();
}

void GpuDevice::TransitionToShaderRead(Texture& texture)
{
    BeginUploadBatch();
    const VkImageMemoryBarrier imageBarrier{
        .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
        .srcAccessMask = 0,
        .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
        .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
        .image = texture.image,
        .subresourceRange = {
            .aspectMask = BarrierAspectOf(texture.format),
            .baseMipLevel = 0,
            .levelCount = texture.mipLevels,
            .baseArrayLayer = 0,
            .layerCount = texture.layerCount,
        },
    };
    vkCmdPipelineBarrier(UploadCommandBuffer(), VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, kUploadConsumerStages, 0, 0, nullptr, 0, nullptr, 1,
        &imageBarrier);
    texture.state = kResourceStateShaderResource;
    EndUploadBatch();
}

void GpuDevice::DestroyTexture(TextureHandle handle)
{
    Texture* texture = m_textures.Access(handle);
    if (texture == nullptr)
    {
        return;
    }
    DeferTextureObjects(*texture);
    m_textures.Release(handle);
}

void GpuDevice::LinkTextureSampler(TextureHandle texture, SamplerHandle sampler)
{
    if (Texture* target = m_textures.Access(texture))
    {
        target->sampler = sampler;
    }
}

void GpuDevice::SetTextureState(TextureHandle texture, ResourceStates state)
{
    if (Texture* target = m_textures.Access(texture))
    {
        target->state = state;
    }
}

// =====================================================================================================================
// Samplers

SamplerHandle GpuDevice::CreateSampler(const SamplerCreation& creation)
{
    const bool anisotropy = m_samplerAnisotropy && creation.maxAnisotropy > 1.0f;
    const VkSamplerCreateInfo samplerInfo{
        .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
        .magFilter = creation.magFilter,
        .minFilter = creation.minFilter,
        .mipmapMode = creation.mipmapMode,
        .addressModeU = creation.addressModeU,
        .addressModeV = creation.addressModeV,
        .addressModeW = creation.addressModeW,
        .anisotropyEnable = anisotropy ? VK_TRUE : VK_FALSE,
        .maxAnisotropy = anisotropy ? std::min(creation.maxAnisotropy, m_properties.limits.maxSamplerAnisotropy) : 1.0f,
        .compareEnable = creation.compareEnable ? VK_TRUE : VK_FALSE,
        .compareOp = creation.compareOp,
        .minLod = 0.0f,
        .maxLod = creation.maxLod,
        .borderColor = creation.borderColor,
    };
    Sampler sampler;
    CheckVk(vkCreateSampler(m_device, &samplerInfo, nullptr, &sampler.sampler), "vkCreateSampler");
    sampler.name = creation.name ? creation.name : "";
    SetResourceName(VK_OBJECT_TYPE_SAMPLER, reinterpret_cast<uint64_t>(sampler.sampler), creation.name);

    const SamplerHandle handle = m_samplers.Obtain();
    Sampler* slot = m_samplers.Access(handle);
    if (slot == nullptr)
    {
        vkDestroySampler(m_device, sampler.sampler, nullptr);
        throw std::runtime_error("Sampler pool exhausted");
    }
    *slot = std::move(sampler);
    return handle;
}

void GpuDevice::DestroySampler(SamplerHandle handle)
{
    Sampler* sampler = m_samplers.Access(handle);
    if (sampler == nullptr)
    {
        return;
    }
    Defer([device = m_device, vkSampler = sampler->sampler] { vkDestroySampler(device, vkSampler, nullptr); });
    m_samplers.Release(handle);
}

// =====================================================================================================================
// Descriptor sets

DescriptorSetLayoutHandle GpuDevice::CreateDescriptorSetLayout(const DescriptorSetLayoutCreation& creation)
{
    std::array<VkDescriptorSetLayoutBinding, kMaxDescriptorsPerSet> bindings{};
    for (uint32_t index = 0; index < creation.bindingCount; ++index)
    {
        const DescriptorBinding& binding = creation.bindings[index];
        bindings[index] = VkDescriptorSetLayoutBinding{
            .binding = binding.index,
            .descriptorType = binding.type,
            .descriptorCount = binding.count,
            .stageFlags = binding.stages,
        };
    }
    const VkDescriptorSetLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
        .bindingCount = creation.bindingCount,
        .pBindings = bindings.data(),
    };

    DescriptorSetLayout layout;
    CheckVk(vkCreateDescriptorSetLayout(m_device, &layoutInfo, nullptr, &layout.layout), "vkCreateDescriptorSetLayout");
    layout.bindings = creation.bindings;
    layout.bindingCount = creation.bindingCount;
    layout.setIndex = creation.setIndex;
    SetResourceName(VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT, reinterpret_cast<uint64_t>(layout.layout), creation.name);

    const DescriptorSetLayoutHandle handle = m_descriptorSetLayouts.Obtain();
    DescriptorSetLayout* slot = m_descriptorSetLayouts.Access(handle);
    if (slot == nullptr)
    {
        vkDestroyDescriptorSetLayout(m_device, layout.layout, nullptr);
        throw std::runtime_error("Descriptor set layout pool exhausted");
    }
    *slot = layout;
    return handle;
}

void GpuDevice::DestroyDescriptorSetLayout(DescriptorSetLayoutHandle handle)
{
    DescriptorSetLayout* layout = m_descriptorSetLayouts.Access(handle);
    if (layout == nullptr)
    {
        return;
    }
    Defer([device = m_device, vkLayout = layout->layout] { vkDestroyDescriptorSetLayout(device, vkLayout, nullptr); });
    m_descriptorSetLayouts.Release(handle);
}

VkDescriptorSet GpuDevice::AllocateDescriptorSet(VkDescriptorSetLayout layout)
{
    const VkDescriptorSetAllocateInfo allocateInfo{
        .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
        .descriptorPool = m_descriptorPool,
        .descriptorSetCount = 1,
        .pSetLayouts = &layout,
    };
    VkDescriptorSet set = VK_NULL_HANDLE;
    CheckVk(vkAllocateDescriptorSets(m_device, &allocateInfo, &set), "vkAllocateDescriptorSets");
    return set;
}

void GpuDevice::WriteDescriptorSet(VkDescriptorSet set, const DescriptorSetLayout& layout, const DescriptorSetCreation& creation)
{
    std::array<VkWriteDescriptorSet, kMaxDescriptorsPerSet> writes{};
    std::array<VkDescriptorBufferInfo, kMaxDescriptorsPerSet> bufferInfos{};
    std::array<VkDescriptorImageInfo, kMaxDescriptorsPerSet> imageInfos{};
    uint32_t writeCount = 0;

    for (uint32_t index = 0; index < creation.resourceCount; ++index)
    {
        const DescriptorResource& resource = creation.resources[index];
        const DescriptorBinding* binding = nullptr;
        for (uint32_t bindingIndex = 0; bindingIndex < layout.bindingCount; ++bindingIndex)
        {
            if (layout.bindings[bindingIndex].index == resource.binding)
            {
                binding = &layout.bindings[bindingIndex];
                break;
            }
        }
        if (binding == nullptr)
        {
            throw std::runtime_error("Descriptor set " + std::string(creation.name ? creation.name : "<unnamed>") + " writes binding "
                + std::to_string(resource.binding) + " that is not in its layout");
        }

        VkWriteDescriptorSet& write = writes[writeCount];
        write = VkWriteDescriptorSet{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = set,
            .dstBinding = resource.binding,
            .dstArrayElement = 0,
            .descriptorCount = 1,
            .descriptorType = binding->type,
        };

        if (IsBufferDescriptor(binding->type))
        {
            const Buffer* buffer = m_buffers.Access(resource.buffer);
            if (buffer == nullptr)
            {
                throw std::runtime_error("Descriptor set binding " + std::to_string(resource.binding) + " references an invalid buffer");
            }
            bufferInfos[writeCount] = VkDescriptorBufferInfo{.buffer = buffer->buffer, .offset = 0, .range = resource.bufferRange};
            write.pBufferInfo = &bufferInfos[writeCount];
        }
        else if (binding->type == VK_DESCRIPTOR_TYPE_STORAGE_IMAGE)
        {
            // No dummy fallback: the dummy texture is not a storage image. The image must be in kResourceStateUnorderedAccess
            // (GENERAL) when the shader runs.
            const Texture* texture = m_textures.Access(resource.texture);
            if (texture == nullptr || (texture->flags & kTextureFlagStorage) == 0)
            {
                throw std::runtime_error("Descriptor set binding " + std::to_string(resource.binding)
                    + " needs a valid texture created with kTextureFlagStorage");
            }
            imageInfos[writeCount] = VkDescriptorImageInfo{.imageView = texture->view, .imageLayout = VK_IMAGE_LAYOUT_GENERAL};
            write.pImageInfo = &imageInfos[writeCount];
        }
        else
        {
            const Texture* texture = m_textures.Access(resource.texture);
            if (texture == nullptr)
            {
                texture = m_textures.Access(m_dummyTexture);
            }
            SamplerHandle samplerHandle = resource.sampler;
            if (!m_samplers.IsAlive(samplerHandle))
            {
                samplerHandle = m_samplers.IsAlive(texture->sampler) ? texture->sampler : m_defaultSampler;
            }
            const Sampler* sampler = m_samplers.Access(samplerHandle);
            imageInfos[writeCount] = VkDescriptorImageInfo{
                .sampler = binding->type == VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE ? VK_NULL_HANDLE : sampler->sampler,
                .imageView = texture->view,
                .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            };
            write.pImageInfo = &imageInfos[writeCount];
        }
        ++writeCount;
    }

    vkUpdateDescriptorSets(m_device, writeCount, writes.data(), 0, nullptr);
}

DescriptorSetHandle GpuDevice::CreateDescriptorSet(const DescriptorSetCreation& creation)
{
    const DescriptorSetLayout* layout = m_descriptorSetLayouts.Access(creation.layout);
    if (layout == nullptr)
    {
        throw std::runtime_error("CreateDescriptorSet: invalid layout");
    }

    DescriptorSet set;
    set.set = AllocateDescriptorSet(layout->layout);
    set.layout = creation.layout;
    try
    {
        WriteDescriptorSet(set.set, *layout, creation);
    }
    catch (...)
    {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &set.set);
        throw;
    }
    SetResourceName(VK_OBJECT_TYPE_DESCRIPTOR_SET, reinterpret_cast<uint64_t>(set.set), creation.name);

    const DescriptorSetHandle handle = m_descriptorSets.Obtain();
    DescriptorSet* slot = m_descriptorSets.Access(handle);
    if (slot == nullptr)
    {
        vkFreeDescriptorSets(m_device, m_descriptorPool, 1, &set.set);
        throw std::runtime_error("Descriptor set pool exhausted");
    }
    *slot = set;
    return handle;
}

void GpuDevice::UpdateDescriptorSet(DescriptorSetHandle handle, const DescriptorSetCreation& creation)
{
    DescriptorSet* set = m_descriptorSets.Access(handle);
    const DescriptorSetLayout* layout = m_descriptorSetLayouts.Access(creation.layout);
    if (set == nullptr || layout == nullptr)
    {
        throw std::runtime_error("UpdateDescriptorSet: invalid set or layout");
    }

    // The old set may still be bound by a frame in flight, so a fresh set replaces it.
    const VkDescriptorSet replacement = AllocateDescriptorSet(layout->layout);
    WriteDescriptorSet(replacement, *layout, creation);
    Defer([device = m_device, pool = m_descriptorPool, old = set->set] { vkFreeDescriptorSets(device, pool, 1, &old); });
    set->set = replacement;
    set->layout = creation.layout;
}

void GpuDevice::DestroyDescriptorSet(DescriptorSetHandle handle)
{
    DescriptorSet* set = m_descriptorSets.Access(handle);
    if (set == nullptr)
    {
        return;
    }
    Defer([device = m_device, pool = m_descriptorPool, vkSet = set->set] { vkFreeDescriptorSets(device, pool, 1, &vkSet); });
    m_descriptorSets.Release(handle);
}

// =====================================================================================================================
// Render passes

VkRenderPass GpuDevice::GetVulkanRenderPass(const RenderPassOutput& output)
{
    const uint64_t hash = output.Hash();
    if (const auto found = m_renderPassCache.find(hash); found != m_renderPassCache.end())
    {
        return found->second;
    }

    std::array<VkAttachmentDescription, kMaxImageOutputs + 1> attachments{};
    std::array<VkAttachmentReference, kMaxImageOutputs> colorReferences{};
    uint32_t attachmentCount = 0;

    const VkImageLayout colorFinalLayout = output.presentable ? VK_IMAGE_LAYOUT_PRESENT_SRC_KHR : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    for (uint32_t index = 0; index < output.colorCount; ++index)
    {
        attachments[attachmentCount] = VkAttachmentDescription{
            .format = output.colorFormats[index],
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = ToVkLoadOp(output.colorOperation),
            .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            // Loading needs the layout the previous pass left the image in.
            .initialLayout = output.colorOperation == RenderPassOperation::Load ? colorFinalLayout : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = colorFinalLayout,
        };
        colorReferences[index] = VkAttachmentReference{.attachment = attachmentCount, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        ++attachmentCount;
    }

    VkAttachmentReference depthReference{};
    const bool hasDepth = output.depthFormat != VK_FORMAT_UNDEFINED;
    if (hasDepth)
    {
        const VkImageLayout depthFinalLayout =
            output.sampleDepth ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[attachmentCount] = VkAttachmentDescription{
            .format = output.depthFormat,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .loadOp = ToVkLoadOp(output.depthOperation),
            .storeOp = output.storeDepth || output.sampleDepth ? VK_ATTACHMENT_STORE_OP_STORE : VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
            .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
            .initialLayout = output.depthOperation == RenderPassOperation::Load ? depthFinalLayout : VK_IMAGE_LAYOUT_UNDEFINED,
            .finalLayout = depthFinalLayout,
        };
        depthReference = VkAttachmentReference{.attachment = attachmentCount, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        ++attachmentCount;
    }

    const VkSubpassDescription subpass{
        .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
        .colorAttachmentCount = output.colorCount,
        .pColorAttachments = colorReferences.data(),
        .pDepthStencilAttachment = hasDepth ? &depthReference : nullptr,
    };

    constexpr VkPipelineStageFlags kAttachmentStages = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT
        | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    constexpr VkAccessFlags kAttachmentWrites = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    const std::array<VkSubpassDependency, 2> dependencies = {{
        // Earlier attachment writes and shader reads (write-after-read on reused targets) complete before this pass
        // writes; for the swapchain pass this also chains onto the acquire semaphore wait at COLOR_ATTACHMENT_OUTPUT.
        {
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = kAttachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .dstStageMask = kAttachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = kAttachmentWrites,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        },
        // This pass's outputs become visible to later passes that sample or load them.
        {
            .srcSubpass = 0,
            .dstSubpass = VK_SUBPASS_EXTERNAL,
            .srcStageMask = kAttachmentStages,
            .dstStageMask = kAttachmentStages | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            .srcAccessMask = kAttachmentWrites,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT
                | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT,
        },
    }};

    const VkRenderPassCreateInfo renderPassInfo{
        .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
        .attachmentCount = attachmentCount,
        .pAttachments = attachments.data(),
        .subpassCount = 1,
        .pSubpasses = &subpass,
        .dependencyCount = static_cast<uint32_t>(dependencies.size()),
        .pDependencies = dependencies.data(),
    };
    VkRenderPass renderPass = VK_NULL_HANDLE;
    CheckVk(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &renderPass), "vkCreateRenderPass");
    m_renderPassCache.emplace(hash, renderPass);
    return renderPass;
}

VkFramebuffer GpuDevice::GetFramebuffer(const RenderPass& renderPass) const
{
    return renderPass.isSwapchain ? m_swapchain.Framebuffer(m_imageIndex) : renderPass.framebuffer;
}

void GpuDevice::CreateFramebuffer(RenderPass& renderPass)
{
    std::array<VkImageView, kMaxImageOutputs + 1> views{};
    uint32_t viewCount = 0;
    for (uint32_t index = 0; index < renderPass.renderTargetCount; ++index)
    {
        const Texture* texture = m_textures.Access(renderPass.outputTextures[index]);
        if (texture == nullptr)
        {
            throw std::runtime_error("Render pass " + renderPass.name + " has an invalid color output");
        }
        views[viewCount++] = texture->view;
    }
    if (renderPass.depthStencilTexture.IsValid())
    {
        const Texture* depth = m_textures.Access(renderPass.depthStencilTexture);
        if (depth == nullptr)
        {
            throw std::runtime_error("Render pass " + renderPass.name + " has an invalid depth output");
        }
        views[viewCount++] = depth->view;
    }

    const VkFramebufferCreateInfo framebufferInfo{
        .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
        .renderPass = renderPass.renderPass,
        .attachmentCount = viewCount,
        .pAttachments = views.data(),
        .width = renderPass.width,
        .height = renderPass.height,
        .layers = 1,
    };
    CheckVk(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &renderPass.framebuffer), "vkCreateFramebuffer");
    SetResourceName(VK_OBJECT_TYPE_FRAMEBUFFER, reinterpret_cast<uint64_t>(renderPass.framebuffer), renderPass.name.c_str());
}

RenderPassHandle GpuDevice::CreateRenderPass(const RenderPassCreation& creation)
{
    RenderPass renderPass;
    renderPass.name = creation.name ? creation.name : "<unnamed>";
    renderPass.outputTextures = creation.outputTextures;
    renderPass.renderTargetCount = creation.renderTargetCount;
    renderPass.depthStencilTexture = creation.depthStencilTexture;

    RenderPassOutput& output = renderPass.output;
    output.colorOperation = creation.colorOperation;
    output.depthOperation = creation.depthOperation;
    output.storeDepth = creation.storeDepth;
    output.sampleDepth = creation.sampleDepth;
    for (uint32_t index = 0; index < creation.renderTargetCount; ++index)
    {
        const Texture* texture = m_textures.Access(creation.outputTextures[index]);
        if (texture == nullptr)
        {
            throw std::runtime_error("CreateRenderPass: invalid color output for " + renderPass.name);
        }
        output.AddColor(texture->format);
        renderPass.width = texture->width;
        renderPass.height = texture->height;
    }
    if (const Texture* depth = m_textures.Access(creation.depthStencilTexture))
    {
        output.SetDepth(depth->format);
        renderPass.width = depth->width;
        renderPass.height = depth->height;
    }

    renderPass.renderPass = GetVulkanRenderPass(output);
    CreateFramebuffer(renderPass);

    const RenderPassHandle handle = m_renderPasses.Obtain();
    RenderPass* slot = m_renderPasses.Access(handle);
    if (slot == nullptr)
    {
        vkDestroyFramebuffer(m_device, renderPass.framebuffer, nullptr);
        throw std::runtime_error("Render pass pool exhausted");
    }
    *slot = std::move(renderPass);
    return handle;
}

void GpuDevice::ResizeOutputTextures(RenderPassHandle handle, uint32_t width, uint32_t height)
{
    RenderPass* renderPass = m_renderPasses.Access(handle);
    if (renderPass == nullptr || renderPass->isSwapchain || width == 0 || height == 0)
    {
        return;
    }
    if (renderPass->width == width && renderPass->height == height)
    {
        return;
    }

    const auto resize = [&](TextureHandle textureHandle) {
        Texture* texture = m_textures.Access(textureHandle);
        if (texture == nullptr)
        {
            return;
        }
        DeferTextureObjects(*texture);
        texture->width = width;
        texture->height = height;
        CreateTextureObjects(*texture);
        texture->state = kResourceStateUndefined;
    };
    for (uint32_t index = 0; index < renderPass->renderTargetCount; ++index)
    {
        resize(renderPass->outputTextures[index]);
    }
    resize(renderPass->depthStencilTexture);

    Defer([device = m_device, framebuffer = renderPass->framebuffer] { vkDestroyFramebuffer(device, framebuffer, nullptr); });
    renderPass->width = width;
    renderPass->height = height;
    CreateFramebuffer(*renderPass);
}

void GpuDevice::DestroyRenderPass(RenderPassHandle handle)
{
    RenderPass* renderPass = m_renderPasses.Access(handle);
    if (renderPass == nullptr || renderPass->isSwapchain)
    {
        return;
    }
    Defer([device = m_device, framebuffer = renderPass->framebuffer] { vkDestroyFramebuffer(device, framebuffer, nullptr); });
    m_renderPasses.Release(handle);
}

// =====================================================================================================================
// Pipelines

PipelineHandle GpuDevice::CreatePipeline(const PipelineCreation& creation)
{
    const std::string name = creation.name ? creation.name : "<unnamed>";

    // A lone compute stage makes a compute pipeline (Raptor tells them apart the same way, through its shader state).
    const ShaderStateCreation& shaders = creation.shaders;
    if (shaders.stageCount == 0)
    {
        throw std::runtime_error("CreatePipeline: no shader stages for " + name);
    }
    const bool compute = shaders.stages[0].stage == VK_SHADER_STAGE_COMPUTE_BIT;
    for (uint32_t index = 0; index < shaders.stageCount; ++index)
    {
        if ((shaders.stages[index].stage == VK_SHADER_STAGE_COMPUTE_BIT) != compute || (compute && index > 0))
        {
            throw std::runtime_error("CreatePipeline: a compute stage must be the only stage of " + name);
        }
    }
    if (compute && !m_computeSupported)
    {
        throw std::runtime_error("CreatePipeline: the graphics queue of this GPU does not run compute (" + name + ")");
    }
    const VkShaderStageFlags pushConstantStages = compute ? VK_SHADER_STAGE_COMPUTE_BIT : creation.pushConstantStages;
    ValidatePipelineInterface(creation, pushConstantStages);

    std::array<VkDescriptorSetLayout, kMaxDescriptorSetLayouts> setLayouts{};
    for (uint32_t index = 0; index < creation.descriptorSetLayoutCount; ++index)
    {
        const DescriptorSetLayout* layout = m_descriptorSetLayouts.Access(creation.descriptorSetLayouts[index]);
        if (layout == nullptr)
        {
            throw std::runtime_error("CreatePipeline: invalid descriptor set layout for " + name);
        }
        setLayouts[index] = layout->layout;
    }

    const VkPushConstantRange pushConstantRange{
        .stageFlags = pushConstantStages,
        .offset = 0,
        .size = creation.pushConstantSize,
    };
    const VkPipelineLayoutCreateInfo layoutInfo{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
        .setLayoutCount = creation.descriptorSetLayoutCount,
        .pSetLayouts = setLayouts.data(),
        .pushConstantRangeCount = creation.pushConstantSize > 0 ? 1u : 0u,
        .pPushConstantRanges = &pushConstantRange,
    };
    Pipeline pipeline;
    pipeline.name = name;
    pipeline.bindPoint = compute ? VK_PIPELINE_BIND_POINT_COMPUTE : VK_PIPELINE_BIND_POINT_GRAPHICS;
    pipeline.pushConstantStages = pushConstantStages;
    pipeline.pushConstantSize = creation.pushConstantSize;
    CheckVk(vkCreatePipelineLayout(m_device, &layoutInfo, nullptr, &pipeline.layout), "vkCreatePipelineLayout");

    // Shader modules only live for the duration of pipeline creation.
    std::array<VkShaderModule, kMaxShaderStages> modules{};
    const auto destroyModules = [&] {
        for (VkShaderModule module : modules)
        {
            if (module != VK_NULL_HANDLE)
            {
                vkDestroyShaderModule(m_device, module, nullptr);
            }
        }
    };

    try
    {
        std::array<VkSpecializationMapEntry, kMaxSpecializationConstants> mapEntries{};
        std::array<uint32_t, kMaxSpecializationConstants> specializationData{};
        for (uint32_t index = 0; index < creation.specializationConstantCount; ++index)
        {
            mapEntries[index] = VkSpecializationMapEntry{
                .constantID = creation.specializationConstants[index].id,
                .offset = static_cast<uint32_t>(index * sizeof(uint32_t)),
                .size = sizeof(uint32_t),
            };
            specializationData[index] = creation.specializationConstants[index].value;
        }
        const VkSpecializationInfo specializationInfo{
            .mapEntryCount = creation.specializationConstantCount,
            .pMapEntries = mapEntries.data(),
            .dataSize = creation.specializationConstantCount * sizeof(uint32_t),
            .pData = specializationData.data(),
        };

        std::array<VkPipelineShaderStageCreateInfo, kMaxShaderStages> stages{};
        for (uint32_t index = 0; index < creation.shaders.stageCount; ++index)
        {
            const ShaderStage& stage = creation.shaders.stages[index];
            const VkShaderModuleCreateInfo moduleInfo{
                .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
                .codeSize = stage.codeSize,
                .pCode = stage.code,
            };
            CheckVk(vkCreateShaderModule(m_device, &moduleInfo, nullptr, &modules[index]), "vkCreateShaderModule");
            // slangc always names the SPIR-V entry point "main".
            stages[index] = VkPipelineShaderStageCreateInfo{
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = stage.stage,
                .module = modules[index],
                .pName = "main",
                .pSpecializationInfo = creation.specializationConstantCount > 0 ? &specializationInfo : nullptr,
            };
        }

        if (compute)
        {
            CreateComputePipeline(stages[0], pipeline);
        }
        else
        {
            CreateGraphicsPipeline(creation, std::span(stages.data(), creation.shaders.stageCount), pipeline);
        }
    }
    catch (...)
    {
        destroyModules();
        vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
        throw;
    }
    destroyModules();

    SetResourceName(VK_OBJECT_TYPE_PIPELINE, reinterpret_cast<uint64_t>(pipeline.pipeline), creation.name);

    const PipelineHandle handle = m_pipelines.Obtain();
    Pipeline* slot = m_pipelines.Access(handle);
    if (slot == nullptr)
    {
        vkDestroyPipeline(m_device, pipeline.pipeline, nullptr);
        vkDestroyPipelineLayout(m_device, pipeline.layout, nullptr);
        throw std::runtime_error("Pipeline pool exhausted");
    }
    *slot = std::move(pipeline);
    return handle;
}

void GpuDevice::ValidatePipelineInterface(const PipelineCreation& creation, VkShaderStageFlags pushConstantStages)
{
    const std::string name = creation.name ? creation.name : "<unnamed>";
    const ShaderStateCreation& shaders = creation.shaders;

    // Malformed SPIR-V is never tolerated: the driver would reject or crash on it anyway.
    std::array<ShaderReflection, kMaxShaderStages> perStage{};
    std::string error;
    for (uint32_t index = 0; index < shaders.stageCount; ++index)
    {
        const ShaderStage& stage = shaders.stages[index];
        if (stage.code == nullptr || stage.codeSize == 0 || stage.codeSize % sizeof(uint32_t) != 0)
        {
            throw std::runtime_error("CreatePipeline " + name + ": stage " + std::to_string(index) + " has no SPIR-V words");
        }
        if (!ReflectShader({stage.code, stage.codeSize / sizeof(uint32_t)}, perStage[index], error))
        {
            throw std::runtime_error("CreatePipeline " + name + ": stage " + std::to_string(index) + ": " + error);
        }
    }
    ShaderReflection merged;
    if (!MergeReflections(std::span(perStage.data(), shaders.stageCount), merged, error))
    {
        throw std::runtime_error("CreatePipeline " + name + ": " + error);
    }

    std::string problems;
    const auto report = [&](const std::string& problem) {
        problems += "\n  ";
        problems += problem;
    };

    for (uint32_t index = 0; index < shaders.stageCount; ++index)
    {
        if (perStage[index].stages != static_cast<VkShaderStageFlags>(shaders.stages[index].stage))
        {
            report("stage " + std::to_string(index) + " is declared as " + StageNames(static_cast<VkShaderStageFlags>(shaders.stages[index].stage)) +
                " but its entry point is " + StageNames(perStage[index].stages));
        }
    }

    for (const ReflectedBinding& binding : merged.bindings)
    {
        const std::string where = "set " + std::to_string(binding.set) + " binding " + std::to_string(binding.binding) + ": ";
        if (binding.set >= creation.descriptorSetLayoutCount)
        {
            report(where + "used by the shaders, but the pipeline has only " + std::to_string(creation.descriptorSetLayoutCount) + " set layouts");
            continue;
        }
        const DescriptorSetLayout* layout = m_descriptorSetLayouts.Access(creation.descriptorSetLayouts[binding.set]);
        if (layout == nullptr)
        {
            continue;
        }
        if (layout->setIndex != binding.set)
        {
            report(where + "the layout at this position was created for set " + std::to_string(layout->setIndex));
        }
        const auto end = layout->bindings.begin() + layout->bindingCount;
        const auto found = std::find_if(layout->bindings.begin(), end, [&](const DescriptorBinding& candidate) { return candidate.index == binding.binding; });
        if (found == end)
        {
            report(where + "missing from the layout (shader expects " + DescriptorTypeName(binding.type) + ")");
            continue;
        }
        const bool typeMatches = found->type == binding.type
            || (binding.type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER && found->type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC)
            || (binding.type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER && found->type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC);
        if (!typeMatches)
        {
            report(where + "layout has " + DescriptorTypeName(found->type) + ", shader expects " + DescriptorTypeName(binding.type));
        }
        if (found->count < binding.count)
        {
            report(where + "layout has " + std::to_string(found->count) + " descriptors, shader expects " + std::to_string(binding.count));
        }
        if ((found->stages & binding.stages) != binding.stages)
        {
            report(where + "layout stages " + StageNames(found->stages) + " miss " + StageNames(binding.stages & ~found->stages) +
                " that use it");
        }
    }

    if (merged.pushConstantSize > creation.pushConstantSize)
    {
        report("push constants: shaders use " + std::to_string(merged.pushConstantSize) + " bytes, the pipeline declares " +
            std::to_string(creation.pushConstantSize));
    }
    else if (merged.pushConstantSize > 0 && (pushConstantStages & merged.pushConstantStages) != merged.pushConstantStages)
    {
        report("push constants: range stages " + StageNames(pushConstantStages) + " miss " +
            StageNames(merged.pushConstantStages & ~pushConstantStages) + " that use them");
    }

    for (uint32_t index = 0; index < creation.specializationConstantCount; ++index)
    {
        const uint32_t id = creation.specializationConstants[index].id;
        if (!std::binary_search(merged.specializationConstantIds.begin(), merged.specializationConstantIds.end(), id))
        {
            MENGINE_LOG_WARN("CreatePipeline %s: specialization constant %u is not declared by any stage", name.c_str(), id);
        }
    }

    if (problems.empty())
    {
        return;
    }
    const std::string message = "CreatePipeline " + name + ": shader interface mismatch" + problems;
#ifdef MENGINE_ENABLE_VALIDATION_LAYERS
    throw std::runtime_error(message);
#else
    MENGINE_LOG_ERROR("%s", message.c_str());
#endif
}

void GpuDevice::RecordPipelineCreation(uint64_t startTicks)
{
    const double milliseconds =
        static_cast<double>(SDL_GetPerformanceCounter() - startTicks) * 1000.0 / static_cast<double>(SDL_GetPerformanceFrequency());
    ++m_pipelineStats.created;
    m_pipelineStats.totalMilliseconds += milliseconds;
    m_pipelineStats.lastMilliseconds = milliseconds;
}

void GpuDevice::CreateComputePipeline(const VkPipelineShaderStageCreateInfo& stage, Pipeline& pipeline)
{
    const VkComputePipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO,
        .stage = stage,
        .layout = pipeline.layout,
    };
    const uint64_t start = SDL_GetPerformanceCounter();
    CheckVk(vkCreateComputePipelines(m_device, m_pipelineCache, 1, &pipelineInfo, nullptr, &pipeline.pipeline), "vkCreateComputePipelines");
    RecordPipelineCreation(start);
}

void GpuDevice::CreateGraphicsPipeline(
    const PipelineCreation& creation, std::span<const VkPipelineShaderStageCreateInfo> stages, Pipeline& pipeline)
{
    std::array<VkVertexInputBindingDescription, kMaxVertexStreams> vertexBindings{};
    for (uint32_t index = 0; index < creation.vertexInput.streamCount; ++index)
    {
        const VertexStream& stream = creation.vertexInput.streams[index];
        vertexBindings[index] = VkVertexInputBindingDescription{.binding = stream.binding, .stride = stream.stride, .inputRate = stream.inputRate};
    }
    std::array<VkVertexInputAttributeDescription, kMaxVertexAttributes> vertexAttributes{};
    for (uint32_t index = 0; index < creation.vertexInput.attributeCount; ++index)
    {
        const VertexAttribute& attribute = creation.vertexInput.attributes[index];
        vertexAttributes[index] = VkVertexInputAttributeDescription{
            .location = attribute.location,
            .binding = attribute.binding,
            .format = attribute.format,
            .offset = attribute.offset,
        };
    }
    const VkPipelineVertexInputStateCreateInfo vertexInputState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
        .vertexBindingDescriptionCount = creation.vertexInput.streamCount,
        .pVertexBindingDescriptions = vertexBindings.data(),
        .vertexAttributeDescriptionCount = creation.vertexInput.attributeCount,
        .pVertexAttributeDescriptions = vertexAttributes.data(),
    };

    const VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
        .topology = creation.topology,
    };
    const VkPipelineViewportStateCreateInfo viewportState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
        .viewportCount = 1,
        .scissorCount = 1,
    };
    const VkPipelineRasterizationStateCreateInfo rasterizationState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
        .polygonMode = creation.rasterization.polygonMode,
        .cullMode = creation.rasterization.cullMode,
        .frontFace = creation.rasterization.frontFace,
        .depthBiasEnable = creation.rasterization.depthBiasEnable ? VK_TRUE : VK_FALSE,
        .depthBiasConstantFactor = creation.rasterization.depthBiasConstantFactor,
        .depthBiasClamp = 0.0f,
        .depthBiasSlopeFactor = creation.rasterization.depthBiasSlopeFactor,
        .lineWidth = 1.0f,
    };
    const VkPipelineMultisampleStateCreateInfo multisampleState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
        .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
    };
    const VkPipelineDepthStencilStateCreateInfo depthStencilState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
        .depthTestEnable = creation.depthStencil.depthTestEnable ? VK_TRUE : VK_FALSE,
        .depthWriteEnable = creation.depthStencil.depthWriteEnable ? VK_TRUE : VK_FALSE,
        .depthCompareOp = creation.depthStencil.depthCompareOp,
    };

    std::array<VkPipelineColorBlendAttachmentState, kMaxImageOutputs> blendAttachments{};
    for (uint32_t index = 0; index < creation.renderPass.colorCount; ++index)
    {
        const BlendStateCreation& blend = creation.blend;
        blendAttachments[index] = VkPipelineColorBlendAttachmentState{
            .blendEnable = blend.blendEnable ? VK_TRUE : VK_FALSE,
            .srcColorBlendFactor = blend.sourceColor,
            .dstColorBlendFactor = blend.destinationColor,
            .colorBlendOp = blend.colorOperation,
            .srcAlphaBlendFactor = blend.sourceAlpha,
            .dstAlphaBlendFactor = blend.destinationAlpha,
            .alphaBlendOp = blend.alphaOperation,
            .colorWriteMask = blend.writeMask,
        };
    }
    const VkPipelineColorBlendStateCreateInfo colorBlendState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
        .attachmentCount = creation.renderPass.colorCount,
        .pAttachments = blendAttachments.data(),
    };

    const std::array<VkDynamicState, 2> dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    const VkPipelineDynamicStateCreateInfo dynamicState{
        .sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO,
        .dynamicStateCount = static_cast<uint32_t>(dynamicStates.size()),
        .pDynamicStates = dynamicStates.data(),
    };

    const VkGraphicsPipelineCreateInfo pipelineInfo{
        .sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO,
        .stageCount = static_cast<uint32_t>(stages.size()),
        .pStages = stages.data(),
        .pVertexInputState = &vertexInputState,
        .pInputAssemblyState = &inputAssemblyState,
        .pViewportState = &viewportState,
        .pRasterizationState = &rasterizationState,
        .pMultisampleState = &multisampleState,
        .pDepthStencilState = &depthStencilState,
        .pColorBlendState = &colorBlendState,
        .pDynamicState = &dynamicState,
        .layout = pipeline.layout,
        .renderPass = GetVulkanRenderPass(creation.renderPass),
        .subpass = 0,
    };
    const uint64_t start = SDL_GetPerformanceCounter();
    CheckVk(vkCreateGraphicsPipelines(m_device, m_pipelineCache, 1, &pipelineInfo, nullptr, &pipeline.pipeline), "vkCreateGraphicsPipelines");
    RecordPipelineCreation(start);
}

void GpuDevice::DestroyPipeline(PipelineHandle handle)
{
    Pipeline* pipeline = m_pipelines.Access(handle);
    if (pipeline == nullptr)
    {
        return;
    }
    Defer([device = m_device, vkPipeline = pipeline->pipeline, layout = pipeline->layout] {
        vkDestroyPipeline(device, vkPipeline, nullptr);
        vkDestroyPipelineLayout(device, layout, nullptr);
    });
    m_pipelines.Release(handle);
}

// =====================================================================================================================
// Frames

bool GpuDevice::RecreateSwapchain()
{
    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_window, &width, &height);
    if (width == 0 || height == 0)
    {
        return false;
    }

    WaitIdle();
    m_swapchain.Destroy();
    if (!m_swapchain.Create(GetVulkanRenderPass(m_swapchainOutput), m_vsync))
    {
        m_swapchain.Destroy();
        return false;
    }

    if (RenderPass* swapchainPass = m_renderPasses.Access(m_swapchainPass))
    {
        swapchainPass->width = m_swapchain.Extent().width;
        swapchainPass->height = m_swapchain.Extent().height;
    }
    m_swapchainDirty = false;
    m_swapchainResized = true;
    return true;
}

void GpuDevice::HandleSurfaceLost()
{
    WaitIdle();
    m_swapchain.Destroy();
    m_swapchain.DestroySurface();
    m_swapchainDirty = true;
}

void GpuDevice::SetSuspended(bool suspended)
{
    if (m_suspended == suspended || m_device == VK_NULL_HANDLE)
    {
        m_suspended = suspended;
        return;
    }
    m_suspended = suspended;
    if (suspended)
    {
        HandleSurfaceLost();
    }
    else
    {
        m_swapchainDirty = true;
    }
}

void GpuDevice::SetVsync(bool enabled)
{
    if (m_vsync != enabled)
    {
        m_vsync = enabled;
        m_swapchainDirty = true;
    }
}

bool GpuDevice::BeginFrame()
{
    MENGINE_ASSERT(!m_frameActive);
    if (m_suspended)
    {
        return false;
    }
    if ((m_swapchainDirty || !m_swapchain.IsValid()) && !RecreateSwapchain())
    {
        SDL_Delay(kRecreateRetryDelayMilliseconds);
        return false;
    }

    FrameData& frame = m_frames[m_frameSlot];
    CheckVk(vkWaitForFences(m_device, 1, &frame.inFlight, VK_TRUE, std::numeric_limits<uint64_t>::max()), "vkWaitForFences");
    ProcessPendingDestructions(false);

    const SwapchainStatus acquireStatus = m_swapchain.Acquire(frame.imageAvailable, m_imageIndex);
    if (acquireStatus == SwapchainStatus::OutOfDate)
    {
        m_swapchainDirty = true;
        return false;
    }
    if (acquireStatus == SwapchainStatus::SurfaceLost)
    {
        HandleSurfaceLost();
        return false;
    }

    // Reset only after a successful acquire so an early return never leaves the fence unsignaled.
    CheckVk(vkResetFences(m_device, 1, &frame.inFlight), "vkResetFences");
    CheckVk(vkResetCommandPool(m_device, frame.commandPool, 0), "vkResetCommandPool");
    const VkCommandBufferBeginInfo beginInfo{
        .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
        .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
    };
    CheckVk(vkBeginCommandBuffer(frame.commandBuffer, &beginInfo), "vkBeginCommandBuffer");

    m_commandBuffer.Init(this, frame.commandBuffer);
    m_profiler.BeginFrame(frame.commandBuffer, m_frameSlot);

    m_dynamicFrameStart = m_frameSlot * m_dynamicBytesPerFrame;
    m_dynamicUsed = 0;
    m_frameActive = true;
    return true;
}

DynamicAllocation GpuDevice::DynamicAllocate(uint32_t size)
{
    MENGINE_ASSERT(m_frameActive);
    const auto offset = static_cast<uint32_t>(AlignUp(m_dynamicUsed, m_properties.limits.minUniformBufferOffsetAlignment));
    if (offset + size > m_dynamicBytesPerFrame)
    {
        throw std::runtime_error("Dynamic uniform ring exhausted: " + std::to_string(offset + size) + " of "
            + std::to_string(m_dynamicBytesPerFrame) + " bytes this frame");
    }
    m_dynamicUsed = offset + size;
    Buffer* buffer = m_buffers.Access(m_dynamicBuffer);
    return DynamicAllocation{
        .data = static_cast<uint8_t*>(buffer->mappedData) + m_dynamicFrameStart + offset,
        .offset = m_dynamicFrameStart + offset,
    };
}

void GpuDevice::EndFrame()
{
    MENGINE_ASSERT(m_frameActive);
    m_frameActive = false;
    FrameData& frame = m_frames[m_frameSlot];

    if (m_dynamicUsed > 0)
    {
        const Buffer* buffer = m_buffers.Access(m_dynamicBuffer);
        vmaFlushAllocation(m_allocator, buffer->allocation, m_dynamicFrameStart, m_dynamicUsed);
    }

    m_commandBuffer.Reset();
    CheckVk(vkEndCommandBuffer(frame.commandBuffer), "vkEndCommandBuffer");

    const VkSemaphore renderFinished = m_swapchain.RenderFinishedSemaphore(m_imageIndex);
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    const VkSubmitInfo submitInfo{
        .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &frame.imageAvailable,
        .pWaitDstStageMask = &waitStage,
        .commandBufferCount = 1,
        .pCommandBuffers = &frame.commandBuffer,
        .signalSemaphoreCount = 1,
        .pSignalSemaphores = &renderFinished,
    };
    CheckVk(vkQueueSubmit(m_queue, 1, &submitInfo, frame.inFlight), "vkQueueSubmit");

    const SwapchainStatus presentStatus = m_swapchain.Present(m_queue, m_imageIndex);
    m_frameSlot = (m_frameSlot + 1) % kMaxFramesInFlight;
    ++m_absoluteFrame;

    if (presentStatus == SwapchainStatus::OutOfDate)
    {
        m_swapchainDirty = true;
    }
    else if (presentStatus == SwapchainStatus::SurfaceLost)
    {
        HandleSurfaceLost();
    }
}

// =====================================================================================================================
// Debug utils

void GpuDevice::SetResourceName(VkObjectType type, uint64_t handle, const char* name)
{
    if (m_setObjectName == nullptr || name == nullptr || name[0] == '\0' || handle == 0)
    {
        return;
    }
    const VkDebugUtilsObjectNameInfoEXT nameInfo{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
        .objectType = type,
        .objectHandle = handle,
        .pObjectName = name,
    };
    m_setObjectName(m_device, &nameInfo);
}

void GpuDevice::CmdBeginLabel(VkCommandBuffer commandBuffer, const char* name)
{
    if (m_cmdBeginLabel == nullptr)
    {
        return;
    }
    const VkDebugUtilsLabelEXT label{
        .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT,
        .pLabelName = name,
        .color = {1.0f, 1.0f, 1.0f, 1.0f},
    };
    m_cmdBeginLabel(commandBuffer, &label);
}

void GpuDevice::CmdEndLabel(VkCommandBuffer commandBuffer)
{
    if (m_cmdEndLabel != nullptr)
    {
        m_cmdEndLabel(commandBuffer);
    }
}

} // namespace mengine::rhi
