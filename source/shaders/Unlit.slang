// glm matrices are pushed as-is; Slang's RowMajor decoration plus mul(M, v) cancel out, so the shader sees glm's matrix.

struct VertexInput
{
    float3 position : POSITION;
    float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    float2 texCoord : TEXCOORD0;
};

struct PushConstants
{
    float4x4 modelViewProjection;
};

[[vk::push_constant]] ConstantBuffer<PushConstants> pushConstants;

[[vk::binding(0, 0)]] Sampler2D albedoTexture;

[shader("vertex")]
VertexOutput VertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(pushConstants.modelViewProjection, float4(input.position, 1.0));
    output.texCoord = input.texCoord;
    return output;
}

[shader("fragment")]
float4 FragmentMain(VertexOutput input) : SV_Target
{
    return albedoTexture.Sample(input.texCoord);
}
