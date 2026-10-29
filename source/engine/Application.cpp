#include "Application.hpp"

#include "core/Log.hpp"

#include <SDL3/SDL_timer.h>

#include <exception>

namespace mengine {

namespace {

constexpr uint32_t kIdleDelayMilliseconds = 16;

} // namespace

bool Application::Initialize(const WindowCreation& windowCreation)
{
    if (!SDL_Init(SDL_INIT_VIDEO))
    {
        MENGINE_LOG_ERROR("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    if (!m_window.Create(windowCreation))
    {
        return false;
    }
    // One thread is the main thread, which also runs tasks while it waits for them.
    m_tasks.Initialize();
    m_tasksRunning = true;

    try
    {
        if (!OnInitialize())
        {
            return false;
        }
    }
    catch (const std::exception& error)
    {
        MENGINE_LOG_ERROR("Initialization failed: %s", error.what());
        return false;
    }

    m_initialized = true;
    m_clock.Reset();
    return true;
}

void Application::Shutdown()
{
    try
    {
        OnShutdown();
    }
    catch (const std::exception& error)
    {
        MENGINE_LOG_ERROR("Shutdown failed: %s", error.what());
    }
    // OnShutdown has waited for (or cancelled) its own tasks; this only joins the worker threads.
    if (m_tasksRunning)
    {
        m_tasks.WaitforAllAndShutdown();
        m_tasksRunning = false;
    }
    m_initialized = false;
    m_window.Destroy();
}

SDL_AppResult Application::Iterate()
{
    const double deltaSeconds = m_clock.Tick();
    if (!m_initialized || m_suspended || m_window.IsMinimized())
    {
        SDL_Delay(kIdleDelayMilliseconds);
        return SDL_APP_CONTINUE;
    }

    try
    {
        OnFrame(deltaSeconds);
    }
    catch (const std::exception& error)
    {
        MENGINE_LOG_ERROR("Frame failed: %s", error.what());
        return SDL_APP_FAILURE;
    }
    m_input.EndFrame();
    return SDL_APP_CONTINUE;
}

SDL_AppResult Application::HandleEvent(const SDL_Event& event)
{
    if (event.type == SDL_EVENT_QUIT)
    {
        return SDL_APP_SUCCESS;
    }

    m_input.HandleEvent(event, m_window);

    try
    {
        // The Android native window and its VkSurfaceKHR go away while in the background.
        if (event.type == SDL_EVENT_WILL_ENTER_BACKGROUND || event.type == SDL_EVENT_DID_ENTER_FOREGROUND)
        {
            const bool suspended = event.type == SDL_EVENT_WILL_ENTER_BACKGROUND;
            if (suspended != m_suspended)
            {
                m_suspended = suspended;
                if (m_initialized)
                {
                    OnSuspendChanged(suspended);
                }
                m_clock.Reset();
            }
        }
        return m_initialized ? OnEvent(event) : SDL_APP_CONTINUE;
    }
    catch (const std::exception& error)
    {
        MENGINE_LOG_ERROR("Event handling failed: %s", error.what());
        return SDL_APP_FAILURE;
    }
}

} // namespace mengine
