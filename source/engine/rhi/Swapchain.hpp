#pragma once

#include "rhi/VulkanUtils.hpp"

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace mengine::rhi {

enum class SwapchainStatus : uint8_t
{
    Ok,
    OutOfDate,   // recreate before the next frame
    SurfaceLost, // the native window went away (Android background); surface and swapchain must be rebuilt
};

struct SwapchainContext
{
    SDL_Window* window = nullptr;
    VkInstance instance = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkDevice device = VK_NULL_HANDLE;
    uint32_t queueFamily = 0;
};

// Surface, swapchain images, per-image render-finished semaphores and framebuffers for the swapchain pass.
class Swapchain
{
public:
    void Init(const SwapchainContext& context) { m_context = context; }

    void CreateSurface();
    void DestroySurface();
    bool HasSurface() const { return m_surface != VK_NULL_HANDLE; }
    VkSurfaceKHR Surface() const { return m_surface; }

    // Picks the format once; the swapchain render pass is built for it.
    VkSurfaceFormatKHR ChooseSurfaceFormat() const;
    void SetSurfaceFormat(VkSurfaceFormatKHR format) { m_surfaceFormat = format; }
    VkSurfaceFormatKHR SurfaceFormat() const { return m_surfaceFormat; }

    // Returns false when the window has no area (minimized); the caller retries later. Needs an idle device.
    bool Create(VkRenderPass renderPass, bool vsync);
    void Destroy();
    bool IsValid() const { return m_swapchain != VK_NULL_HANDLE; }

    SwapchainStatus Acquire(VkSemaphore imageAvailable, uint32_t& imageIndex);
    SwapchainStatus Present(VkQueue queue, uint32_t imageIndex);

    VkSemaphore RenderFinishedSemaphore(uint32_t imageIndex) const { return m_renderFinishedSemaphores[imageIndex]; }
    VkFramebuffer Framebuffer(uint32_t imageIndex) const { return m_framebuffers[imageIndex]; }
    VkExtent2D Extent() const { return m_extent; }
    VkPresentModeKHR PresentMode() const { return m_presentMode; }
    uint32_t ImageCount() const { return static_cast<uint32_t>(m_images.size()); }

private:
    VkExtent2D ChooseExtent(const VkSurfaceCapabilitiesKHR& capabilities) const;
    VkPresentModeKHR ChoosePresentMode(bool vsync) const;

    SwapchainContext m_context;
    VkSurfaceKHR m_surface = VK_NULL_HANDLE;
    VkSurfaceFormatKHR m_surfaceFormat = {};
    VkSwapchainKHR m_swapchain = VK_NULL_HANDLE;
    VkExtent2D m_extent = {0, 0};
    VkPresentModeKHR m_presentMode = VK_PRESENT_MODE_FIFO_KHR;
    std::vector<VkImage> m_images;
    std::vector<VkImageView> m_imageViews;
    std::vector<VkSemaphore> m_renderFinishedSemaphores;
    std::vector<VkFramebuffer> m_framebuffers;
};

const char* PresentModeName(VkPresentModeKHR mode);

} // namespace mengine::rhi
