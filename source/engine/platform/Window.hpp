#pragma once

#include <cstdint>

struct SDL_Window;

namespace mengine {

struct WindowCreation
{
    const char* title = "MEngine";
    int width = 1280;
    int height = 720;
};

class Window
{
public:
    Window() = default;
    ~Window();
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;

    bool Create(const WindowCreation& creation);
    void Destroy();

    SDL_Window* NativeHandle() const { return m_window; }

    void GetPixelSize(uint32_t& width, uint32_t& height) const;
    void GetSize(int& width, int& height) const;
    float DisplayScale() const;
    bool IsMinimized() const;

    void SetRelativeMouseMode(bool enabled);

private:
    SDL_Window* m_window = nullptr;
};

} // namespace mengine
