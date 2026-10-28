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

// Absent members keep the defaults of render::DirectionalLight.
struct DirectionalLightConfig
{
    std::optional<glm::vec3> direction; // direction the light travels, non-zero, normalized on load
    std::optional<glm::vec3> color;     // non-negative
    std::optional<float> intensity;     // non-negative
};

// assets/config.json. "scene" is relative to models/, "environment" to the asset root.
struct ViewerConfig
{
    std::string scenePath;                      // "models/<scene>"
    std::optional<std::string> environmentPath; // e.g. "environments/papermill"; absent: uniform environment
    std::optional<CameraConfig> camera;          // absent: frame the scene bounds
    std::optional<DirectionalLightConfig> directionalLight;
};

// Logs the reason and returns nullopt when the file is missing or invalid.
std::optional<ViewerConfig> LoadViewerConfig(std::string_view assetPath);

} // namespace mengine
