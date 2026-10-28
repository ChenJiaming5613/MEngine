#define SDL_MAIN_USE_CALLBACKS 1

#include <vulkan/vulkan.h>

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_vulkan.h>

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wtype-limits"
#endif

#define VMA_IMPLEMENTATION
#include <vk_mem_alloc.h>

#define STB_IMAGE_IMPLEMENTATION
#include <stb_image.h>

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>

#include "Unlit.FragmentMain.h"
#include "Unlit.VertexMain.h"

#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace mengine {

namespace {

constexpr uint32_t kRequiredVulkanVersion = VK_API_VERSION_1_1;
constexpr uint32_t kMaxFramesInFlight = 2;
[[maybe_unused]] constexpr const char* kValidationLayerName = "VK_LAYER_KHRONOS_validation";
constexpr const char* kCubeTexturePath = "textures/m-logo-red.jpg";
constexpr float kRotationRadiansPerSecond = 0.8f;
constexpr double kMaxRotationStepSeconds = 0.1;
constexpr double kStatsRefreshSeconds = 0.5;
constexpr float kFortyFiveDegreesInRadians = 0.785398163f;
constexpr float kHorizontalFieldOfViewForPortrait = kFortyFiveDegreesInRadians;
constexpr float kVerticalFieldOfView = kFortyFiveDegreesInRadians;
constexpr VkClearColorValue kClearColor = {{0.08f, 0.08f, 0.10f, 1.0f}};

// Android reports SUBOPTIMAL whenever the swapchain preTransform differs from the display rotation.
// The compositor handles the rotation for an identity transform, so recreating would only loop.
#if defined(__ANDROID__)
constexpr bool kRecreateSwapChainOnSuboptimal = false;
#else
constexpr bool kRecreateSwapChainOnSuboptimal = true;
#endif

struct Vertex
{
    glm::vec3 position;
    glm::vec2 texCoord;
};

struct PushConstants
{
    glm::mat4 modelViewProjection;
};

struct AllocatedBuffer
{
    VkBuffer buffer = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
};

struct AllocatedImage
{
    VkImage image = VK_NULL_HANDLE;
    VmaAllocation allocation = VK_NULL_HANDLE;
    VkImageView view = VK_NULL_HANDLE;
};

struct FrameContext
{
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    VkSemaphore imageAvailableSemaphore = VK_NULL_HANDLE;
    VkFence inFlightFence = VK_NULL_HANDLE;
};

// Four vertices per face so each face owns its UVs. Quads are counter-clockwise seen from outside.
const std::array<Vertex, 24> kCubeVertices = {{
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},

    {{0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}},

    {{0.5f, -0.5f, 0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}},
    {{0.5f, 0.5f, 0.5f}, {0.0f, 0.0f}},

    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{-0.5f, -0.5f, 0.5f}, {1.0f, 1.0f}},
    {{-0.5f, 0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}},

    {{-0.5f, 0.5f, 0.5f}, {0.0f, 1.0f}},
    {{0.5f, 0.5f, 0.5f}, {1.0f, 1.0f}},
    {{0.5f, 0.5f, -0.5f}, {1.0f, 0.0f}},
    {{-0.5f, 0.5f, -0.5f}, {0.0f, 0.0f}},

    {{-0.5f, -0.5f, -0.5f}, {0.0f, 1.0f}},
    {{0.5f, -0.5f, -0.5f}, {1.0f, 1.0f}},
    {{0.5f, -0.5f, 0.5f}, {1.0f, 0.0f}},
    {{-0.5f, -0.5f, 0.5f}, {0.0f, 0.0f}},
}};

constexpr std::array<uint16_t, 36> MakeCubeIndices()
{
    constexpr std::array<uint16_t, 6> kQuadIndices = {0, 1, 2, 2, 3, 0};
    std::array<uint16_t, 36> indices{};
    for (uint16_t face = 0; face < 6; ++face)
    {
        for (size_t corner = 0; corner < kQuadIndices.size(); ++corner)
        {
            indices[face * kQuadIndices.size() + corner] = static_cast<uint16_t>(face * 4 + kQuadIndices[corner]);
        }
    }
    return indices;
}

constexpr std::array<uint16_t, 36> kCubeIndices = MakeCubeIndices();

template <typename Callback>
class ScopeExit
{
public:
    explicit ScopeExit(Callback callback) : m_callback(std::move(callback)) {}
    ~ScopeExit() { m_callback(); }

    ScopeExit(const ScopeExit&) = delete;
    ScopeExit& operator=(const ScopeExit&) = delete;

private:
    Callback m_callback;
};

void CheckVk(VkResult result, const char* operation)
{
    if (result != VK_SUCCESS)
    {
        throw std::runtime_error(std::string(operation) + " failed with VkResult " + std::to_string(static_cast<int>(result)));
    }
}

void CheckImGuiVkResult(VkResult result)
{
    if (result < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "ImGui Vulkan backend error: VkResult %d", static_cast<int>(result));
    }
}

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
    return std::any_of(layers.begin(), layers.end(),
        [&](const VkLayerProperties& layer) { return layerName == layer.layerName; });
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

std::string ResolveAssetPath(std::string_view relativePath)
{
#if defined(__ANDROID__)
    // Relative paths are resolved against the APK assets by SDL.
    return std::string(relativePath);
#else
    const char* basePath = SDL_GetBasePath();
    return std::string(basePath != nullptr ? basePath : "") + "assets/" + std::string(relativePath);
#endif
}

std::vector<uint8_t> LoadAsset(const std::string& path)
{
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(path.c_str(), &size), &SDL_free);
    if (data == nullptr)
    {
        throw std::runtime_error("Failed to load " + path + ": " + SDL_GetError());
    }
    const auto* bytes = static_cast<const uint8_t*>(data.get());
    return std::vector<uint8_t>(bytes, bytes + size);
}

class Application
{
public:
    Application() = default;
    ~Application() { Shutdown(); }

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize()
    {
        try
        {
            CreateMainWindow();
            CreateInstance();
            CreateSurface();
            PickPhysicalDevice();
            CreateDevice();
            CreateAllocator();
            m_surfaceFormat = ChooseSurfaceFormat();
            m_depthFormat = ChooseDepthFormat();
            CreateRenderPass();
            CreatePipeline();
            CreateFrameContexts();
            CreateCubeResources();
            CreateImGui();
            RecreateSwapChain();
        }
        catch (const std::exception& error)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Initialization failed: %s", error.what());
            return false;
        }

