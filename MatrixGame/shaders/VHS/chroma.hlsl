// VHS, at half size: the colour as a tape keeps it (the I and Q of YIQ; in linear light a dark thing took the bright colour beside it): far blurrier across than the brightness,
// and it runs on to the right of where it was.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float px = PxX();
    float spread = (2.0 + 9.0 * BLEED) * px;  // between reads: 9 reads over 8 of these
    float2 at = uv - float2(BLEED * 4.0 * px, 0);  // (read on the left: the colour shows on the right)
    float2 iq = 0;
    float weight = 0;
    [unroll] for (int i = -4; i <= 4; i++) {
        float w = 1.0 - abs(i) / 5.0;
        iq += ToYIQ(tex2D(SceneTex, at + float2(spread * i, 0)).rgb).yz * w;
        weight += w;
    }
    return float4(iq / weight, 0, 1);
}
