// HDR target to swapchain: exposure, then John Hable's Uncharted 2 curve normalized at the white point 11.2, with
// Vulkan-glTF-PBR's constants and default exposure (4.5). MEngine's addition; the UE4 notes do not cover tone mapping.

struct TonemapConstants
{
    float exposure;
    // 1 when the swapchain is UNORM and the shader must encode sRGB itself.
    uint encodeSrgb;
};

[[vk::push_constant]] ConstantBuffer<TonemapConstants> tonemap;
[[vk::binding(0, 0)]] Sampler2D hdrColor;

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

// SV_VulkanVertexID is gl_VertexIndex as is; SV_VertexID would subtract BaseVertex and need shaderDrawParameters.
[shader("vertex")]
VertexOutput VertexMain(uint vertexId : SV_VulkanVertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.texCoord = uv;
    output.position = float4(uv * 2.0 - 1.0, 0.0, 1.0);
    return output;
}

// http://filmicworlds.com/blog/filmic-tonemapping-operators/
float3 Uncharted2Curve(float3 x)
{
    const float A = 0.15; // shoulder strength
    const float B = 0.50; // linear strength
    const float C = 0.10; // linear angle
    const float D = 0.20; // toe strength
    const float E = 0.02; // toe numerator
    const float F = 0.30; // toe denominator
    return ((x * (A * x + C * B) + D * E) / (x * (A * x + B) + D * F)) - E / F;
}

float3 Uncharted2Filmic(float3 x)
{
    const float whitePoint = 11.2;
    return saturate(Uncharted2Curve(x) / Uncharted2Curve(float3(whitePoint)));
}

float3 LinearToSrgb(float3 color)
{
    float3 low = color * 12.92;
    float3 high = 1.055 * pow(color, float3(1.0 / 2.4)) - 0.055;
    return select(color <= 0.0031308, low, high);
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
    float3 color = Uncharted2Filmic(hdrColor.SampleLevel(input.texCoord, 0.0).rgb * tonemap.exposure);
    if (tonemap.encodeSrgb != 0)
    {
        color = LinearToSrgb(color);
    }
    return float4(color, 1.0);
}
