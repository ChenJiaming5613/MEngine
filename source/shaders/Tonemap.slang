// Resolves the HDR forward target into the swapchain: exposure, then the ACES filmic fit (Narkowicz 2015).
// Tone mapping is not covered by the UE4 notes; it is MEngine's addition.

struct TonemapConstants
{
    float exposure;
    // 1 when the swapchain is UNORM and the shader must apply the sRGB transfer function itself.
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

float3 AcesFilmic(float3 x)
{
    const float a = 2.51;
    const float b = 0.03;
    const float c = 2.43;
    const float d = 0.59;
    const float e = 0.14;
    return saturate((x * (a * x + b)) / (x * (c * x + d) + e));
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
    float3 color = AcesFilmic(hdrColor.SampleLevel(input.texCoord, 0.0).rgb * tonemap.exposure);
    if (tonemap.encodeSrgb != 0)
    {
        color = LinearToSrgb(color);
    }
    return float4(color, 1.0);
}
