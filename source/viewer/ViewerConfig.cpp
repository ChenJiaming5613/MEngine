#include "ViewerConfig.hpp"

#include "core/FileSystem.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"

#include <glm/geometric.hpp>
#include <glm/vector_relational.hpp>

#include <cmath>
#include <string>
#include <vector>

namespace mengine {
namespace {

std::optional<float> ReadNumber(JsonValue value, const char* name, const std::string& path, bool* ok)
{
    if (!value.IsValid())
    {
        return std::nullopt;
    }
    const std::optional<double> number = value.AsNumber();
    if (!number || !std::isfinite(*number))
    {
        MENGINE_LOG_ERROR("%s: \"%s\" must be a number", path.c_str(), name);
        *ok = false;
        return std::nullopt;
    }
    return static_cast<float>(*number);
}

// Absent: nullopt with ok unchanged. Present but not three numbers: logs, clears ok and returns nullopt.
std::optional<glm::vec3> ReadVec3(JsonValue value, const char* name, const std::string& path, bool* ok)
{
    if (!value.IsValid())
    {
        return std::nullopt;
    }
    glm::vec3 result(0.0f);
    bool valid = value.IsArray() && value.Size() == 3;
    for (size_t i = 0; valid && i < 3; ++i)
    {
        const std::optional<double> number = value.At(i).AsNumber();
        valid = number && std::isfinite(*number);
        if (valid)
        {
            result[static_cast<glm::length_t>(i)] = static_cast<float>(*number);
        }
    }
    if (!valid)
    {
        MENGINE_LOG_ERROR("%s: \"%s\" must be an array of three numbers", path.c_str(), name);
        *ok = false;
        return std::nullopt;
    }
    return result;
}

std::optional<std::string> ReadRelativePath(JsonValue value, const char* name, const std::string& path, bool* ok)
{
    const std::optional<std::string_view> text = value.AsString();
    if (!text || text->empty() || !IsSafeRelativePath(*text))
    {
        MENGINE_LOG_ERROR("%s: \"%s\" must be a non-empty relative path without \"..\"", path.c_str(), name);
        *ok = false;
        return std::nullopt;
    }
    return std::string(*text);
}

} // namespace

std::optional<ViewerConfig> LoadViewerConfig(std::string_view assetPath)
{
    const std::string path(assetPath);
    const std::optional<std::vector<uint8_t>> bytes = ReadAssetFile(path);
    if (!bytes)
    {
        MENGINE_LOG_ERROR("%s not found", path.c_str());
        return std::nullopt;
    }
    std::string error;
    const std::optional<JsonDocument> document =
        JsonDocument::Parse(std::string_view(reinterpret_cast<const char*>(bytes->data()), bytes->size()), &error);
    if (!document)
    {
        MENGINE_LOG_ERROR("%s: %s", path.c_str(), error.c_str());
        return std::nullopt;
    }
    const JsonValue root = document->Root();
    if (!root.IsObject())
    {
        MENGINE_LOG_ERROR("%s: the root must be an object", path.c_str());
        return std::nullopt;
    }

    bool ok = true;
    ViewerConfig config;
    if (const std::optional<std::string> scene = ReadRelativePath(root["scene"], "scene", path, &ok))
    {
        config.scenePath = JoinPath("models", *scene);
    }
    if (root["environment"].IsValid())
    {
        if (const std::optional<std::string> environment = ReadRelativePath(root["environment"], "environment", path, &ok))
        {
            config.environmentPath = *environment;
        }
    }

    const JsonValue camera = root["camera"];
    if (camera.IsValid())
    {
        if (!camera.IsObject())
        {
            MENGINE_LOG_ERROR("%s: \"camera\" must be an object", path.c_str());
            return std::nullopt;
        }
        CameraConfig cameraConfig;
        cameraConfig.position = ReadVec3(camera["position"], "camera.position", path, &ok).value_or(glm::vec3(0.0f));
        cameraConfig.yawDegrees = ReadNumber(camera["yawDegrees"], "camera.yawDegrees", path, &ok).value_or(0.0f);
        cameraConfig.pitchDegrees = ReadNumber(camera["pitchDegrees"], "camera.pitchDegrees", path, &ok).value_or(0.0f);
        cameraConfig.fovYDegrees = ReadNumber(camera["fovYDegrees"], "camera.fovYDegrees", path, &ok).value_or(60.0f);
        if (ok && (cameraConfig.fovYDegrees <= 1.0f || cameraConfig.fovYDegrees >= 179.0f))
        {
            MENGINE_LOG_ERROR("%s: \"camera.fovYDegrees\" must be in (1, 179)", path.c_str());
            ok = false;
        }
        config.camera = cameraConfig;
    }

    const JsonValue light = root["directionalLight"];
    if (light.IsValid())
    {
        if (!light.IsObject())
        {
            MENGINE_LOG_ERROR("%s: \"directionalLight\" must be an object", path.c_str());
            return std::nullopt;
        }
        DirectionalLightConfig lightConfig;
        if (const std::optional<glm::vec3> direction = ReadVec3(light["direction"], "directionalLight.direction", path, &ok))
        {
            const float length = glm::length(*direction);
            if (length > 1e-6f)
            {
                lightConfig.direction = *direction / length;
            }
            else
            {
                MENGINE_LOG_ERROR("%s: \"directionalLight.direction\" must not be zero", path.c_str());
                ok = false;
            }
        }
        if (const std::optional<glm::vec3> color = ReadVec3(light["color"], "directionalLight.color", path, &ok))
        {
            if (glm::all(glm::greaterThanEqual(*color, glm::vec3(0.0f))))
            {
                lightConfig.color = *color;
            }
            else
            {
                MENGINE_LOG_ERROR("%s: \"directionalLight.color\" must not be negative", path.c_str());
                ok = false;
            }
        }
        if (const std::optional<float> intensity = ReadNumber(light["intensity"], "directionalLight.intensity", path, &ok))
        {
            if (*intensity >= 0.0f)
            {
                lightConfig.intensity = *intensity;
            }
            else
            {
                MENGINE_LOG_ERROR("%s: \"directionalLight.intensity\" must not be negative", path.c_str());
                ok = false;
            }
        }
        config.directionalLight = lightConfig;
    }

    if (!ok)
    {
        return std::nullopt;
    }
    return config;
}

} // namespace mengine
