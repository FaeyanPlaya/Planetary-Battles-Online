// VHS: the brightness as a tape keeps it (linear): a little blurred across (r), and much blurred (g) for the halos the tape's
// sharpening leaves at edges.
#include "common.hlsl"

float Y(float2 at) { return ToYIQ(ToLinear(tex2D(SceneTex, at).rgb)).x; }  // (in linear light)

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float px = PxX();
    float narrow = Y(uv) * 0.4 + (Y(uv + float2(px * 1.2, 0)) + Y(uv - float2(px * 1.2, 0))) * 0.3;
    float wide = narrow * 0.2;
    [unroll] for (int i = 1; i <= 4; i++) {
        float off = px * 1.1 * i;
        wide += (Y(uv + float2(off, 0)) + Y(uv - float2(off, 0))) * 0.1;
    }
    return float4(narrow, wide, 0, 1);
}
