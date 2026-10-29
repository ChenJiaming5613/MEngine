#include "common/Bindings.slang"
#include "common/Brdf.slang"
#include "common/ShadowSampling.slang"

// Nonzero for alphaMode MASK (alpha test against material.alphaCutoff).
[vk::constant_id(0)] const uint kAlphaMask = 0;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
};

struct VertexInputTangent
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float3 normal : NORMAL;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
    [[vk::location(3)]] float4 tangent : TANGENT;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 worldPosition : POSITION0;
    float3 normal : NORMAL0;
    // w = 0 marks a primitive without tangents; the fragment shader then builds the frame from derivatives.
    float4 tangent : TANGENT0;
    float2 texCoord : TEXCOORD0;
};

VertexOutput TransformVertex(float3 position, float3 normal, float4 tangent, float2 texCoord)
{
    float4 worldPosition = mul(drawConstants.model, float4(position, 1.0));
    VertexOutput output;
    output.position = mul(frame.viewProjection, worldPosition);
    output.worldPosition = worldPosition.xyz;
    output.normal = mul(drawConstants.normalMatrix, float4(normal, 0.0)).xyz;
    output.tangent = float4(mul(drawConstants.model, float4(tangent.xyz, 0.0)).xyz, tangent.w);
    output.texCoord = texCoord;
    return output;
}

[shader("vertex")]
VertexOutput VertexMain(VertexInput input)
{
    return TransformVertex(input.position, input.normal, float4(0.0), input.texCoord);
}

[shader("vertex")]
VertexOutput VertexMainTangent(VertexInputTangent input)
{
    return TransformVertex(input.position, input.normal, input.tangent, input.texCoord);
}

// glTF: bitangent = cross(normal, tangent.xyz) * tangent.w. Without tangents the frame follows the UV gradients, as in
// the glTF sample viewer (MEngine's addition, not in the notes).
float3 ApplyNormalMap(VertexOutput input, float3 N, bool frontFacing)
{
    float3 tangentNormal = normalTexture.Sample(input.texCoord).xyz * 2.0 - 1.0;
    tangentNormal.xy *= material.normalScale;

    float3 T;
    float3 B;
    if (input.tangent.w != 0.0)
    {
        T = normalize(input.tangent.xyz - N * dot(N, input.tangent.xyz));
        B = cross(N, T) * input.tangent.w;
        if (!frontFacing)
        {
            T = -T;
            B = -B;
        }
    }
    else
    {
        float3 positionDx = ddx(input.worldPosition);
        float3 positionDy = ddy(input.worldPosition);
        float2 uvDx = ddx(input.texCoord);
        float2 uvDy = ddy(input.texCoord);
        float determinant = uvDx.x * uvDy.y - uvDy.x * uvDx.y;
        float3 tangent = abs(determinant) > 1e-12 ? (uvDy.y * positionDx - uvDx.y * positionDy) / determinant : positionDx;
        tangent = tangent - N * dot(N, tangent);
        if (dot(tangent, tangent) < 1e-20)
        {
            return N;
        }
        T = normalize(tangent);
        B = cross(N, T);
    }
    return normalize(T * tangentNormal.x + B * tangentNormal.y + N * tangentNormal.z);
}

