#pragma once

#include "render/RenderTypes.hpp"
#include "rhi/GpuResources.hpp"

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>

namespace mengine::rhi {
class GpuDevice;
}

namespace mengine::render {

// Vertex streams are separate buffers (non-interleaved), bound in this order.
enum VertexStreamIndex : uint32_t
{
    kVertexStreamPosition = 0, // float3
    kVertexStreamNormal = 1,   // float3
    kVertexStreamTexCoord = 2, // float2, TEXCOORD_0
    kVertexStreamTangent = 3,  // float4, only when hasTangents
    kVertexStreamCount = 4,
};

struct ScenePrimitive
{
    std::array<rhi::BufferHandle, kVertexStreamCount> vertexBuffers{};
    std::array<VkDeviceSize, kVertexStreamCount> vertexOffsets{};
    bool hasTangents = false;

    rhi::BufferHandle indexBuffer;
    VkDeviceSize indexOffset = 0;
    VkIndexType indexType = VK_INDEX_TYPE_UINT32;
    uint32_t indexCount = 0;

    uint32_t material = 0;
    glm::vec3 boundsMin{0.0f};
    glm::vec3 boundsMax{0.0f};
};

struct SceneMaterial
{
    std::string name;
    rhi::BufferHandle uniformBuffer;
    rhi::DescriptorSetHandle descriptorSet;
    AlphaMode alphaMode = AlphaMode::Opaque;
    bool doubleSided = false;
    // KHR_materials_transmission (glass): refraction is not implemented and an opaque pane would seal windows and
    // block the sun, so such primitives get no draws.
    bool transmissive = false;
};

struct SceneDraw
{
    uint32_t primitive = 0;
    uint32_t material = 0;
    uint32_t pipelineKey = 0;
    DrawConstants constants;
    // World space AABB of the primitive's bounds under the draw's transform; empty (min > max) when unknown, which
    // culling treats as always visible.
    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};
};

// GPU side of a loaded glTF scene. Draws are sorted by pipeline (opaque before mask), then material.
struct Scene
{
    std::string path;

    std::vector<rhi::BufferHandle> buffers;
    std::vector<rhi::TextureHandle> textures;
    std::vector<rhi::SamplerHandle> samplers;

    std::vector<ScenePrimitive> primitives;
    std::vector<SceneMaterial> materials;
    std::vector<SceneDraw> draws;

    glm::vec3 boundsMin{std::numeric_limits<float>::max()};
    glm::vec3 boundsMax{std::numeric_limits<float>::lowest()};

    bool IsEmpty() const { return draws.empty(); }
    bool HasBounds() const { return boundsMin.x <= boundsMax.x; }

    // Releases every GPU resource (deferred by the device) and leaves an empty scene.
    void Destroy(rhi::GpuDevice& device);
};

} // namespace mengine::render
