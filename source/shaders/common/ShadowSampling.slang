// Directional light shadow map lookup. Shadows are MEngine's addition; the UE4 2013 notes do not cover them.
//
// The receiver moves along its geometric normal by shadowParams.z (normal offset, about 1.5 shadow texels in world
// units) before projection, which together with the caster's depth bias removes acne. A 3x3 grid of compare taps is
// averaged; with a LINEAR compare sampler every tap is itself a 2x2 bilinear PCF.
float DirectionalShadow(float3 worldPosition, float3 geometricNormal)
{
    if (frame.shadowParams.x == 0.0)
    {
        return 1.0;
    }
    float3 receiver = worldPosition + geometricNormal * frame.shadowParams.z;
    float4 clip = mul(frame.lightViewProjection, float4(receiver, 1.0));
    float3 ndc = clip.xyz / clip.w;
    // Outside the map nothing casts a shadow (the projection encloses the whole scene).
    if (any(abs(ndc.xy) > 1.0) || ndc.z < 0.0 || ndc.z > 1.0)
    {
        return 1.0;
    }
    // Vulkan viewport: NDC y = -1 is the first row, so uv is a plain remap.
    float2 uv = ndc.xy * 0.5 + 0.5;
    float texel = frame.shadowParams.y;

    float lit = 0.0;
    [unroll]
    for (int y = -1; y <= 1; ++y)
    {
        [unroll]
        for (int x = -1; x <= 1; ++x)
        {
            lit += shadowMap.SampleCmpLevelZero(uv + float2(x, y) * texel, ndc.z);
        }
    }
    return lit / 9.0;
}
