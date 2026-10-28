#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL_main.h>
#include <SDL3/SDL_hints.h>

#include "ViewerApp.hpp"

SDL_AppResult SDL_AppInit(void** appState, int, char*[])
{
    SDL_SetAppMetadata("MEngine", "0.1.0", "com.mengine.app");
    // Fingers reach the camera as touch events only. Android keeps touch -> mouse so ImGui receives taps; the
    // desktop drops it so touchscreens do not double as a mouse.
#if defined(__ANDROID__)
    SDL_SetHint(SDL_HINT_MOUSE_TOUCH_EVENTS, "0");
#else
    SDL_SetHint(SDL_HINT_TOUCH_MOUSE_EVENTS, "0");
#endif

    std::unique_ptr<mengine::Application> application = mengine::CreateViewerApp();
    const bool initialized = application->Initialize(mengine::WindowCreation{.title = "MEngine", .width = 1280, .height = 720});
    *appState = application.release();
    return initialized ? SDL_APP_CONTINUE : SDL_APP_FAILURE;
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
    auto* application = static_cast<mengine::Application*>(appState);
    if (application != nullptr)
    {
        application->Shutdown();
        delete application;
    }
}
