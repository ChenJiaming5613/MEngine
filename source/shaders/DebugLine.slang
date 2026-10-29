#include "common/Bindings.slang"

// Debug lines (render/DebugDraw.hpp), depth tested without depth writes. The color is pre-compensated for exposure and
// the Tonemap.slang curve so the line shows its sRGB color on screen (exact below the curve's white point).

struct DebugLineConstants
{
    float exposure;
};

[[vk::push_constant]] ConstantBuffer<DebugLineConstants> debugLine;

struct VertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(1)]] float4 color : COLOR; // RGBA8 UNORM, sRGB encoded
};

struct VertexOutput
{
    float4 position : SV_Position;
    float3 color : COLOR;
};

[shader("vertex")]
VertexOutput VertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(frame.viewProjection, float4(input.position, 1.0));
    output.color = input.color.rgb;
    return output;
}

float3 SrgbToLinear(float3 color)
{
    float3 low = color / 12.92;
    float3 high = pow((color + 0.055) / 1.055, float3(2.4));
    return select(color <= 0.04045, low, high);
}

// Inverse of Tonemap.slang's Uncharted2Filmic: solves A(1-t)x^2 + B(C-t)x + D(E-tF) = 0 for t = y * curve(W) + E/F.
float3 InverseUncharted2Filmic(float3 y)
{
    const float A = 0.15;
    const float B = 0.50;
    const float C = 0.10;
    const float D = 0.20;
    const float E = 0.02;
    const float F = 0.30;
    const float whitePoint = 11.2;
    const float curveWhite = ((whitePoint * (A * whitePoint + C * B) + D * E) / (whitePoint * (A * whitePoint + B) + D * F)) - E / F;

    float3 t = min(y, 0.999) * curveWhite + E / F;
    float3 a = A * (1.0 - t);
    float3 b = B * (C - t);
    float3 c = D * (E - t * F);
    return (-b + sqrt(max(b * b - 4.0 * a * c, 0.0))) / (2.0 * a);
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
    float3 hdr = InverseUncharted2Filmic(SrgbToLinear(input.color)) / debugLine.exposure;
    return float4(hdr, 1.0);
}
