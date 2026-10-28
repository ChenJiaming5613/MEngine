#pragma once

// Brian Karis, "Real Shading in Unreal Engine 4", SIGGRAPH 2013 course notes, pages 4-7 (docs/UE4-RealShading-2013.md).
// Literal C++ ports of the notes' HLSL: Hammersley, ImportanceSampleGgx (ImportanceSampleGGX), SpecularIbl (SpecularIBL),
// PrefilterEnvMap and IntegrateBrdf (IntegrateBRDF).
// Only the syntax changes (HLSL -> C++/glm); the formulas, sample sequence and k = alpha / 2 are the notes'.
// Function names match source/shaders/common/Brdf.slang.

#include "Cubemap.hpp"

#include <glm/geometric.hpp>
#include <glm/vec2.hpp>
#include <glm/vec3.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace iblbaker {

constexpr float PI = 3.14159265358979323846f;

inline float saturate(float x)
{
    return std::clamp(x, 0.0f, 1.0f);
}

// Van der Corput radical inverse in base 2 (bit reversal).
inline float RadicalInverseVdC(uint32_t bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return static_cast<float>(bits) * 2.3283064365386963e-10f; // / 0x100000000
}

// Hammersley point i of n: (i / n, radical inverse of i).
inline glm::vec2 Hammersley(uint32_t i, uint32_t n)
{
    return glm::vec2(static_cast<float>(i) / static_cast<float>(n), RadicalInverseVdC(i));
}

// Notes p4.
inline glm::vec3 ImportanceSampleGgx(glm::vec2 Xi, float Roughness, glm::vec3 N)
{
    float a = Roughness * Roughness;
    float Phi = 2 * PI * Xi.x;
    float CosTheta = std::sqrt((1 - Xi.y) / (1 + (a * a - 1) * Xi.y));
    float SinTheta = std::sqrt(1 - CosTheta * CosTheta);
    glm::vec3 H;
    H.x = SinTheta * std::cos(Phi);
    H.y = SinTheta * std::sin(Phi);
    H.z = CosTheta;
    glm::vec3 UpVector = std::abs(N.z) < 0.999f ? glm::vec3(0, 0, 1) : glm::vec3(1, 0, 0);
    glm::vec3 TangentX = glm::normalize(glm::cross(UpVector, N));
    glm::vec3 TangentY = glm::cross(N, TangentX);
    // Tangent to world space
    return TangentX * H.x + TangentY * H.y + N * H.z;
}

// Eq. 4, Schlick: G1(x) = (n.x) / ((n.x)(1 - k) + k).
inline float G1Schlick(float NoX, float k)
{
    return NoX / (NoX * (1 - k) + k);
}

// Eq. 4 with the IBL remapping k = alpha / 2 (notes p3: the Disney "hotness" remap is for analytic lights only).
inline float GSmithIbl(float Roughness, float NoV, float NoL)
{
    float k = (Roughness * Roughness) / 2;
    return G1Schlick(NoL, k) * G1Schlick(NoV, k);
}

// Notes p4: reference specular IBL by importance sampling. Fc uses the plain Schlick power, as in the notes.
inline glm::vec3 SpecularIbl(const Cubemap& EnvMap, glm::vec3 SpecularColor, float Roughness, glm::vec3 N, glm::vec3 V,
    uint32_t NumSamples)
{
    glm::vec3 SpecularLighting(0);
    for (uint32_t i = 0; i < NumSamples; i++)
    {
        glm::vec2 Xi = Hammersley(i, NumSamples);
        glm::vec3 H = ImportanceSampleGgx(Xi, Roughness, N);
        glm::vec3 L = 2 * glm::dot(V, H) * H - V;
        float NoV = saturate(glm::dot(N, V));
        float NoL = saturate(glm::dot(N, L));
        float NoH = saturate(glm::dot(N, H));
        float VoH = saturate(glm::dot(V, H));
        if (NoL > 0)
        {
            glm::vec3 SampleColor = EnvMap.Sample(L);
            float G = GSmithIbl(Roughness, NoV, NoL);
            float Fc = std::pow(1 - VoH, 5.0f);
            glm::vec3 F = (1 - Fc) * SpecularColor + Fc;
            // Incident light = SampleColor * NoL
            // Microfacet specular = D*G*F / (4*NoL*NoV)
            // pdf = D * NoH / (4 * VoH)
            SpecularLighting += SampleColor * F * G * VoH / (NoH * NoV);
        }
    }
    return SpecularLighting / static_cast<float>(NumSamples);
}

// Notes p6: first sum, with n = v = r and NoL weighting.
inline glm::vec3 PrefilterEnvMap(const Cubemap& EnvMap, float Roughness, glm::vec3 R, uint32_t NumSamples)
{
    glm::vec3 N = R;
    glm::vec3 V = R;
    glm::vec3 PrefilteredColor(0);
    float TotalWeight = 0;
    for (uint32_t i = 0; i < NumSamples; i++)
    {
        glm::vec2 Xi = Hammersley(i, NumSamples);
        glm::vec3 H = ImportanceSampleGgx(Xi, Roughness, N);
        glm::vec3 L = 2 * glm::dot(V, H) * H - V;
        float NoL = saturate(glm::dot(N, L));
        if (NoL > 0)
        {
            PrefilteredColor += EnvMap.Sample(L) * NoL;
            TotalWeight += NoL;
        }
    }
    return PrefilteredColor / TotalWeight;
}

// Notes p6: second sum, the environment BRDF as a scale A and bias B on F0.
inline glm::vec2 IntegrateBrdf(float Roughness, float NoV, uint32_t NumSamples)
{
    glm::vec3 V;
    V.x = std::sqrt(1.0f - NoV * NoV); // sin
    V.y = 0;
    V.z = NoV; // cos
    glm::vec3 N(0, 0, 1);
    float A = 0;
    float B = 0;
    for (uint32_t i = 0; i < NumSamples; i++)
    {
        glm::vec2 Xi = Hammersley(i, NumSamples);
        glm::vec3 H = ImportanceSampleGgx(Xi, Roughness, N);
        glm::vec3 L = 2 * glm::dot(V, H) * H - V;
        float NoL = saturate(L.z);
        float NoH = saturate(H.z);
        float VoH = saturate(glm::dot(V, H));
        if (NoL > 0)
        {
            float G = GSmithIbl(Roughness, NoV, NoL);
            float G_Vis = G * VoH / (NoH * NoV);
            float Fc = std::pow(1 - VoH, 5.0f);
            A += (1 - Fc) * G_Vis;
            B += Fc * G_Vis;
        }
    }
    return glm::vec2(A, B) / static_cast<float>(NumSamples);
}

} // namespace iblbaker
