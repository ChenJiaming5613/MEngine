// Descriptor layout shared with source/engine/render/RenderTypes.hpp. glm matrices are uploaded as-is; Slang's default
// matrix layout plus mul(M, v) cancel out, so shaders see glm's matrices.

// set 0: per frame -------------------------------------------------------------------------------------------------------

struct FrameConstants
{
    float4x4 view;
    float4x4 projection;
    float4x4 viewProjection;
    // inverse(projection * rotation-only view), maps clip space to a world space direction for the skybox.
    float4x4 skyboxInverseViewProjection;
    float4 cameraPosition;      // xyz
    float4 lightPositionRadius; // xyz position, w lightRadius of eq. 9
    float4 lightColorIntensity; // rgb color, w intensity
    float4 iblParams;           // x intensity, y specular mip count
};

[[vk::binding(0, 0)]] ConstantBuffer<FrameConstants> frame;
[[vk::binding(1, 0)]] SamplerCube irradianceMap; // E(n) / pi, see programs/IblBaker/Irradiance.cpp
[[vk::binding(2, 0)]] SamplerCube specularMap;   // PrefilterEnvMap, Roughness = mip / (mipCount - 1)
[[vk::binding(3, 0)]] Sampler2D brdfLut;         // IntegrateBRDF, u = NoV, v = Roughness

// set 1: per material ----------------------------------------------------------------------------------------------------

static const uint kMaterialFlagNormalTexture = 1u;

struct MaterialData
{
    float4 baseColorFactor;
    float4 emissiveFactor; // rgb
    float metallicFactor;
    float roughnessFactor;
    float normalScale;
    float occlusionStrength;
    float alphaCutoff;
    uint flags;
    float2 padding;
};

[[vk::binding(0, 1)]] ConstantBuffer<MaterialData> material;
[[vk::binding(1, 1)]] Sampler2D baseColorTexture;         // sRGB
[[vk::binding(2, 1)]] Sampler2D metallicRoughnessTexture; // G roughness, B metallic
[[vk::binding(3, 1)]] Sampler2D normalTexture;
[[vk::binding(4, 1)]] Sampler2D occlusionTexture;         // R
[[vk::binding(5, 1)]] Sampler2D emissiveTexture;          // sRGB

// push constants ---------------------------------------------------------------------------------------------------------

struct DrawConstants
{
    float4x4 model;
    float4x4 normalMatrix; // transpose(inverse(model))
};

[[vk::push_constant]] ConstantBuffer<DrawConstants> drawConstants;
