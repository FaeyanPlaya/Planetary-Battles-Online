// VHS: onto the screen: the lines of the other half-frame from the one before (interlacing), the bend of a
// picture tube's glass, the darker edges.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 at = uv;
    if (LENS > 0) {
        // The glass bulges: the picture's edges bend in (the corners most), the screen's aspect kept.
        float2 c = (uv - 0.5) * float2(SceneSize.x / SceneSize.y, 1.0);
        float k = LENS * 0.12;
        at = 0.5 + (uv - 0.5) * (1.0 + k * dot(c, c)) / (1.0 + k * 0.5);
        if (any(at != saturate(at))) return float4(0, 0, 0, 1);
    }
    float3 col = tex2D(Signal, at).rgb;
    float field = floor(Time.x * FIELDS);
    if (fmod(TapeLine(at) + field, 2.0) >= 1.0 && State.z >= 2)
        col = lerp(col, tex2D(TapeOld, at).rgb, INTERLACE);
    float2 v = (uv - 0.5) * float2(1.0, 0.85);
    col *= 1.0 - VIGNETTE * 0.7 * saturate(dot(v, v) * 2.4);
    return float4(col, 1);
}
