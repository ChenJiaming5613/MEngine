#include "ViewerConfig.hpp"

#include "core/FileSystem.hpp"
#include "core/Json.hpp"
#include "core/Log.hpp"

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
        const JsonValue position = camera["position"];
        if (position.IsValid())
        {
            if (!position.IsArray() || position.Size() != 3)
            {
                MENGINE_LOG_ERROR("%s: \"camera.position\" must be an array of three numbers", path.c_str());
                return std::nullopt;
            }
            for (size_t i = 0; i < 3; ++i)
            {
                if (const std::optional<float> value = ReadNumber(position.At(i), "camera.position", path, &ok))
                {
                    cameraConfig.position[static_cast<glm::length_t>(i)] = *value;
                }
                else if (ok)
                {
                    MENGINE_LOG_ERROR("%s: \"camera.position\" must be an array of three numbers", path.c_str());
                    ok = false;
                }
            }
        }
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

    if (!ok)
    {
        return std::nullopt;
    }
    return config;
}

} // namespace mengine
