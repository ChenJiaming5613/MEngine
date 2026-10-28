// Brian Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes (docs/UE4-RealShading-2013.md).
// Every function is the formula of the notes it names; nothing outside the notes is used. Function names match
// programs/IblBaker/UeBrdf.hpp. All dot products are saturated by the caller.

static const float PI = 3.14159265358979323846;

// p9: non-metals use F0 = 0.04; the metallic workflow derives F0 and the diffuse albedo from BaseColor.
float3 SpecularColorFromMetallic(float3 baseColor, float metallic)
{
    return lerp(float3(0.04), baseColor, metallic);
}

float3 DiffuseColorFromMetallic(float3 baseColor, float metallic)
{
    return baseColor * (1.0 - metallic);
}

// Eq. 1: Lambert, f_d = c_diff / pi.
float3 DiffuseLambert(float3 diffuseColor)
{
    return diffuseColor / PI;
}

// Eq. 3: GGX / Trowbridge-Reitz with alpha = Roughness^2.
float DGgx(float roughness, float NoH)
{
    float a = roughness * roughness;
    float a2 = a * a;
    float d = NoH * NoH * (a2 - 1.0) + 1.0;
    return a2 / (PI * d * d);
}

// Eq. 4: Schlick-Smith, G1(x) = (n.x) / ((n.x)(1 - k) + k).
float G1Schlick(float NoX, float k)
{
    return NoX / (NoX * (1.0 - k) + k);
}

// Eq. 4 for analytic lights: k = (Roughness + 1)^2 / 8.
float GSmithAnalytic(float roughness, float NoV, float NoL)
{
    float k = (roughness + 1.0) * (roughness + 1.0) / 8.0;
    return G1Schlick(NoL, k) * G1Schlick(NoV, k);
}

// Eq. 4 for image based lighting: k = alpha / 2 (used offline by IblBaker; kept here for parity).
float GSmithIbl(float roughness, float NoV, float NoL)
{
    float k = (roughness * roughness) / 2.0;
    return G1Schlick(NoL, k) * G1Schlick(NoV, k);
}

// Eq. 5: Schlick Fresnel with the spherical Gaussian approximation.
float3 FSchlickSg(float3 specularColor, float VoH)
{
    return specularColor + (1.0 - specularColor) * exp2((-5.55473 * VoH - 6.98316) * VoH);
}

// Eq. 2: Cook-Torrance, f_s = D F G / (4 (n.l) (n.v)). Only evaluated for NoL > 0 and NoV > 0.
float3 SpecularCookTorrance(float D, float3 F, float G, float NoL, float NoV)
{
    return D * F * G / (4.0 * NoL * NoV);
}

// Eq. 9: windowed inverse square falloff.
float LightFalloff(float distance, float lightRadius)
{
    float ratio = distance / lightRadius;
    float window = saturate(1.0 - ratio * ratio * ratio * ratio);
    return window * window / (distance * distance + 1.0);
}

// p7 ApproximateSpecularIBL: the prefiltered cube replaces PrefilterEnvMap, the LUT replaces IntegrateBRDF.
// Mip mapping Roughness = mip / (mipCount - 1) is MEngine's addition (the notes do not fix it).
float3 ApproximateSpecularIbl(SamplerCube prefilteredMap, Sampler2D environmentBrdf, float mipCount, float3 specularColor,
    float roughness, float3 N, float3 V)
{
    float NoV = saturate(dot(N, V));
    float3 R = 2.0 * dot(V, N) * N - V;
    float3 prefilteredColor = prefilteredMap.SampleLevel(R, roughness * (mipCount - 1.0)).rgb;
    float2 envBrdf = environmentBrdf.SampleLevel(float2(NoV, roughness), 0.0).rg;
    return prefilteredColor * (specularColor * envBrdf.x + envBrdf.y);
}
