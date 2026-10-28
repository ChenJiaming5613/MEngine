#pragma once

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <string_view>

namespace mengine {

struct CameraConfig
{
    glm::vec3 position{0.0f};
    float yawDegrees = 0.0f;
    float pitchDegrees = 0.0f;
    float fovYDegrees = 60.0f;
};

// assets/config.json. "scene" is relative to models/, "environment" to the asset root.
struct ViewerConfig
{
    std::string scenePath;                      // "models/<scene>"
    std::optional<std::string> environmentPath; // e.g. "environments/default"; absent: uniform environment
    std::optional<CameraConfig> camera;          // absent: frame the scene bounds
};

// Logs the reason and returns nullopt when the file is missing or invalid.
std::optional<ViewerConfig> LoadViewerConfig(std::string_view assetPath);

} // namespace mengine
