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

// Read* helpers: absent gives nullopt with *ok unchanged; invalid logs and clears *ok.
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

// Required: absent counts as invalid.
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

std::optional<CameraConfig> ReadCamera(JsonValue camera, const std::string& prefix, const std::string& path, bool* ok)
{
    if (!camera.IsValid())
    {
        return std::nullopt;
    }
    if (!camera.IsObject())
    {
        MENGINE_LOG_ERROR("%s: \"%s\" must be an object", path.c_str(), prefix.c_str());
        *ok = false;
        return std::nullopt;
    }
    auto name = [&](const char* member) { return prefix + "." + member; };
    CameraConfig cameraConfig;
    cameraConfig.position = ReadVec3(camera["position"], name("position").c_str(), path, ok).value_or(glm::vec3(0.0f));
    cameraConfig.yawDegrees = ReadNumber(camera["yawDegrees"], name("yawDegrees").c_str(), path, ok).value_or(0.0f);
    cameraConfig.pitchDegrees = ReadNumber(camera["pitchDegrees"], name("pitchDegrees").c_str(), path, ok).value_or(0.0f);
    cameraConfig.fovYDegrees = ReadNumber(camera["fovYDegrees"], name("fovYDegrees").c_str(), path, ok).value_or(60.0f);
    if (cameraConfig.fovYDegrees <= 1.0f || cameraConfig.fovYDegrees >= 179.0f)
    {
        MENGINE_LOG_ERROR("%s: \"%s\" must be in (1, 179)", path.c_str(), name("fovYDegrees").c_str());
        *ok = false;
    }
    return cameraConfig;
}

std::string FileStem(std::string_view path)
{
    const size_t slash = path.find_last_of('/');
    std::string_view name = slash == std::string_view::npos ? path : path.substr(slash + 1);
    const size_t dot = name.find_last_of('.');
    return std::string(dot == std::string_view::npos || dot == 0 ? name : name.substr(0, dot));
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
    const JsonValue scenes = root["scenes"];
    if (!scenes.IsArray() || scenes.Size() == 0)
    {
        MENGINE_LOG_ERROR("%s: \"scenes\" must be a non-empty array of {\"name\", \"path\", \"camera\"} objects", path.c_str());
        return std::nullopt;
    }
    for (size_t i = 0; i < scenes.Size(); ++i)
    {
        const JsonValue scene = scenes.At(i);
        const std::string prefix = "scenes[" + std::to_string(i) + "]";
        if (!scene.IsObject())
        {
            MENGINE_LOG_ERROR("%s: \"%s\" must be an object", path.c_str(), prefix.c_str());
            ok = false;
            continue;
        }
        SceneConfig sceneConfig;
        const std::optional<std::string> scenePath = ReadRelativePath(scene["path"], (prefix + ".path").c_str(), path, &ok);
        if (!scenePath)
        {
            continue;
        }
        sceneConfig.path = JoinPath("models", *scenePath);
        sceneConfig.name = FileStem(*scenePath);
        if (scene["name"].IsValid())
        {
            const std::optional<std::string_view> name = scene["name"].AsString();
            if (!name || name->empty())
            {
                MENGINE_LOG_ERROR("%s: \"%s.name\" must be a non-empty string", path.c_str(), prefix.c_str());
                ok = false;
            }
            else
            {
                sceneConfig.name = std::string(*name);
            }
        }
        sceneConfig.camera = ReadCamera(scene["camera"], prefix + ".camera", path, &ok);
        config.scenes.push_back(std::move(sceneConfig));
    }
    if (root["environment"].IsValid())
    {
        if (const std::optional<std::string> environment = ReadRelativePath(root["environment"], "environment", path, &ok))
        {
            config.environmentPath = *environment;
        }
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
