#pragma once

#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace mengine {

// Asset paths are relative to the staged asset root: APK assets on Android, <exe dir>/assets on desktop.
std::string ResolveAssetPath(std::string_view relativePath);

// Reads a file through SDL so APK assets work too. Logs and returns nullopt on failure. Thread safe once the first file
// has been read on the main thread: on Android SDL creates its asset manager lazily, without a lock.
std::optional<std::vector<uint8_t>> ReadFileBytes(const std::string& resolvedPath);
std::optional<std::vector<uint8_t>> ReadAssetFile(std::string_view relativePath);

// Writable per-user directory (SDL_GetPrefPath: %APPDATA%/MEngine/MEngine on Windows, internal storage on Android),
// created on demand and ending with a separator. nullopt when the platform has none.
std::optional<std::string> GetUserDataDirectory();
// Regular files only; never logs.
bool FileExists(const std::string& path);
// Writes path.tmp and renames it over path, so an interrupted write never leaves a truncated file. Logs on failure.
bool WriteFileAtomic(const std::string& path, std::span<const uint8_t> data);

// "a/b/c.gltf" -> "a/b", "c.gltf" -> "".
std::string_view ParentPath(std::string_view path);
std::string JoinPath(std::string_view base, std::string_view relative);

// Same rule as the configure-time config.json check: non-empty, not absolute (no leading slash or drive letter),
// no ".." segment.
bool IsSafeRelativePath(std::string_view path);

} // namespace mengine
