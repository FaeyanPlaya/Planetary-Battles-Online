// Vanilla+, one pixel: how bright the view is (the average of 8x8 points of the picture, the middle counting more),
// followed slowly as an eye gets used to it; post.hlsl sets the exposure by it.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float sum = 0, weight = 0;
    [unroll] for (int y = 0; y < 8; y++)
        [unroll] for (int x = 0; x < 8; x++) {
            float2 at = (float2(x, y) + 0.5) / 8.0;
            float w = 1.0 - length(at - 0.5);
            sum += log2(max(Luma(tex2Dlod(SceneTex, float4(at, 0, 0)).rgb), 0.01)) * w;
            weight += w;
        }
    float now = exp2(sum / weight);
    float before = tex2D(EyeLast, float2(0.5, 0.5)).r;
    if (State.z < 2 || before <= 0) return now;
    return lerp(before, now, saturate(Time.w * 1.5));
}