// Eq. 1-5 for one light: Lo = (f_d + f_s) * radiance * NoL.
float3 EvaluateDirectLight(float3 N, float3 V, float3 L, float3 radiance, float3 diffuseColor, float3 specularColor,
                           float roughness, float NoV)
{
    float NoL = saturate(dot(N, L));
    if (NoL <= 0.0)
    {
        return float3(0.0);
    }
    float3 brdf = DiffuseLambert(diffuseColor);
    if (NoV > 0.0)
    {
        float3 H = normalize(L + V);
        float NoH = saturate(dot(N, H));
        float VoH = saturate(dot(V, H));
        float D = DGgx(roughness, NoH);
        float3 F = FSchlickSg(specularColor, VoH);
        float G = GSmithAnalytic(roughness, NoV, NoL);
        brdf += SpecularCookTorrance(D, F, G, NoL, NoV);
    }
    return brdf * radiance * NoL;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input, bool frontFacing : SV_IsFrontFace) : SV_Target
{
    float4 baseColor = material.baseColorFactor * baseColorTexture.Sample(input.texCoord);
    if (kAlphaMask != 0 && baseColor.a < material.alphaCutoff)
    {
        discard;
    }

    float4 metallicRoughness = metallicRoughnessTexture.Sample(input.texCoord);
    float3 specularColor;
    float3 diffuseColor;
    float roughness;
    if ((material.flags & kMaterialFlagSpecularGlossiness) != 0)
    {
        // KHR_materials_pbrSpecularGlossiness: specular is F0 and roughness = 1 - glossiness; the extension's
        // reference model scales diffuse by 1 - max(specular) to conserve energy.
        float4 specularGlossiness = material.specularGlossinessFactor * metallicRoughness;
        specularColor = saturate(specularGlossiness.rgb);
        roughness = saturate(1.0 - specularGlossiness.a);
        diffuseColor = baseColor.rgb * (1.0 - max(specularColor.r, max(specularColor.g, specularColor.b)));
    }
    else
    {
        float metallic = saturate(material.metallicFactor * metallicRoughness.b);
        roughness = saturate(material.roughnessFactor * metallicRoughness.g);
        specularColor = SpecularColorFromMetallic(baseColor.rgb, metallic);
        diffuseColor = DiffuseColorFromMetallic(baseColor.rgb, metallic);
    }

    // Back faces of double sided materials use the reversed normal (glTF 2.0, 3.9.5).
    float3 N = normalize(input.normal);
    if (!frontFacing)
    {
        N = -N;
    }
    float3 geometricNormal = N;
    if ((material.flags & kMaterialFlagNormalTexture) != 0)
    {
        N = ApplyNormalMap(input, N, frontFacing);
    }

    float3 V = normalize(frame.cameraPosition.xyz - input.worldPosition);
    float NoV = saturate(dot(N, V));

    // One point light (eq. 9 falloff) and one shadowed directional light.
    float3 direct = float3(0.0);
    float3 toLight = frame.lightPositionRadius.xyz - input.worldPosition;
    float distance = length(toLight);
    if (distance > 0.0)
    {
        float falloff = LightFalloff(distance, frame.lightPositionRadius.w);
        float3 radiance = frame.lightColorIntensity.rgb * frame.lightColorIntensity.w * falloff;
        direct += EvaluateDirectLight(N, V, toLight / distance, radiance, diffuseColor, specularColor, roughness, NoV);
    }

    float3 directionalRadiance = frame.directionalColorIntensity.rgb * frame.directionalColorIntensity.w;
    if (any(directionalRadiance > 0.0))
    {
        float3 L = -frame.directionalDirection.xyz;
        if (dot(N, L) > 0.0)
        {
            float shadow = DirectionalShadow(input.worldPosition, geometricNormal);
            direct += EvaluateDirectLight(N, V, L, directionalRadiance * shadow, diffuseColor, specularColor, roughness, NoV);
        }
    }

    // Split sum specular (p7) plus the irradiance cube (eq. 6 applied to eq. 1).
    float3 specularIbl = ApproximateSpecularIbl(specularMap, brdfLut, frame.iblParams.y, specularColor, roughness, N, V);
    float3 diffuseIbl = diffuseColor * irradianceMap.SampleLevel(N, 0.0).rgb;
    // glTF: occlusion = 1 + strength * (texture.r - 1), applied to indirect light only.
    float occlusion = 1.0 + material.occlusionStrength * (occlusionTexture.Sample(input.texCoord).r - 1.0);

    float3 emissive = material.emissiveFactor.rgb * emissiveTexture.Sample(input.texCoord).rgb;
    float3 color = direct + (diffuseIbl + specularIbl) * occlusion * frame.iblParams.x + emissive;
    return float4(color, 1.0);
}
