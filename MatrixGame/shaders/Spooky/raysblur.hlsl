// Spooky, at half size: the rays without their grain. rays.hlsl starts each pixel of a 4x4 block elsewhere along its
// steps; the block's average here is as good as 256 steps a pixel. Four smoothed reads, two by two pixels each; one
// across an edge of things (another depth) counts less, so a near palm does not smear into the far fog.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
#if !SHAFTS
    return float4(0, 0, 0, 1);
#else
    float2 px = ScreenSize.zw;
    float z = ViewDepth(SceneDepth(uv));
    float2 sum = 0;
    float weight = 0;
    [unroll] for (int i = 0; i < 4; i++) {
        float2 at = uv + float2(i % 2 == 0 ? -1.5 : 0.5, i < 2 ? -1.5 : 0.5) * px;
        float w = 1.0 / (1.0 + abs(ViewDepth(SceneDepth(at)) - z) * 0.05);
        sum += tex2D(Rays, at).rg * w;
        weight += w;
    }
    return float4(sum / weight, 0, 1);
#endif
}
