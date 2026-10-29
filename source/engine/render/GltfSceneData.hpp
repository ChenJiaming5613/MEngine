#pragma once

#include "render/RenderTypes.hpp"
#include "render/Scene.hpp"
#include "rhi/GpuResources.hpp"

#include <glm/vec3.hpp>

#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace mengine::render {

// The CPU result of ParseGltfScene: everything GltfSceneUploader needs, without any GPU object. Immutable after
// parsing, so PrepareGltfTexture may read it from several threads while the main thread uploads.

// Owns the tinygltf model (defined in GltfLoader.cpp).
struct GltfParsedModel;

struct GltfBufferData
{
    VkBufferUsageFlags usage = 0;
    std::string name;
    // A glTF buffer view used as is (points into GltfSceneData::model), or null with the data converted on the CPU.
    std::span<const uint8_t> modelBytes;
    std::vector<uint8_t> convertedBytes;

    std::span<const uint8_t> Bytes() const
    {
        return modelBytes.data() != nullptr ? modelBytes : std::span<const uint8_t>(convertedBytes);
    }
};

struct GltfSamplerData
{
    rhi::SamplerCreation creation; // creation.name is set from `name` when the sampler is created
    std::string name;
};

// One (image, color space) pair: an image sampled as sRGB and as linear becomes two textures.
struct GltfTextureRequest
{
    uint32_t image = 0;
    bool srgb = false;
    std::string label; // "image N 'name'", for log messages and the texture name
};

constexpr uint32_t kGltfMaterialTextureCount = kMaterialBindingEmissive - kMaterialBindingBaseColor + 1;

struct GltfTextureSlot
{
    int32_t request = -1; // into textureRequests; -1 (or a failed texture) binds the dummy texture
    int32_t sampler = -1; // into samplers, valid whenever request is
};

struct GltfMaterialData
{
    std::string name;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    bool transmissive = false;
    // Without kMaterialFlagNormalTexture: the uploader sets it once it knows the normal texture was created.
    MaterialData constants;
    // Indexed by material binding - kMaterialBindingBaseColor.
    std::array<GltfTextureSlot, kGltfMaterialTextureCount> textures{};
};

struct GltfSceneData
{
    std::string path;
    std::string baseDirectory;
    bool bcSupported = false;
    std::chrono::steady_clock::time_point loadStart;

    std::unique_ptr<GltfParsedModel> model;

    // Uploaded in this order, so an index into this list is also the index into Scene::buffers.
    std::vector<GltfBufferData> buffers;
    std::vector<GltfSamplerData> samplers;
    std::vector<GltfTextureRequest> textureRequests;
    std::vector<GltfMaterialData> materials;
    // Buffer references are indices into `buffers`.
    std::vector<ScenePrimitive> primitives;
    std::vector<SceneDraw> draws; // sorted like Scene::draws
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};

    GltfSceneData();
    ~GltfSceneData();
    GltfSceneData(const GltfSceneData&) = delete;
    GltfSceneData& operator=(const GltfSceneData&) = delete;
};

} // namespace mengine::render
