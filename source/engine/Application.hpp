#pragma once

#include "core/Time.hpp"
#include "platform/Input.hpp"
#include "platform/Window.hpp"

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_init.h>
#include <TaskScheduler.h>

namespace mengine {

// Drives the SDL main callbacks. Subclasses get no OnFrame while suspended (Android background) or minimized.
class Application
{
public:
    Application() = default;
    virtual ~Application() = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    bool Initialize(const WindowCreation& windowCreation);
    void Shutdown();

    SDL_AppResult Iterate();
    SDL_AppResult HandleEvent(const SDL_Event& event);

protected:
    virtual bool OnInitialize() = 0;
    virtual void OnShutdown() = 0;
    // Exceptions thrown here end the application with SDL_APP_FAILURE.
    virtual void OnFrame(double deltaSeconds) = 0;
    virtual SDL_AppResult OnEvent(const SDL_Event&) { return SDL_APP_CONTINUE; }
    virtual void OnSuspendChanged(bool) {}

    Window& GetWindow() { return m_window; }
    Input& GetInput() { return m_input; }
    // Worker threads for CPU work only: tasks must not touch GpuDevice (see Architecture.md, threading).
    enki::TaskScheduler& GetTaskScheduler() { return m_tasks; }
    bool IsSuspended() const { return m_suspended; }

private:
    Window m_window;
    Input m_input;
    FrameClock m_clock;
    enki::TaskScheduler m_tasks;
    bool m_tasksRunning = false;
    bool m_initialized = false;
    bool m_suspended = false;
};

} // namespace mengine
