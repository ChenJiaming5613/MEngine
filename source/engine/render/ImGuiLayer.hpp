#pragma once

#include "render/graph/FrameGraph.hpp"

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

// Frame graph node drawing an ImGuiLayer over the backbuffer (loaded, so it shares the tone mapping render pass).
class ImGuiPass final : public FrameGraphPass
{
public:
    // Null skips the pass, e.g. for applications without UI.
    void SetLayer(ImGuiLayer* layer) { m_layer = layer; }

    void Setup(FrameGraphBuilder& builder) override;
    void Execute(FrameGraphContext& context) override;
    bool IsActive() const override { return m_layer != nullptr && m_layer->IsReady(); }
    const char* Description() const override { return "Draws the Dear ImGui user interface over the backbuffer."; }

private:
    ImGuiLayer* m_layer = nullptr;
};

} // namespace mengine::render
