#pragma once

#include <SDL3/SDL_log.h>

// Thin wrappers over SDL logging (logcat on Android, stderr / debugger on desktop).
#define MENGINE_LOG_INFO(...) SDL_LogInfo(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define MENGINE_LOG_WARN(...) SDL_LogWarn(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
#define MENGINE_LOG_ERROR(...) SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, __VA_ARGS__)
