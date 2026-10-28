#include "platform/Window.hpp"

#include "core/Log.hpp"

#include <SDL3/SDL_mouse.h>
#include <SDL3/SDL_video.h>

namespace mengine {

Window::~Window()
{
    Destroy();
}

bool Window::Create(const WindowCreation& creation)
{
    m_window = SDL_CreateWindow(creation.title, creation.width, creation.height,
        SDL_WINDOW_VULKAN | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (m_window == nullptr)
    {
        MENGINE_LOG_ERROR("SDL_CreateWindow failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void Window::Destroy()
{
    if (m_window != nullptr)
    {
        SDL_DestroyWindow(m_window);
        m_window = nullptr;
    }
}

void Window::GetPixelSize(uint32_t& width, uint32_t& height) const
{
    int pixelWidth = 0;
    int pixelHeight = 0;
    SDL_GetWindowSizeInPixels(m_window, &pixelWidth, &pixelHeight);
    width = static_cast<uint32_t>(pixelWidth > 0 ? pixelWidth : 0);
    height = static_cast<uint32_t>(pixelHeight > 0 ? pixelHeight : 0);
}

void Window::GetSize(int& width, int& height) const
{
    SDL_GetWindowSize(m_window, &width, &height);
}

float Window::DisplayScale() const
{
    const float scale = SDL_GetWindowDisplayScale(m_window);
    return scale > 0.0f ? scale : 1.0f;
}

bool Window::IsMinimized() const
{
    return (SDL_GetWindowFlags(m_window) & SDL_WINDOW_MINIMIZED) != 0;
}

void Window::SetRelativeMouseMode(bool enabled)
{
    SDL_SetWindowRelativeMouseMode(m_window, enabled);
}

} // namespace mengine
