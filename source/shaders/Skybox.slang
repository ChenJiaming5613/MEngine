#include "common/Bindings.slang"

// Drawn last in the forward pass as a full screen triangle on the far plane; depth test keeps it behind the scene.

struct VertexOutput
{
    float4 position : SV_Position;
    float2 clipPosition : TEXCOORD0;
};

// SV_VulkanVertexID is gl_VertexIndex as is; SV_VertexID would subtract BaseVertex and need shaderDrawParameters.
[shader("vertex")]
VertexOutput VertexMain(uint vertexId : SV_VulkanVertexID)
{
    float2 uv = float2((vertexId << 1) & 2, vertexId & 2);
    VertexOutput output;
    output.clipPosition = uv * 2.0 - 1.0;
    output.position = float4(output.clipPosition, 1.0, 1.0);
    return output;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
    float4 direction = mul(frame.skyboxInverseViewProjection, float4(input.clipPosition, 1.0, 1.0));
    float3 radiance = specularMap.SampleLevel(normalize(direction.xyz / direction.w), 0.0).rgb;
    return float4(radiance * frame.iblParams.x, 1.0);
}
