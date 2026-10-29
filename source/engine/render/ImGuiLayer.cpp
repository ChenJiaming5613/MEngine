#include "render/ImGuiLayer.hpp"

#include "core/Log.hpp"
#include "platform/Window.hpp"
#include "render/RenderView.hpp"
#include "rhi/CommandBuffer.hpp"
#include "rhi/GpuDevice.hpp"

#if defined(_MSC_VER)
#pragma warning(push, 0)
#elif defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif
#include <imgui.h>
#include <imgui_impl_sdl3.h>
#include <imgui_impl_vulkan.h>
#if defined(_MSC_VER)
#pragma warning(pop)
#elif defined(__clang__)
#pragma clang diagnostic pop
#endif

#include <stdexcept>

namespace mengine::render {

namespace {

void CheckImGuiVkResult(VkResult result)
{
    if (result < 0)
    {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "ImGui Vulkan backend error: VkResult %d", static_cast<int>(result));
    }
}

} // namespace

void ImGuiLayer::Init(rhi::GpuDevice& device, Window& window)
{
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    ImGui::StyleColorsDark();

    const float displayScale = window.DisplayScale();
    if (displayScale > 0.0f)
    {
        ImGui::GetStyle().ScaleAllSizes(displayScale);
        ImGui::GetStyle().FontScaleDpi = displayScale;
    }
#if defined(__ANDROID__)
    ImGui::GetStyle().TouchExtraPadding = ImVec2(4.0f, 4.0f);
#endif
    m_initialized = true;

    if (!ImGui_ImplSDL3_InitForVulkan(window.NativeHandle()))
    {
        throw std::runtime_error("ImGui_ImplSDL3_InitForVulkan failed");
    }
    m_platformInitialized = true;

    // ApiVersion must match the instance, otherwise the backend assumes the header version and uses newer entry points.
    ImGui_ImplVulkan_InitInfo initInfo = {};
    initInfo.ApiVersion = VK_API_VERSION_1_1;
    initInfo.Instance = device.GetInstance();
    initInfo.PhysicalDevice = device.GetPhysicalDevice();
    initInfo.Device = device.GetDevice();
    initInfo.QueueFamily = device.GetQueueFamily();
    initInfo.Queue = device.GetQueue();
    initInfo.PipelineCache = device.GetPipelineCache();
    initInfo.DescriptorPoolSize = IMGUI_IMPL_VULKAN_MINIMUM_SAMPLED_IMAGE_POOL_SIZE;
    initInfo.MinImageCount = rhi::kMaxFramesInFlight;
    initInfo.ImageCount = rhi::kMaxFramesInFlight;
    initInfo.UseDynamicRendering = false;
    initInfo.PipelineInfoMain.RenderPass = device.GetVulkanRenderPass(device.GetSwapchainOutput());
    initInfo.PipelineInfoMain.Subpass = 0;
    initInfo.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    initInfo.CheckVkResultFn = CheckImGuiVkResult;
    if (!ImGui_ImplVulkan_Init(&initInfo))
    {
        throw std::runtime_error("ImGui_ImplVulkan_Init failed");
    }
    m_rendererInitialized = true;
}

void ImGuiLayer::Shutdown()
{
    if (m_rendererInitialized)
    {
        ImGui_ImplVulkan_Shutdown();
        m_rendererInitialized = false;
    }
    if (m_platformInitialized)
    {
        ImGui_ImplSDL3_Shutdown();
        m_platformInitialized = false;
    }
    if (m_initialized)
    {
        ImGui::DestroyContext();
        m_initialized = false;
    }
}

void ImGuiLayer::ProcessEvent(const SDL_Event& event)
{
    if (m_platformInitialized)
    {
        ImGui_ImplSDL3_ProcessEvent(&event);
    }
}

void ImGuiLayer::NewFrame()
{
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplSDL3_NewFrame();
    ImGui::NewFrame();
}

void ImGuiLayer::EndFrame()
{
    ImGui::Render();
}

void ImGuiLayer::Render(rhi::CommandBuffer& commandBuffer)
{
    commandBuffer.PushMarker("ImGui");
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), commandBuffer.Handle());
    commandBuffer.PopMarker();
}

bool ImGuiLayer::WantsMouse() const
{
    return m_initialized && ImGui::GetIO().WantCaptureMouse;
}

bool ImGuiLayer::WantsKeyboard() const
{
    return m_initialized && ImGui::GetIO().WantCaptureKeyboard;
}

void ImGuiPass::Setup(FrameGraphBuilder& builder)
{
    builder.ImportBackbuffer(kResourceBackbuffer);
    builder.ColorAttachment(kResourceBackbuffer, rhi::RenderPassOperation::Load);
}

void ImGuiPass::Execute(FrameGraphContext& context)
{
    m_layer->Render(context.commandBuffer);
}

} // namespace mengine::render
