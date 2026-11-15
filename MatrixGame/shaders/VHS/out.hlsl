// VHS: onto the screen: the lines of the other half-frame from the one before (interlacing), the darker edges.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 at = uv;  // (the lens bends the picture before the interface: lit.hlsl)
    float3 col = tex2D(Signal, at).rgb;
    float field = floor(Time.x * FIELDS);
    if (fmod(TapeLine(at) + field, 2.0) >= 1.0 && State.z >= 2)
        col = lerp(col, tex2D(TapeOld, at).rgb, INTERLACE);
    float2 v = (uv - 0.5) * float2(1.0, 0.85);
    col *= 1.0 - VIGNETTE * 0.7 * saturate(dot(v, v) * 2.4);
    return float4(col, 1);
}
