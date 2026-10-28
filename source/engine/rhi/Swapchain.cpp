#include "rhi/Swapchain.hpp"

#include <SDL3/SDL_video.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <limits>
#include <stdexcept>
#include <string>

namespace mengine::rhi {

namespace {

// Android reports SUBOPTIMAL whenever the swapchain preTransform differs from the display rotation.
// The compositor handles the rotation for an identity transform, so recreating would only loop.
#if defined(__ANDROID__)
constexpr bool kRecreateOnSuboptimal = false;
#else
constexpr bool kRecreateOnSuboptimal = true;
#endif

VkCompositeAlphaFlagBitsKHR ChooseCompositeAlpha(VkCompositeAlphaFlagsKHR supported)
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

} // namespace

const char* PresentModeName(VkPresentModeKHR mode)
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

void Swapchain::CreateSurface()
{
    if (!SDL_Vulkan_CreateSurface(m_context.window, m_context.instance, nullptr, &m_surface))
    {
        throw std::runtime_error(std::string("SDL_Vulkan_CreateSurface failed: ") + SDL_GetError());
    }
}

void Swapchain::DestroySurface()
{
    if (m_surface != VK_NULL_HANDLE)
    {
        SDL_Vulkan_DestroySurface(m_context.instance, m_surface, nullptr);
        m_surface = VK_NULL_HANDLE;
    }
}

VkSurfaceFormatKHR Swapchain::ChooseSurfaceFormat() const
{
    uint32_t formatCount = 0;
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_context.physicalDevice, m_surface, &formatCount, nullptr);
    std::vector<VkSurfaceFormatKHR> formats(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(m_context.physicalDevice, m_surface, &formatCount, formats.data());
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

VkExtent2D Swapchain::ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const
{
    if (capabilities.currentExtent.width != std::numeric_limits<uint32_t>::max())
    {
        return capabilities.currentExtent;
    }

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(m_context.window, &width, &height);
    return VkExtent2D{
        std::clamp(static_cast<uint32_t>(width), capabilities.minImageExtent.width, capabilities.maxImageExtent.width),
        std::clamp(static_cast<uint32_t>(height), capabilities.minImageExtent.height, capabilities.maxImageExtent.height),
    };
}

VkPresentModeKHR Swapchain::ChoosePresentMode(bool vsync) const
{
    if (vsync)
    {
        return VK_PRESENT_MODE_FIFO_KHR;
    }

    uint32_t modeCount = 0;
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_context.physicalDevice, m_surface, &modeCount, nullptr);
    std::vector<VkPresentModeKHR> modes(modeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(m_context.physicalDevice, m_surface, &modeCount, modes.data());

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

bool Swapchain::Create(VkRenderPass renderPass, bool vsync)
{
    int windowWidth = 0;
    int windowHeight = 0;
    SDL_GetWindowSizeInPixels(m_context.window, &windowWidth, &windowHeight);
    if (windowWidth == 0 || windowHeight == 0)
    {
        return false;
    }

    if (m_surface == VK_NULL_HANDLE)
    {
        CreateSurface();
    }

    VkBool32 presentSupported = VK_FALSE;
    CheckVk(vkGetPhysicalDeviceSurfaceSupportKHR(m_context.physicalDevice, m_context.queueFamily, m_surface, &presentSupported),
        "vkGetPhysicalDeviceSurfaceSupportKHR");
    if (presentSupported != VK_TRUE)
    {
        throw std::runtime_error("The graphics queue cannot present to the surface");
    }

    if (ChooseSurfaceFormat().format != m_surfaceFormat.format)
    {
        throw std::runtime_error("The surface format changed and no longer matches the swapchain render pass");
    }

    VkSurfaceCapabilitiesKHR capabilities{};
    CheckVk(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(m_context.physicalDevice, m_surface, &capabilities),
        "vkGetPhysicalDeviceSurfaceCapabilitiesKHR");

    const VkExtent2D extent = ChooseExtent(capabilities);
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

    const VkPresentModeKHR presentMode = ChoosePresentMode(vsync);

    const VkSwapchainCreateInfoKHR swapchainInfo{
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
    CheckVk(vkCreateSwapchainKHR(m_context.device, &swapchainInfo, nullptr, &m_swapchain), "vkCreateSwapchainKHR");
    m_extent = extent;
    m_presentMode = presentMode;

    uint32_t swapchainImageCount = 0;
    vkGetSwapchainImagesKHR(m_context.device, m_swapchain, &swapchainImageCount, nullptr);
    m_images.resize(swapchainImageCount);
    vkGetSwapchainImagesKHR(m_context.device, m_swapchain, &swapchainImageCount, m_images.data());

    const VkSemaphoreCreateInfo semaphoreInfo{.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
    for (VkImage image : m_images)
    {
        const VkImageViewCreateInfo viewInfo{
            .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
            .image = image,
            .viewType = VK_IMAGE_VIEW_TYPE_2D,
            .format = m_surfaceFormat.format,
            .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT, .baseMipLevel = 0, .levelCount = 1, .baseArrayLayer = 0, .layerCount = 1},
        };
        VkImageView view = VK_NULL_HANDLE;
        CheckVk(vkCreateImageView(m_context.device, &viewInfo, nullptr, &view), "vkCreateImageView");
        m_imageViews.push_back(view);

        // One per swapchain image: presentation may still wait on it after the frame's fence signals.
        VkSemaphore renderFinished = VK_NULL_HANDLE;
        CheckVk(vkCreateSemaphore(m_context.device, &semaphoreInfo, nullptr, &renderFinished), "vkCreateSemaphore");
        m_renderFinishedSemaphores.push_back(renderFinished);

        const VkFramebufferCreateInfo framebufferInfo{
            .sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
            .renderPass = renderPass,
            .attachmentCount = 1,
            .pAttachments = &view,
            .width = extent.width,
            .height = extent.height,
            .layers = 1,
        };
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        CheckVk(vkCreateFramebuffer(m_context.device, &framebufferInfo, nullptr, &framebuffer), "vkCreateFramebuffer");
        m_framebuffers.push_back(framebuffer);
    }
    return true;
}

void Swapchain::Destroy()
{
    for (VkFramebuffer framebuffer : m_framebuffers)
    {
        vkDestroyFramebuffer(m_context.device, framebuffer, nullptr);
    }
    m_framebuffers.clear();

    for (VkImageView view : m_imageViews)
    {
        vkDestroyImageView(m_context.device, view, nullptr);
    }
    m_imageViews.clear();

    for (VkSemaphore semaphore : m_renderFinishedSemaphores)
    {
        vkDestroySemaphore(m_context.device, semaphore, nullptr);
    }
    m_renderFinishedSemaphores.clear();

    m_images.clear();

    if (m_swapchain != VK_NULL_HANDLE)
    {
        vkDestroySwapchainKHR(m_context.device, m_swapchain, nullptr);
        m_swapchain = VK_NULL_HANDLE;
    }
}

SwapchainStatus Swapchain::Acquire(VkSemaphore imageAvailable, uint32_t& imageIndex)
{
    const VkResult result =
        vkAcquireNextImageKHR(m_context.device, m_swapchain, std::numeric_limits<uint64_t>::max(), imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (result == VK_ERROR_OUT_OF_DATE_KHR)
    {
        return SwapchainStatus::OutOfDate;
    }
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        return SwapchainStatus::SurfaceLost;
    }
    // A suboptimal image is still presentable; Present decides whether to recreate.
    if (result != VK_SUBOPTIMAL_KHR)
    {
        CheckVk(result, "vkAcquireNextImageKHR");
    }
    return SwapchainStatus::Ok;
}

SwapchainStatus Swapchain::Present(VkQueue queue, uint32_t imageIndex)
{
    const VkSemaphore renderFinished = m_renderFinishedSemaphores[imageIndex];
    const VkPresentInfoKHR presentInfo{
        .sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR,
        .waitSemaphoreCount = 1,
        .pWaitSemaphores = &renderFinished,
        .swapchainCount = 1,
        .pSwapchains = &m_swapchain,
        .pImageIndices = &imageIndex,
    };
    const VkResult result = vkQueuePresentKHR(queue, &presentInfo);
    if (result == VK_ERROR_OUT_OF_DATE_KHR || (result == VK_SUBOPTIMAL_KHR && kRecreateOnSuboptimal))
    {
        return SwapchainStatus::OutOfDate;
    }
    if (result == VK_ERROR_SURFACE_LOST_KHR)
    {
        return SwapchainStatus::SurfaceLost;
    }
    if (result != VK_SUBOPTIMAL_KHR)
    {
        CheckVk(result, "vkQueuePresentKHR");
    }
    return SwapchainStatus::Ok;
}

} // namespace mengine::rhi
