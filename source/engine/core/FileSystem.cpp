#include "core/FileSystem.hpp"

#include "core/Log.hpp"

#include <SDL3/SDL_filesystem.h>
#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_stdinc.h>

#include <memory>

namespace mengine {

std::string ResolveAssetPath(std::string_view relativePath)
{
#if defined(__ANDROID__)
    // Relative paths are resolved against the APK assets by SDL.
    return std::string(relativePath);
#else
    const char* basePath = SDL_GetBasePath();
    return std::string(basePath != nullptr ? basePath : "") + "assets/" + std::string(relativePath);
#endif
}

std::optional<std::vector<uint8_t>> ReadFileBytes(const std::string& resolvedPath)
{
    size_t size = 0;
    std::unique_ptr<void, decltype(&SDL_free)> data(SDL_LoadFile(resolvedPath.c_str(), &size), &SDL_free);
    if (data == nullptr)
    {
        MENGINE_LOG_ERROR("Failed to read %s: %s", resolvedPath.c_str(), SDL_GetError());
        return std::nullopt;
    }
    const auto* bytes = static_cast<const uint8_t*>(data.get());
    return std::vector<uint8_t>(bytes, bytes + size);
}

std::optional<std::vector<uint8_t>> ReadAssetFile(std::string_view relativePath)
{
    return ReadFileBytes(ResolveAssetPath(relativePath));
}

std::optional<std::string> GetUserDataDirectory()
{
    std::unique_ptr<char, decltype(&SDL_free)> path(SDL_GetPrefPath("MEngine", "MEngine"), &SDL_free);
    if (path == nullptr)
    {
        MENGINE_LOG_WARN("No user data directory: %s", SDL_GetError());
        return std::nullopt;
    }
    return std::string(path.get());
}

bool FileExists(const std::string& path)
{
    SDL_PathInfo info{};
    return SDL_GetPathInfo(path.c_str(), &info) && info.type == SDL_PATHTYPE_FILE;
}

bool WriteFileAtomic(const std::string& path, std::span<const uint8_t> data)
{
    const std::string temporaryPath = path + ".tmp";
    SDL_IOStream* stream = SDL_IOFromFile(temporaryPath.c_str(), "wb");
    if (stream == nullptr)
    {
        MENGINE_LOG_WARN("Failed to open %s for writing: %s", temporaryPath.c_str(), SDL_GetError());
        return false;
    }
    const size_t written = data.empty() ? 0 : SDL_WriteIO(stream, data.data(), data.size());
    // Closing flushes; a failed flush means the file is incomplete.
    const bool closed = SDL_CloseIO(stream);
    if (written != data.size() || !closed)
    {
        MENGINE_LOG_WARN("Failed to write %s: %s", temporaryPath.c_str(), SDL_GetError());
        SDL_RemovePath(temporaryPath.c_str());
        return false;
    }
    // SDL renames with MoveFileEx(REPLACE_EXISTING) on Windows and rename() on POSIX: both replace the target.
    if (!SDL_RenamePath(temporaryPath.c_str(), path.c_str()))
    {
        MENGINE_LOG_WARN("Failed to rename %s to %s: %s", temporaryPath.c_str(), path.c_str(), SDL_GetError());
        SDL_RemovePath(temporaryPath.c_str());
        return false;
    }
    return true;
}

std::string_view ParentPath(std::string_view path)
{
    const size_t separator = path.find_last_of("/\\");
    return separator == std::string_view::npos ? std::string_view() : path.substr(0, separator);
}

std::string JoinPath(std::string_view base, std::string_view relative)
{
    if (base.empty())
    {
        return std::string(relative);
    }
    std::string joined(base);
    if (joined.back() != '/' && joined.back() != '\\')
    {
        joined.push_back('/');
    }
    joined.append(relative);
    return joined;
}

bool IsSafeRelativePath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\')
    {
        return false;
    }
    if (path.size() >= 2 && path[1] == ':')
    {
        return false;
    }

    size_t segmentStart = 0;
    while (segmentStart <= path.size())
    {
        size_t segmentEnd = path.find_first_of("/\\", segmentStart);
        if (segmentEnd == std::string_view::npos)
        {
            segmentEnd = path.size();
        }
        if (path.substr(segmentStart, segmentEnd - segmentStart) == "..")
        {
            return false;
        }
        segmentStart = segmentEnd + 1;
    }
    return true;
}

} // namespace mengine
