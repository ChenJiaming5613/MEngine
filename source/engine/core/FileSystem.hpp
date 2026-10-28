#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace mengine {

// Asset paths are relative to the staged asset root: APK assets on Android, <exe dir>/assets on desktop.
std::string ResolveAssetPath(std::string_view relativePath);

// Reads a file through SDL so APK assets work too. Logs and returns nullopt on failure.
std::optional<std::vector<uint8_t>> ReadFileBytes(const std::string& resolvedPath);
std::optional<std::vector<uint8_t>> ReadAssetFile(std::string_view relativePath);

// "a/b/c.gltf" -> "a/b", "c.gltf" -> "".
std::string_view ParentPath(std::string_view path);
std::string JoinPath(std::string_view base, std::string_view relative);

// The rule shared by config.json validation at configure time and at runtime:
// non-empty, not absolute (no leading slash, no drive letter) and no ".." segment.
bool IsSafeRelativePath(std::string_view path);

} // namespace mengine
