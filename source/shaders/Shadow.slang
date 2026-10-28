// Depth only pass of the directional light shadow map. Reuses the frame set (lightViewProjection), the material set
// (alpha MASK) and the forward pass's DrawConstants. Shadows are MEngine's addition to the UE4 2013 notes.
#include "common/Bindings.slang"

struct MaskVertexInput
{
    [[vk::location(0)]] float3 position : POSITION;
    [[vk::location(2)]] float2 texCoord : TEXCOORD0;
};

struct MaskVertexOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

float4 TransformToLight(float3 position)
{
    return mul(frame.lightViewProjection, mul(drawConstants.model, float4(position, 1.0)));
}

[shader("vertex")]
float4 VertexMain([[vk::location(0)]] float3 position : POSITION) : SV_Position
{
    return TransformToLight(position);
}

[shader("vertex")]
MaskVertexOutput VertexMainMask(MaskVertexInput input)
{
    MaskVertexOutput output;
    output.position = TransformToLight(input.position);
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
void FragmentMask(MaskVertexOutput input)
{
    float alpha = material.baseColorFactor.a * baseColorTexture.Sample(input.texCoord).a;
    if (alpha < material.alphaCutoff)
    {
        discard;
    }
}
