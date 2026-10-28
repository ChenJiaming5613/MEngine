#pragma once

#include <SDL3/SDL_events.h>

namespace mengine {
class Window;
}

namespace mengine::rhi {
class CommandBuffer;
class GpuDevice;
} // namespace mengine::rhi

namespace mengine::render {

// Dear ImGui with the official SDL3 and Vulkan backends, drawing inside the device's swapchain pass.
class ImGuiLayer
{
public:
    // Throws std::runtime_error when a backend fails to initialize.
    void Init(rhi::GpuDevice& device, Window& window);
    void Shutdown();

    bool IsReady() const { return m_initialized; }
    void ProcessEvent(const SDL_Event& event);

    void NewFrame();
    // Finalizes the frame; call before recording so it is safe to skip Render when the frame is dropped.
    void EndFrame();
    // Must be called inside the swapchain pass.
    void Render(rhi::CommandBuffer& commandBuffer);

    bool WantsMouse() const;
    bool WantsKeyboard() const;

private:
    bool m_initialized = false;
    bool m_platformInitialized = false;
    bool m_rendererInitialized = false;
};

} // namespace mengine::render
