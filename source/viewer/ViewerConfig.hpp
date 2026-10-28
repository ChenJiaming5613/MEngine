#pragma once

#include <glm/vec3.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

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

struct SceneConfig
{
    std::string name;                   // shown in the scene list; defaults to the file name without extension
    std::string path;                   // "models/<path>", path relative to third_party/ in config.json
    std::optional<CameraConfig> camera; // absent: frame the scene bounds
};

// assets/config.json. Scene paths are relative to models/ (third_party/ in the source tree), "environment" to the
// asset root. The first scene is loaded at startup.
struct ViewerConfig
{
    std::vector<SceneConfig> scenes;            // at least one
    std::optional<std::string> environmentPath; // e.g. "environments/papermill"; absent: uniform environment
    std::optional<DirectionalLightConfig> directionalLight;
};

// Logs the reason and returns nullopt when the file is missing or invalid.
std::optional<ViewerConfig> LoadViewerConfig(std::string_view assetPath);

} // namespace mengine