        m_lastFrameCounter = SDL_GetPerformanceCounter();
        return true;
    }

    void Shutdown()
    {
        if (m_device != VK_NULL_HANDLE)
        {
            vkDeviceWaitIdle(m_device);

            ShutdownImGui();

            vkDestroyDescriptorPool(m_device, m_descriptorPool, nullptr);
            vkDestroySampler(m_device, m_textureSampler, nullptr);
            DestroyImage(m_texture);
            DestroyBuffer(m_indexBuffer);
            DestroyBuffer(m_vertexBuffer);
            m_descriptorPool = VK_NULL_HANDLE;
            m_textureSampler = VK_NULL_HANDLE;

            for (FrameContext& frame : m_frames)
            {
                vkDestroyFence(m_device, frame.inFlightFence, nullptr);
                vkDestroySemaphore(m_device, frame.imageAvailableSemaphore, nullptr);
                frame = {};
            }
            vkDestroyCommandPool(m_device, m_commandPool, nullptr);
            m_commandPool = VK_NULL_HANDLE;

            DestroySwapChain();

            vkDestroyPipeline(m_device, m_pipeline, nullptr);
            vkDestroyPipelineLayout(m_device, m_pipelineLayout, nullptr);
            vkDestroyDescriptorSetLayout(m_device, m_descriptorSetLayout, nullptr);
            vkDestroyRenderPass(m_device, m_renderPass, nullptr);
            m_pipeline = VK_NULL_HANDLE;
            m_pipelineLayout = VK_NULL_HANDLE;
            m_descriptorSetLayout = VK_NULL_HANDLE;
            m_renderPass = VK_NULL_HANDLE;

            if (m_allocator != VK_NULL_HANDLE)
            {
                vmaDestroyAllocator(m_allocator);
                m_allocator = VK_NULL_HANDLE;
            }

            vkDestroyDevice(m_device, nullptr);
            m_device = VK_NULL_HANDLE;
        }

        DestroySurface();

        if (m_debugMessenger != VK_NULL_HANDLE)
        {
            auto destroyDebugMessenger = reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkDestroyDebugUtilsMessengerEXT"));
            if (destroyDebugMessenger != nullptr)
            {
                destroyDebugMessenger(m_instance, m_debugMessenger, nullptr);
            }
            m_debugMessenger = VK_NULL_HANDLE;
        }

        if (m_instance != VK_NULL_HANDLE)
        {
            vkDestroyInstance(m_instance, nullptr);
            m_instance = VK_NULL_HANDLE;
        }

        if (m_window != nullptr)
        {
            SDL_DestroyWindow(m_window);
            m_window = nullptr;
        }
    }

    SDL_AppResult Iterate()
    {
        try
        {
            const uint64_t now = SDL_GetPerformanceCounter();
            const double deltaSeconds = static_cast<double>(now - m_lastFrameCounter) / static_cast<double>(SDL_GetPerformanceFrequency());
            m_lastFrameCounter = now;

            if (m_suspended || (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0)
            {
                SDL_Delay(16);
                return SDL_APP_CONTINUE;
            }

            if ((m_swapChainDirty || m_swapChain == VK_NULL_HANDLE) && !RecreateSwapChain())
            {
                SDL_Delay(16);
                return SDL_APP_CONTINUE;
            }

            UpdateRotation(deltaSeconds);
            UpdateFrameStats(deltaSeconds);
            DrawFrame();
        }
        catch (const std::exception& error)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Frame failed: %s", error.what());
            return SDL_APP_FAILURE;
        }
        return SDL_APP_CONTINUE;
    }

    SDL_AppResult HandleEvent(const SDL_Event& event)
    {
        const bool imGuiReady = ImGui::GetCurrentContext() != nullptr && ImGui::GetIO().BackendPlatformUserData != nullptr;
        if (imGuiReady)
        {
            ImGui_ImplSDL3_ProcessEvent(&event);
        }

        try
        {
            switch (event.type)
            {
            case SDL_EVENT_QUIT:
                return SDL_APP_SUCCESS;

            case SDL_EVENT_KEY_DOWN:
                if (imGuiReady && ImGui::GetIO().WantCaptureKeyboard)
                {
                    break;
                }
                if (event.key.key == SDLK_ESCAPE || event.key.key == SDLK_AC_BACK)
                {
                    return SDL_APP_SUCCESS;
                }
                break;

            case SDL_EVENT_WINDOW_RESIZED:
            case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
                m_swapChainDirty = true;
                break;

            // The Android native window and its VkSurfaceKHR go away while in the background.
            case SDL_EVENT_WILL_ENTER_BACKGROUND:
                SetSuspended(true);
                break;

            case SDL_EVENT_DID_ENTER_FOREGROUND:
                SetSuspended(false);
                break;

            default:
                break;
            }
        }
        catch (const std::exception& error)
        {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Event handling failed: %s", error.what());
            return SDL_APP_FAILURE;
        }
        return SDL_APP_CONTINUE;
    }

