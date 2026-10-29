#pragma once

#include <glm/mat4x4.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

#include <cstddef>
#include <cstdint>

namespace mengine::render {

// Mirrors source/shaders/common/Bindings.slang (and the push constants of Tonemap.slang). Member names match the Slang
// structs: render/ShaderLayoutChecks.cpp static_asserts every offset and size against slangc's reflection at build time.

struct FrameConstants
{
    glm::mat4 view{1.0f};
    glm::mat4 projection{1.0f};
    glm::mat4 viewProjection{1.0f};
    glm::mat4 skyboxInverseViewProjection{1.0f};
    glm::mat4 lightViewProjection{1.0f}; // directional shadow map, Y flipped like the camera projection
    glm::vec4 cameraPosition{0.0f};
    glm::vec4 lightPositionRadius{0.0f};
    glm::vec4 lightColorIntensity{0.0f};
    glm::vec4 directionalDirection{0.0f};      // xyz normalized direction the light travels
    glm::vec4 directionalColorIntensity{0.0f}; // rgb color, w intensity
    glm::vec4 shadowParams{0.0f};              // x enabled, y 1 / shadow map size, z normal offset in world units
    glm::vec4 iblParams{0.0f};                 // x intensity, y specular mip count, z skybox blur roughness
};

constexpr uint32_t kMaterialFlagNormalTexture = 1u << 0;
// KHR_materials_pbrSpecularGlossiness: baseColorFactor / texture hold diffuse, the metallic-roughness slot holds the
// specular (RGB) + glossiness (A) texture and specularGlossinessFactor its factors.
constexpr uint32_t kMaterialFlagSpecularGlossiness = 1u << 1;

struct MaterialData
{
    glm::vec4 baseColorFactor{1.0f};
    glm::vec4 emissiveFactor{0.0f};
    float metallicFactor = 1.0f;
    float roughnessFactor = 1.0f;
    float normalScale = 1.0f;
    float occlusionStrength = 1.0f;
    float alphaCutoff = 0.5f;
    uint32_t flags = 0;
    float padding[2] = {};
    glm::vec4 specularGlossinessFactor{1.0f}; // rgb specular, a glossiness
};

// Push constants of the forward pass (128 bytes, the guaranteed minimum).
struct DrawConstants
{
    glm::mat4 model{1.0f};
    glm::mat4 normalMatrix{1.0f};
};

// Push constants of Tonemap.slang.
struct TonemapConstants
{
    float exposure = 1.0f;
    uint32_t encodeSrgb = 0;
};

// Push constants of DebugLine.slang.
struct DebugLineConstants
{
    float exposure = 1.0f;
};

// Material set (set 1) bindings, in Bindings.slang order.
enum MaterialBinding : uint32_t
{
    kMaterialBindingData = 0,
    kMaterialBindingBaseColor = 1,
    kMaterialBindingMetallicRoughness = 2,
    kMaterialBindingNormal = 3,
    kMaterialBindingOcclusion = 4,
    kMaterialBindingEmissive = 5,
};

enum class AlphaMode : uint8_t
{
    Opaque,
    Mask,
    Blend,
};

// Pipeline variant of a draw; ShadowPass ignores kPipelineKeyTangents.
enum PipelineKeyBits : uint32_t
{
    kPipelineKeyAlphaMask = 1u << 0,
    kPipelineKeyDoubleSided = 1u << 1,
    // Node transform with a negative determinant: glTF requires the winding to be reversed.
    kPipelineKeyMirrored = 1u << 2,
    kPipelineKeyTangents = 1u << 3,
    kPipelineKeyCount = 1u << 4,
};

struct PointLight
{
    glm::vec3 position{0.0f, 3.0f, 0.0f};
    float radius = 20.0f; // lightRadius of eq. 9
    glm::vec3 color{1.0f};
    float intensity = 50.0f;
};

struct DirectionalLight
{
    glm::vec3 direction{-0.3f, -1.0f, -0.2f}; // direction the light travels; normalized when used
    glm::vec3 color{1.0f};
    float intensity = 3.0f; // illuminance on a surface facing the light
    bool castShadows = true;
    float normalOffsetTexels = 1.5f; // shadow receiver offset along the geometric normal, in shadow map texels
};

struct RenderSettings
{
    // Vulkan-glTF-PBR's default for its Uncharted 2 curve.
    float exposure = 4.5f;
    float iblIntensity = 1.0f;
    // Prefiltered specular cube roughness the skybox samples. Vulkan-glTF-PBR samples lod 1.5 of a 10 mip cube, i.e.
    // roughness 1.5 / 9; 0 shows the sharp source environment.
    float skyboxBlur = 1.5f / 9.0f;
    PointLight light;
    DirectionalLight directional;
    // CPU frustum culling of the forward draws (render/Culling.hpp).
    bool frustumCulling = true;
    // Keeps culling with the camera of the frame it was frozen in, so the culled draws can be inspected from outside.
    bool freezeCulling = false;
    // Debug lines the renderer adds itself.
    bool debugSceneBounds = false;
    bool debugShadowFrustum = false;
    // Every draw's world AABB: visible ones green, culled ones red; plus the culling frustum while it is frozen.
    bool debugDrawBounds = false;
};

struct CullingStats
{
    uint32_t draws = 0;
    uint32_t visible = 0;
    bool frozen = false;
};

} // namespace mengine::render
