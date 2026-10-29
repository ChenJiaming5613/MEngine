// Directional shadow lookup (MEngine's addition, not in the UE4 2013 notes). Together with the caster's depth bias, the
// receiver offset along its normal by shadowParams.z (about 1.5 texels) removes acne. 3x3 compare taps; with a LINEAR
// compare sampler each tap is itself a 2x2 bilinear PCF.
float DirectionalShadow(float3 worldPosition, float3 geometricNormal)
{
    if (frame.shadowParams.x == 0.0)
    {
        return 1.0;
    }
    float3 receiver = worldPosition + geometricNormal * frame.shadowParams.z;
    float4 clip = mul(frame.lightViewProjection, float4(receiver, 1.0));
    float3 ndc = clip.xyz / clip.w;
    // The projection encloses the whole scene, so nothing outside the map casts a shadow.
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