private:
    void CreateMainWindow()
    {
        m_window = SDL_CreateWindow("MEngine", 1280, 720, SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
        if (m_window == nullptr)
        {
            throw std::runtime_error(std::string("SDL_CreateWindow failed: ") + SDL_GetError());
        }
    }

    void CreateInstance()
    {
        // Looked up at runtime because the Android API 24 loader stub only exports Vulkan 1.0 symbols.
        auto enumerateInstanceVersion = reinterpret_cast<PFN_vkEnumerateInstanceVersion>(
            vkGetInstanceProcAddr(VK_NULL_HANDLE, "vkEnumerateInstanceVersion"));
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
            layers.push_back(kValidationLayerName);
            extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
        }
        else
        {
            SDL_LogWarn(SDL_LOG_CATEGORY_GPU, "Validation layer requested but not available");
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
            auto createDebugMessenger = reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
                vkGetInstanceProcAddr(m_instance, "vkCreateDebugUtilsMessengerEXT"));
            if (createDebugMessenger != nullptr)
            {
                CheckVk(createDebugMessenger(m_instance, &debugMessengerInfo, nullptr, &m_debugMessenger), "vkCreateDebugUtilsMessengerEXT");
            }
        }
    }

    void CreateSurface()
    {
        if (!SDL_Vulkan_CreateSurface(m_window, m_instance, nullptr, &m_surface))
        {
            throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
        }
    }

    void DestroySurface()
    {
        if (m_surface != VK_NULL_HANDLE)
        {
            SDL_Vulkan_DestroySurface(m_instance, m_surface, nullptr);
            m_surface = VK_NULL_HANDLE;
        }
    }

    std::optional<uint32_t> FindGraphicsPresentQueueFamily(VkPhysicalDevice physicalDevice) const
    {
        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> families(familyCount);
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, families.data());

        for (uint32_t familyIndex = 0; familyIndex < familyCount; ++familyIndex)
        {
            VkBool32 presentSupported = VK_FALSE;
            vkGetPhysicalDeviceSurfaceSupportKHR(physicalDevice, familyIndex, m_surface, &presentSupported);
            if ((families[familyIndex].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0 && presentSupported == VK_TRUE)
            {
                return familyIndex;
            }
        }
        return std::nullopt;
    }

    void PickPhysicalDevice()
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

            const std::optional<uint32_t> queueFamily = FindGraphicsPresentQueueFamily(device);
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
                m_graphicsQueueFamily = *queueFamily;
            }
        }

        if (m_physicalDevice == VK_NULL_HANDLE)
        {
            throw std::runtime_error("No GPU supports Vulkan 1.1 with graphics, present and swapchain");
        }

        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(m_physicalDevice, &properties);
        SDL_Log("GPU: %s (Vulkan %u.%u.%u)", properties.deviceName, VK_API_VERSION_MAJOR(properties.apiVersion),
            VK_API_VERSION_MINOR(properties.apiVersion), VK_API_VERSION_PATCH(properties.apiVersion));
    }

    void CreateDevice()
    {
        const float queuePriority = 1.0f;
        const VkDeviceQueueCreateInfo queueInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
            .queueFamilyIndex = m_graphicsQueueFamily,
            .queueCount = 1,
            .pQueuePriorities = &queuePriority,
        };

        const std::array<const char*, 1> deviceExtensions = {VK_KHR_SWAPCHAIN_EXTENSION_NAME};

        const VkDeviceCreateInfo deviceInfo{
            .sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
            .queueCreateInfoCount = 1,
            .pQueueCreateInfos = &queueInfo,
            .enabledExtensionCount = static_cast<uint32_t>(deviceExtensions.size()),
            .ppEnabledExtensionNames = deviceExtensions.data(),
        };
        CheckVk(vkCreateDevice(m_physicalDevice, &deviceInfo, nullptr, &m_device), "vkCreateDevice");
        vkGetDeviceQueue(m_device, m_graphicsQueueFamily, 0, &m_graphicsQueue);
    }

    void CreateAllocator()
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

    VkSurfaceFormatKHR ChooseSurfaceFormat() const
    {
        uint32_t formatCount = 0;
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, nullptr);
        std::vector<VkSurfaceFormatKHR> formats(formatCount);
        vkGetPhysicalDeviceSurfaceFormatsKHR(m_physicalDevice, m_surface, &formatCount, formats.data());
        if (formats.empty())
        {
            throw std::runtime_error("Surface reports no formats");
        }

        for (const VkSurfaceFormatKHR& format : formats)
        {
            const bool isSrgb = format.format == VK_FORMAT_B8G8R8A8_SRGB || format.format == VK_FORMAT_R8G8B8A8_SRGB;
            if (isSrgb && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)
            {
                return format;
            }
        }
        return formats.front();
    }

    VkFormat ChooseDepthFormat() const
    {
        constexpr std::array<VkFormat, 3> kCandidates = {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM};
        for (VkFormat format : kCandidates)
        {
            VkFormatProperties properties{};
            vkGetPhysicalDeviceFormatProperties(m_physicalDevice, format, &properties);
            if ((properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) != 0)
            {
                return format;
            }
        }
        throw std::runtime_error("No supported depth format");
    }

    void CreateRenderPass()
    {
        const std::array<VkAttachmentDescription, 2> attachments = {{
            {
                .format = m_surfaceFormat.format,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_STORE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
            },
            {
                .format = m_depthFormat,
                .samples = VK_SAMPLE_COUNT_1_BIT,
                .loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
                .storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE,
                .stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE,
                .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,
            },
        }};

        const VkAttachmentReference colorReference{.attachment = 0, .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
        const VkAttachmentReference depthReference{.attachment = 1, .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

        const VkSubpassDescription subpass{
            .pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
            .colorAttachmentCount = 1,
            .pColorAttachments = &colorReference,
            .pDepthStencilAttachment = &depthReference,
        };

        // Orders this frame's color write after the acquire semaphore wait, and its depth clear after the previous frame's depth writes.
        const VkSubpassDependency dependency{
            .srcSubpass = VK_SUBPASS_EXTERNAL,
            .dstSubpass = 0,
            .srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT,
            .dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
            .srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
            .dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
        };

        const VkRenderPassCreateInfo renderPassInfo{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
            .attachmentCount = static_cast<uint32_t>(attachments.size()),
            .pAttachments = attachments.data(),
            .subpassCount = 1,
            .pSubpasses = &subpass,
            .dependencyCount = 1,
            .pDependencies = &dependency,
        };
        CheckVk(vkCreateRenderPass(m_device, &renderPassInfo, nullptr, &m_renderPass), "vkCreateRenderPass");
    }

    VkShaderModule CreateShaderModule(const uint32_t* code, size_t codeSizeInBytes) const
    {
        const VkShaderModuleCreateInfo moduleInfo{
            .sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO,
            .codeSize = codeSizeInBytes,
            .pCode = code,
        };
        VkShaderModule module = VK_NULL_HANDLE;
        CheckVk(vkCreateShaderModule(m_device, &moduleInfo, nullptr, &module), "vkCreateShaderModule");
        return module;
    }

    void CreatePipeline()
    {
        const VkDescriptorSetLayoutBinding textureBinding{
            .binding = 0,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .descriptorCount = 1,
            .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
        };
        const VkDescriptorSetLayoutCreateInfo setLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO,
            .bindingCount = 1,
            .pBindings = &textureBinding,
        };
        CheckVk(vkCreateDescriptorSetLayout(m_device, &setLayoutInfo, nullptr, &m_descriptorSetLayout), "vkCreateDescriptorSetLayout");

        const VkPushConstantRange pushConstantRange{
            .stageFlags = VK_SHADER_STAGE_VERTEX_BIT,
            .offset = 0,
            .size = sizeof(PushConstants),
        };
        const VkPipelineLayoutCreateInfo pipelineLayoutInfo{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO,
            .setLayoutCount = 1,
            .pSetLayouts = &m_descriptorSetLayout,
            .pushConstantRangeCount = 1,
            .pPushConstantRanges = &pushConstantRange,
        };
        CheckVk(vkCreatePipelineLayout(m_device, &pipelineLayoutInfo, nullptr, &m_pipelineLayout), "vkCreatePipelineLayout");

        const VkShaderModule vertexModule = CreateShaderModule(g_unlitVertexSpirv, g_unlitVertexSpirv_sizeInBytes);
        ScopeExit destroyVertexModule([&] { vkDestroyShaderModule(m_device, vertexModule, nullptr); });
        const VkShaderModule fragmentModule = CreateShaderModule(g_unlitFragmentSpirv, g_unlitFragmentSpirv_sizeInBytes);
        ScopeExit destroyFragmentModule([&] { vkDestroyShaderModule(m_device, fragmentModule, nullptr); });

        // slangc always names the SPIR-V entry point "main".
        const std::array<VkPipelineShaderStageCreateInfo, 2> stages = {{
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_VERTEX_BIT,
                .module = vertexModule,
                .pName = "main",
            },
            {
                .sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
                .stage = VK_SHADER_STAGE_FRAGMENT_BIT,
                .module = fragmentModule,
                .pName = "main",
            },
        }};

        const VkVertexInputBindingDescription vertexBinding{
            .binding = 0,
            .stride = sizeof(Vertex),
            .inputRate = VK_VERTEX_INPUT_RATE_VERTEX,
        };
        const std::array<VkVertexInputAttributeDescription, 2> vertexAttributes = {{
            {.location = 0, .binding = 0, .format = VK_FORMAT_R32G32B32_SFLOAT, .offset = offsetof(Vertex, position)},
            {.location = 1, .binding = 0, .format = VK_FORMAT_R32G32_SFLOAT, .offset = offsetof(Vertex, texCoord)},
        }};
        const VkPipelineVertexInputStateCreateInfo vertexInputState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO,
            .vertexBindingDescriptionCount = 1,
            .pVertexBindingDescriptions = &vertexBinding,
            .vertexAttributeDescriptionCount = static_cast<uint32_t>(vertexAttributes.size()),
            .pVertexAttributeDescriptions = vertexAttributes.data(),
        };

        const VkPipelineInputAssemblyStateCreateInfo inputAssemblyState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO,
            .topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
        };

        const VkPipelineViewportStateCreateInfo viewportState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO,
            .viewportCount = 1,
            .scissorCount = 1,
        };

        // The projection flips Y, which keeps counter-clockwise triangles front facing.
        const VkPipelineRasterizationStateCreateInfo rasterizationState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO,
            .polygonMode = VK_POLYGON_MODE_FILL,
            .cullMode = VK_CULL_MODE_BACK_BIT,
            .frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE,
            .lineWidth = 1.0f,
        };

        const VkPipelineMultisampleStateCreateInfo multisampleState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO,
            .rasterizationSamples = VK_SAMPLE_COUNT_1_BIT,
        };

        const VkPipelineDepthStencilStateCreateInfo depthStencilState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
            .depthTestEnable = VK_TRUE,
            .depthWriteEnable = VK_TRUE,
            .depthCompareOp = VK_COMPARE_OP_LESS,
        };

        const VkPipelineColorBlendAttachmentState colorBlendAttachment{
            .blendEnable = VK_FALSE,
            .colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT,
        };
        const VkPipelineColorBlendStateCreateInfo colorBlendState{
            .sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO,
            .attachmentCount = 1,
            .pAttachments = &colorBlendAttachment,
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
            .layout = m_pipelineLayout,
            .renderPass = m_renderPass,
            .subpass = 0,
        };
        CheckVk(vkCreateGraphicsPipelines(m_device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &m_pipeline), "vkCreateGraphicsPipelines");
    }

    void CreateFrameContexts()
    {
        const VkCommandPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
            .flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
            .queueFamilyIndex = m_graphicsQueueFamily,
        };
        CheckVk(vkCreateCommandPool(m_device, &poolInfo, nullptr, &m_commandPool), "vkCreateCommandPool");

        const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        const VkFenceCreateInfo fenceInfo{
            .sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
            .flags = VK_FENCE_CREATE_SIGNALED_BIT,
        };

        for (FrameContext& frame : m_frames)
        {
            const VkCommandBufferAllocateInfo commandBufferInfo{
                .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
                .commandPool = m_commandPool,
                .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
                .commandBufferCount = 1,
            };
            CheckVk(vkAllocateCommandBuffers(m_device, &commandBufferInfo, &frame.commandBuffer), "vkAllocateCommandBuffers");
            CheckVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &frame.imageAvailableSemaphore), "vkCreateSemaphore");
            CheckVk(vkCreateFence(m_device, &fenceInfo, nullptr, &frame.inFlightFence), "vkCreateFence");
        }
    }

    AllocatedBuffer CreateBuffer(VkDeviceSize size, VkBufferUsageFlags usage, VmaAllocationCreateFlags allocationFlags) const
    {
        const VkBufferCreateInfo bufferInfo{
            .sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO,
            .size = size,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
        };
        const VmaAllocationCreateInfo allocationInfo{
            .flags = allocationFlags,
            .usage = VMA_MEMORY_USAGE_AUTO,
        };
        AllocatedBuffer buffer;
        CheckVk(vmaCreateBuffer(m_allocator, &bufferInfo, &allocationInfo, &buffer.buffer, &buffer.allocation, nullptr), "vmaCreateBuffer");
        return buffer;
    }

    void FillStagingBuffer(AllocatedBuffer& stagingBuffer, const void* data, VkDeviceSize size) const
    {
        stagingBuffer = CreateBuffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT);
        CheckVk(vmaCopyMemoryToAllocation(m_allocator, data, stagingBuffer.allocation, 0, size), "vmaCopyMemoryToAllocation");
    }

    void DestroyBuffer(AllocatedBuffer& buffer)
    {
        if (buffer.buffer != VK_NULL_HANDLE)
        {
            vmaDestroyBuffer(m_allocator, buffer.buffer, buffer.allocation);
        }
        buffer = {};
    }

    VkImageView CreateImageView(VkImage image, VkFormat format, VkImageAspectFlags aspect) const
    {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = format,
            .subresourceRange = {.aspectMask = aspect, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        CheckVk(vkCreateImageView(m_device, &viewInfo, nullptr, &view), "vkCreateImageView");
        return view;
    }

    AllocatedImage CreateImage(VkExtent2D extent, VkFormat format, VkImageUsageFlags usage, VkImageAspectFlags aspect) const
    {
        const VkImageCreateInfo imageInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
            .imageType = VK_IMAGE_TYPE_2D,
            .format = format,
            .extent = {extent.width, extent.height, 1},
            .mipLevels = 1,
            .arrayLayers = 1,
            .samples = VK_SAMPLE_COUNT_1_BIT,
            .tiling = VK_IMAGE_TILING_OPTIMAL,
            .usage = usage,
            .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
        };
        const VmaAllocationCreateInfo allocationInfo{.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE};

        AllocatedImage image;
        CheckVk(vmaCreateImage(m_allocator, &imageInfo, &allocationInfo, &image.image, &image.allocation, nullptr), "vmaCreateImage");
        try
        {
            image.view = CreateImageView(image.image, format, aspect);
        }
        catch (...)
        {
            vmaDestroyImage(m_allocator, image.image, image.allocation);
            throw;
        }
        return image;
    }

    void DestroyImage(AllocatedImage& image)
    {
        if (image.view != VK_NULL_HANDLE)
        {
            vkDestroyImageView(m_device, image.view, nullptr);
        }
        if (image.image != VK_NULL_HANDLE)
        {
            vmaDestroyImage(m_allocator, image.image, image.allocation);
        }
        image = {};
    }

    void SubmitImmediate(const std::function<void(VkCommandBuffer)>& record)
    {
        const VkCommandBufferAllocateInfo allocateInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
            .commandPool = m_commandPool,
            .level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
            .commandBufferCount = 1,
        };
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        CheckVk(vkAllocateCommandBuffers(m_device, &allocateInfo, &commandBuffer), "vkAllocateCommandBuffers");
        ScopeExit freeCommandBuffer([&] { vkFreeCommandBuffers(m_device, m_commandPool, 1, &commandBuffer); });

        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");
        record(commandBuffer);
        CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");

        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .commandBufferCount = 1,
            .pCommandBuffers = &commandBuffer,
        };
        CheckVk(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE), "vkQueueSubmit");
        CheckVk(vkQueueWaitIdle(m_graphicsQueue), "vkQueueWaitIdle");
    }

    void CreateCubeResources()
    {
        const std::string texturePath = ResolveAssetPath(kCubeTexturePath);
        const std::vector<uint8_t> encodedTexture = LoadAsset(texturePath);

        int textureWidth = 0;
        int textureHeight = 0;
        int sourceChannels = 0;
        std::unique_ptr<stbi_uc, decltype(&stbi_image_free)> pixels(
            stbi_load_from_memory(encodedTexture.data(), static_cast<int>(encodedTexture.size()), &textureWidth, &textureHeight,
                &sourceChannels, STBI_rgb_alpha),
            &stbi_image_free);
        if (pixels == nullptr)
        {
            throw std::runtime_error("Failed to decode " + texturePath + ": " + stbi_failure_reason());
        }

        const VkExtent2D textureExtent = {static_cast<uint32_t>(textureWidth), static_cast<uint32_t>(textureHeight)};
        const VkDeviceSize textureBytes = VkDeviceSize{textureExtent.width} * textureExtent.height * 4;
        const VkDeviceSize vertexBytes = sizeof(Vertex) * kCubeVertices.size();
        const VkDeviceSize indexBytes = sizeof(uint16_t) * kCubeIndices.size();

        m_vertexBuffer = CreateBuffer(vertexBytes, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
        m_indexBuffer = CreateBuffer(indexBytes, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, 0);
        m_texture = CreateImage(textureExtent, VK_FORMAT_R8G8B8A8_SRGB, VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT,
            VK_IMAGE_ASPECT_COLOR_BIT);

        AllocatedBuffer vertexStaging;
        AllocatedBuffer indexStaging;
        AllocatedBuffer textureStaging;
        ScopeExit destroyStaging([&] {
            DestroyBuffer(vertexStaging);
            DestroyBuffer(indexStaging);
            DestroyBuffer(textureStaging);
        });
        FillStagingBuffer(vertexStaging, kCubeVertices.data(), vertexBytes);
        FillStagingBuffer(indexStaging, kCubeIndices.data(), indexBytes);
        FillStagingBuffer(textureStaging, pixels.get(), textureBytes);

        SubmitImmediate([&](VkCommandBuffer commandBuffer) {
            const VkBufferCopy vertexCopy{.size = vertexBytes};
            const VkBufferCopy indexCopy{.size = indexBytes};
            vkCmdCopyBuffer(commandBuffer, vertexStaging.buffer, m_vertexBuffer.buffer, 1, &vertexCopy);
            vkCmdCopyBuffer(commandBuffer, indexStaging.buffer, m_indexBuffer.buffer, 1, &indexCopy);

            const VkImageSubresourceRange colorRange{
                .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1};

            const VkImageMemoryBarrier toTransferDestination{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = 0,
                .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
                .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = m_texture.image,
                .subresourceRange = colorRange,
            };
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr,
                1, &toTransferDestination);

            const VkBufferImageCopy textureCopy{
                .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .mipLevel = 0, .baseArrayLayer = 0, .layerCount = 1},
                .imageExtent = {textureExtent.width, textureExtent.height, 1},
            };
            vkCmdCopyBufferToImage(commandBuffer, textureStaging.buffer, m_texture.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &textureCopy);

            const VkMemoryBarrier geometryReady{
                .sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT,
            };
            const VkImageMemoryBarrier toShaderRead{
                .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
                .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
                .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
                .oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                .newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
                .image = m_texture.image,
                .subresourceRange = colorRange,
            };
            vkCmdPipelineBarrier(commandBuffer, VK_PIPELINE_STAGE_TRANSFER_BIT,
                VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 1, &geometryReady, 0, nullptr, 1,
                &toShaderRead);
        });

        const VkSamplerCreateInfo samplerInfo{
            .sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO,
            .magFilter = VK_FILTER_LINEAR,
            .minFilter = VK_FILTER_LINEAR,
            .mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR,
            .addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT,
            .maxLod = 0.0f,
        };
        CheckVk(vkCreateSampler(m_device, &samplerInfo, nullptr, &m_textureSampler), "vkCreateSampler");

        const VkDescriptorPoolSize poolSize{.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, .descriptorCount = 1};
        const VkDescriptorPoolCreateInfo poolInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO,
            .maxSets = 1,
            .poolSizeCount = 1,
            .pPoolSizes = &poolSize,
        };
        CheckVk(vkCreateDescriptorPool(m_device, &poolInfo, nullptr, &m_descriptorPool), "vkCreateDescriptorPool");

        const VkDescriptorSetAllocateInfo setInfo{
            .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO,
            .descriptorPool = m_descriptorPool,
            .descriptorSetCount = 1,
            .pSetLayouts = &m_descriptorSetLayout,
        };
        CheckVk(vkAllocateDescriptorSets(m_device, &setInfo, &m_descriptorSet), "vkAllocateDescriptorSets");

        const VkDescriptorImageInfo textureDescriptor{
            .sampler = m_textureSampler,
            .imageView = m_texture.view,
            .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
        };
        const VkWriteDescriptorSet write{
            .sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET,
            .dstSet = m_descriptorSet,
            .dstBinding = 0,
            .descriptorCount = 1,
            .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
            .pImageInfo = &textureDescriptor,
        };
        vkUpdateDescriptorSets(m_device, 1, &write, 0, nullptr);

        SDL_Log("Loaded %s (%dx%d)", texturePath.c_str(), textureWidth, textureHeight);
    }

    void CreateImGui()
    {
        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGuiIO& io = ImGui::GetIO();
        io.IniFilename = nullptr;
        ImGui::StyleColorsDark();

        const float displayScale = SDL_GetWindowDisplayScale(m_window);
        if (displayScale > 0.0f)
        {
            ImGui::GetStyle().ScaleAllSizes(displayScale);
            ImGui::GetStyle().FontScaleDpi = displayScale;
        }
#if defined(__ANDROID__)
        ImGui::GetStyle().TouchExtraPadding = ImVec2(4.0f, 4.0f);
#endif

        if (!ImGui_ImplSDL3_InitForVulkan(m_window))
        {
            throw std::runtime_error("ImGui_ImplSDL3_InitForVulkan failed");
        }

        // ApiVersion must match the instance, otherwise the backend assumes the header version and uses newer entry points.
        ImGui_ImplVulkan_InitInfo initInfo = {};
        initInfo.ApiVersion = kRequiredVulkanVersion;
        initInfo.Instance = m_instance;
        initInfo.PhysicalDevice = m_physicalDevice;
        initInfo.Device = m_device;
        initInfo.QueueFamily = m_graphicsQueueFamily;
        initInfo.Queue = m_graphicsQueue;
        initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
        initInfo.MinImageCount = kMaxFramesInFlight;
        initInfo.ImageCount = kMaxFramesInFlight;
        initInfo.UseDynamicRendering = false;
        initInfo.PipelineInfoMain.RenderPass = m_renderPass;
        initInfo.PipelineInfoMain.Subpass = 0;
        initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
        initInfo.CheckVkResultFn = CheckImGuiVkResult;
        if (!ImGui_ImplVulkan_Init(&initInfo))
        {
            throw std::runtime_error("ImGui_ImplVulkan_Init failed");
        }
    }

    void ShutdownImGui()
    {
        if (ImGui::GetCurrentContext() == nullptr)
        {
            return;
        }
        const ImGuiIO& io = ImGui::GetIO();
        if (io.BackendRendererUserData != nullptr)
        {
            ImGui_ImplVulkan_Shutdown();
        }
        if (io.BackendPlatformUserData != nullptr)
        {
            ImGui_ImplSDL3_Shutdown();
        }
        ImGui::DestroyContext();
    }

    VkExtent2D ChooseSwapExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
    {
        if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
        {
            return capabilities.currentExtent;
        }

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        return VkExtent2D{
            std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
            std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
        };
    }

    static VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
    {
        constexpr std::array<VkCompositeAlphaFlagBitsKHR, 4> kPreferenceOrder = {
            VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR,
            VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR,
            VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
            VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR,
        };
        for (VkCompositeAlphaFlagBitsKHR mode : kPreferenceOrder)
        {
            if ((supported & mode) != 0)
            {
                return mode;
            }
        }
        return VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    }

    VkPresentModeKHR ChoosePresentMode() const
    {
        if (m_vsyncEnabled)
        {
            return VK_PRESENT_MODE_FIFO_KHR;
        }

        uint32_t modeCount = 0;
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, nullptr);
        std::vector<VkPresentModeKHR> modes(modeCount);
        vkGetPhysicalDeviceSurfacePresentModesKHR(m_physicalDevice, m_surface, &modeCount, modes.data());

        // Immediate is true vsync-off (may tear); mailbox is the tear-free fallback but some drivers still pace it. FIFO always exists.
        constexpr std::array<VkPresentModeKHR, 2> kUncappedPreferenceOrder = {VK_PRESENT_MODE_IMMEDIATE_KHR, VK_PRESENT_MODE_MAILBOX_KHR};
        for (VkPresentModeKHR preferred : kUncappedPreferenceOrder)
        {
            if (std::find(modes.begin(), modes.end(), preferred) != modes.end())
            {
                return preferred;
            }
        }
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    static const char* GetPresentModeName(VkPresentModeKHR mode)
    {
        switch (mode)
        {
        case VK_PRESENT_MODE_IMMEDIATE_KHR:
            return "Immediate";
        case VK_PRESENT_MODE_MAILBOX_KHR:
            return "Mailbox";
        case VK_PRESENT_MODE_FIFO_KHR:
            return "FIFO";
        case VK_PRESENT_MODE_FIFO_RELAXED_KHR:
            return "FIFO Relaxed";
        default:
            return "Unknown";
        }
    }

    bool CreateSwapChain()
    {
        VkBool32 presentSupported = VK_FALSE;
        CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(m_physicalDevice, m_graphicsQueueFamily, m_surface, &presentSupported),
            "vkGetPhysicalDeviceSurfaceSupportKHR");
        if (presentSupported != VK_TRUE)
        {
            throw std::runtime_error("The graphics queue cannot present to the surface");
        }

        if (ChooseSurfaceFormat().format != m_surfaceFormat.format)
        {
            throw std::runtime_error("The surface format changed and no longer matches the render pass");
        }

        VkSurfaceCapabilitiesKHR capabilities{};
        CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_physicalDevice, m_surface, &capabilities), "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

        const VkExtent2D extent = ChooseSwapExtent(capabilities);
        if (extent.width == 0 || extent.height == 0)
        {
            return false;
        }

        uint32_t imageCount = capabilities.minImageCount + 1;
        if (capabilities.maxImageCount > 0)
        {
            imageCount = std::min(imageCount, capabilities.maxImageCount);
        }

        // Identity lets the compositor handle device rotation, so the renderer never has to pre-rotate.
        const VkSurfaceTransformFlagBitsKHR preTransform = (capabilities.supportedTransforms & VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR) != 0
            ? VK_SURFACE_TRANSFORM_IDENTITY_BIT_KHR
            : capabilities.currentTransform;

        const VkPresentModeKHR presentMode = ChoosePresentMode();

        const VkSwapchainCreateInfoKHR swapChainInfo{
            .sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR,
            .surface = m_surface,
            .minImageCount = imageCount,
            .imageFormat = m_surfaceFormat.format,
            .imageColorSpace = m_surfaceFormat.colorSpace,
            .imageExtent = extent,
            .imageArrayLayers = 1,
            .imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
            .imageSharingMode = VK_SHARING_MODE_EXCLUSIVE,
            .preTransform = preTransform,
            .compositeAlpha = ChooseCompositeAlpha(capabilities.supportedCompositeAlpha),
            .presentMode = presentMode,
            .clipped = VK_TRUE,
        };
        CheckVk(vkCreateSwapchainKHR(m_device, &swapChainInfo, nullptr, &m_swapChain), "vkCreateSwapchainKHR");
        m_swapChainExtent = extent;
        m_presentMode = presentMode;

        uint32_t swapChainImageCount = 0;
        vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapChainImageCount, nullptr);
        m_swapChainImages.resize(swapChainImageCount);
        vkGetSwapchainImagesKHR(m_device, m_swapChain, &swapChainImageCount, m_swapChainImages.data());

        const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
        for (VkImage image : m_swapChainImages)
        {
            m_swapChainImageViews.push_back(CreateImageView(image, m_surfaceFormat.format, VK_IMAGE_ASPECT_COLOR_BIT));

            // One per swapchain image: presentation may still wait on it after the frame's fence signals.
            VkSemaphore renderFinished = VK_NULL_HANDLE;
            CheckVk(vkCreateSemaphore(m_device, &semaphoreInfo, nullptr, &renderFinished), "vkCreateSemaphore");
            m_renderFinishedSemaphores.push_back(renderFinished);
        }

        m_depthImage = CreateImage(extent, m_depthFormat, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT, VK_IMAGE_ASPECT_DEPTH_BIT);

        for (VkImageView colorView : m_swapChainImageViews)
        {
            const std::array<VkImageView, 2> attachments = {colorView, m_depthImage.view};
            const VkFramebufferCreateInfo framebufferInfo{
                .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
                .renderPass = m_renderPass,
                .attachmentCount = static_cast<uint32_t>(attachments.size()),
                .pAttachments = attachments.data(),
                .width = extent.width,
                .height = extent.height,
                .layers = 1,
            };
            VkFramebuffer framebuffer = VK_NULL_HANDLE;
            CheckVk(vkCreateFramebuffer(m_device, &framebufferInfo, nullptr, &framebuffer), "vkCreateFramebuffer");
            m_framebuffers.push_back(framebuffer);
        }
        return true;
    }

    void DestroySwapChain()
    {
        for (VkFramebuffer framebuffer : m_framebuffers)
        {
            vkDestroyFramebuffer(m_device, framebuffer, nullptr);
        }
        m_framebuffers.clear();

        DestroyImage(m_depthImage);

        for (VkImageView view : m_swapChainImageViews)
        {
            vkDestroyImageView(m_device, view, nullptr);
        }
        m_swapChainImageViews.clear();

        for (VkSemaphore semaphore : m_renderFinishedSemaphores)
        {
            vkDestroySemaphore(m_device, semaphore, nullptr);
        }
        m_renderFinishedSemaphores.clear();

        m_swapChainImages.clear();

        if (m_swapChain != VK_NULL_HANDLE)
        {
            vkDestroySwapchainKHR(m_device, m_swapChain, nullptr);
            m_swapChain = VK_NULL_HANDLE;
        }
    }

    bool RecreateSwapChain()
    {
        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(m_window, &width, &height);
        if (width == 0 || height == 0)
        {
            return false;
        }

        CheckVk(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
        DestroySwapChain();

        if (m_surface == VK_NULL_HANDLE)
        {
            CreateSurface();
        }

        if (!CreateSwapChain())
        {
            DestroySwapChain();
            return false;
        }

        m_swapChainDirty = false;
        return true;
    }

    void HandleSurfaceLost()
    {
        CheckVk(vkDeviceWaitIdle(m_device), "vkDeviceWaitIdle");
        DestroySwapChain();
        DestroySurface();
        m_swapChainDirty = true;
    }

    void SetSuspended(bool suspended)
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
            m_swapChainDirty = true;
            m_lastFrameCounter = SDL_GetPerformanceCounter();
        }
    }

    void UpdateRotation(double deltaSeconds)
    {
        if (!m_rotationEnabled)
        {
            return;
        }
        const double stepSeconds = std::min(deltaSeconds, kMaxRotationStepSeconds);
        m_rotationAngle = std::fmod(m_rotationAngle + kRotationRadiansPerSecond * static_cast<float>(stepSeconds), glm::two_pi<float>());
    }

    void UpdateFrameStats(double deltaSeconds)
    {
        m_statsAccumulatedSeconds += deltaSeconds;
        ++m_statsAccumulatedFrames;
        if (m_statsAccumulatedSeconds >= kStatsRefreshSeconds)
        {
            m_displayedFramesPerSecond = static_cast<float>(m_statsAccumulatedFrames / m_statsAccumulatedSeconds);
            m_displayedFrameMilliseconds = static_cast<float>(m_statsAccumulatedSeconds * 1000.0 / m_statsAccumulatedFrames);
            m_statsAccumulatedSeconds = 0.0;
            m_statsAccumulatedFrames = 0;
        }
    }

    glm::mat4 ComputeModelViewProjection() const
    {
        const float aspectRatio = static_cast<float>(m_swapChainExtent.width) / static_cast<float>(m_swapChainExtent.height);

        // Keeps the cube fully visible in portrait by fixing the horizontal field of view instead.
        const float verticalFieldOfView = aspectRatio >= 1.0f
            ? kVerticalFieldOfView
            : 2.0f * std::atan(std::tan(kHorizontalFieldOfViewForPortrait * 0.5f) / aspectRatio);

        glm::mat4 projection = glm::perspective(verticalFieldOfView, aspectRatio, 0.1f, 100.0f);
        projection[1][1] *= -1.0f;

        const glm::mat4 view = glm::lookAt(glm::vec3(0.0f, 1.2f, 2.4f), glm::vec3(0.0f), glm::vec3(0.0f, 1.0f, 0.0f));
        const glm::mat4 model = glm::rotate(glm::mat4(1.0f), m_rotationAngle, glm::normalize(glm::vec3(0.3f, 1.0f, 0.2f)));
        return projection * view * model;
    }

    void BuildStatsPanel()
    {
        ImGui_ImplVulkan_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        ImGui::SetNextWindowPos(ImVec2(16.0f, 16.0f), ImGuiCond_FirstUseEver);
        ImGui::Begin("MEngine", nullptr, ImGuiWindowFlags_AlwaysAutoResize);
        if (ImGui::Button(m_rotationEnabled ? "Pause Rotation###RotationToggle" : "Resume Rotation###RotationToggle"))
        {
            m_rotationEnabled = !m_rotationEnabled;
        }
        if (ImGui::Button(m_vsyncEnabled ? "Disable VSync###VSyncToggle" : "Enable VSync###VSyncToggle"))
        {
            m_vsyncEnabled = !m_vsyncEnabled;
            m_swapChainDirty = true;
        }
        ImGui::Text("Present Mode: %s", GetPresentModeName(m_presentMode));
        ImGui::Text("FPS: %.1f", m_displayedFramesPerSecond);
        ImGui::Text("Frame Time: %.2f ms", m_displayedFrameMilliseconds);
        ImGui::End();

        ImGui::Render();
    }

    void RecordCommandBuffer(VkCommandBuffer commandBuffer, uint32_t imageIndex)
    {
        const VkCommandBufferBeginInfo beginInfo{
            .sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
            .flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
        };
        CheckVk(vkBeginCommandBuffer(commandBuffer, &beginInfo), "vkBeginCommandBuffer");

        const std::array<VkClearValue, 2> clearValues = {{
            {.color = kClearColor},
            {.depthStencil = {1.0f, 0}},
        }};
        const VkRect2D renderArea{.offset = {0, 0}, .extent = m_swapChainExtent};
        const VkRenderPassBeginInfo renderPassBegin{
            .sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
            .renderPass = m_renderPass,
            .framebuffer = m_framebuffers[imageIndex],
            .renderArea = renderArea,
            .clearValueCount = static_cast<uint32_t>(clearValues.size()),
            .pClearValues = clearValues.data(),
        };
        vkCmdBeginRenderPass(commandBuffer, &renderPassBegin, VK_SUBPASS_CONTENTS_INLINE);

        const VkViewport viewport{
            .x = 0.0f,
            .y = 0.0f,
            .width = static_cast<float>(m_swapChainExtent.width),
            .height = static_cast<float>(m_swapChainExtent.height),
            .minDepth = 0.0f,
            .maxDepth = 1.0f,
        };
        vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
        vkCmdSetScissor(commandBuffer, 0, 1, &renderArea);

        vkCmdBindPipeline(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
        const VkDeviceSize vertexOffset = 0;
        vkCmdBindVertexBuffers(commandBuffer, 0, 1, &m_vertexBuffer.buffer, &vertexOffset);
        vkCmdBindIndexBuffer(commandBuffer, m_indexBuffer.buffer, 0, VK_INDEX_TYPE_UINT16);
        vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipelineLayout, 0, 1, &m_descriptorSet, 0, nullptr);

        const PushConstants pushConstants{.modelViewProjection = ComputeModelViewProjection()};
        vkCmdPushConstants(commandBuffer, m_pipelineLayout, VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(pushConstants), &pushConstants);
        vkCmdDrawIndexed(commandBuffer, static_cast<uint32_t>(kCubeIndices.size()), 1, 0, 0, 0);

        ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer);

        vkCmdEndRenderPass(commandBuffer);
        CheckVk(vkEndCommandBuffer(commandBuffer), "vkEndCommandBuffer");
    }

    void DrawFrame()
    {
        FrameContext& frame = m_frames[m_frameIndex];
        CheckVk(vkWaitForFences(m_device, 1, &frame.inFlightFence, VK_TRUE, std::numeric_limits<uint64_t>::max()), "vkWaitForFences");

        uint32_t imageIndex = 0;
        const VkResult acquireResult = vkAcquireNextImageKHR(m_device, m_swapChain, std::numeric_limits<uint64_t>::max(),
            frame.imageAvailableSemaphore, VK_NULL_HANDLE, &imageIndex);
        if (acquireResult == VK_ERROR_OUT_OF_DATE_KHR)
        {
            m_swapChainDirty = true;
            return;
        }
        if (acquireResult == VK_ERROR_SURFACE_LOST_KHR)
        {
            HandleSurfaceLost();
            return;
        }
        if (acquireResult != VK_SUBOPTIMAL_KHR)
        {
            CheckVk(acquireResult, "vkAcquireNextImageKHR");
        }

        // Reset only after a successful acquire so an early return never leaves the fence unsignaled.
        CheckVk(vkResetFences(m_device, 1, &frame.inFlightFence), "vkResetFences");

        BuildStatsPanel();

        CheckVk(vkResetCommandBuffer(frame.commandBuffer, 0), "vkResetCommandBuffer");
        RecordCommandBuffer(frame.commandBuffer, imageIndex);

        const VkSemaphore renderFinished = m_renderFinishedSemaphores[imageIndex];
        const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        const VkSubmitInfo submitInfo{
            .sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &frame.imageAvailableSemaphore,
            .pWaitDstStageMask = &waitStage,
            .commandBufferCount = 1,
            .pCommandBuffers = &frame.commandBuffer,
            .signalSemaphoreCount = 1,
            .pSignalSemaphores = &renderFinished,
        };
        CheckVk(vkQueueSubmit(m_graphicsQueue, 1, &submitInfo, frame.inFlightFence), "vkQueueSubmit");

        const VkPresentInfoKHR presentInfo{
            .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
            .waitSemaphoreCount = 1,
            .pWaitSemaphores = &renderFinished,
            .swapchainCount = 1,
            .pSwapchains = &m_swapChain,
            .pImageIndices = &imageIndex,
        };
        const VkResult presentResult = vkQueuePresentKHR(m_graphicsQueue, &presentInfo);
        if (presentResult == VK_ERROR_OUT_OF_DATE_KHR || (presentResult == VK_SUBOPTIMAL_KHR && kRecreateSwapChainOnSuboptimal))
        {
            m_swapChainDirty = true;
        }
        else if (presentResult == VK_ERROR_SURFACE_LOST_KHR)
        {
            HandleSurfaceLost();
        }
        else if (presentResult != VK_SUBOPTIMAL_KHR)
        {
            CheckVk(presentResult, "vkQueuePresentKHR");
        }

        m_frameIndex = (m_frameIndex + 1) % kMaxFramesInFlight;
    }

    SDL_Window* m_window = nullptr;

    VkInstance m_instance = VK_NULL_HANDLE;
    VkDebugUtilsMessengerEXT m_debugMessenger = VK_NULL_HANDLE;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkPhysicalDevice m_physicalDevice = VK_NULL_HANDLE;
    VkDevice m_device = VK_NULL_HANDLE;
    uint32_t m_graphicsQueueFamily = 0;
    VkQueue m_graphicsQueue = VK_NULL_HANDLE;
    VmaAllocator m_allocator = VK_NULL_HANDLE;

    VkSurfaceFormatKHR m_surfaceFormat = {};
    VkFormat m_depthFormat = VK_FORMAT_UNDEFINED;
    VkSwapchainKHR m_swapChain = VK_NULL_HANDLE;
    VkExtent2D m_swapChainExtent = {0, 0};
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> m_swapChainImages;
    std::vector<VkImageView> m_swapChainImageViews;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFramebuffer> m_framebuffers;
    AllocatedImage m_depthImage;

    VkRenderPass m_renderPass = VK_NULL_HANDLE;
    VkDescriptorSetLayout m_descriptorSetLayout = VK_NULL_HANDLE;
    VkPipelineLayout m_pipelineLayout = VK_NULL_HANDLE;
    VkPipeline m_pipeline = VK_NULL_HANDLE;

    VkCommandPool m_commandPool = VK_NULL_HANDLE;
    std::array<FrameContext, kMaxFramesInFlight> m_frames{};
    uint32_t m_frameIndex = 0;

    AllocatedBuffer m_vertexBuffer;
    AllocatedBuffer m_indexBuffer;
    AllocatedImage m_texture;
    VkSampler m_textureSampler = VK_NULL_HANDLE;
    VkDescriptorPool m_descriptorPool = VK_NULL_HANDLE;
    VkDescriptorSet m_descriptorSet = VK_NULL_HANDLE;

    bool m_validationEnabled = false;
    bool m_swapChainDirty = true;
    bool m_suspended = false;
    bool m_vsyncEnabled = true;

    bool m_rotationEnabled = true;
    float m_rotationAngle = 0.0f;

    uint64_t m_lastFrameCounter = 0;
    double m_statsAccumulatedSeconds = 0.0;
    uint32_t m_statsAccumulatedFrames = 0;
    float m_displayedFramesPerSecond = 0.0f;
    float m_displayedFrameMilliseconds = 0.0f;
};

} // namespace

} // namespace mengine

SDL_AppResult SDL_AppInit(void** appState, int, char*[])
{
    SDL_SetAppMetadata("MEngine", "0.1.0", "com.mengine.app");

    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }

    auto* application = new mengine::Application();
    *appState = application;
    return application->Initialize() ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
}

SDL_AppResult SDL_AppIterate(void* appState)
{
    return static_cast<mengine::Application*>(appState)->Iterate();
}

SDL_AppResult SDL_AppEvent(void* appState, SDL_Event* event)
{
    return static_cast<mengine::Application*>(appState)->HandleEvent(*event);
}

void SDL_AppQuit(void* appState, SDL_AppResult)
{
    delete static_cast<mengine::Application*>(appState);
}
