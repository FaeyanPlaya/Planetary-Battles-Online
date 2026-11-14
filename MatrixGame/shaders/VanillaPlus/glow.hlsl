// Vanilla+, at a quarter: what glows (the effects' bright part: fire, flashes, shots) and the brightest places of
// the picture (the sun on the water, white-hot metal), every pixel of its 4x4 read.
#include "common.hlsl"

float3 Bright(float2 at) {
    float3 scene = tex2D(SceneTex, at).rgb, solid = tex2D(SolidTex, at).rgb;
    float3 hot = Hot(scene, solid);
    float over = saturate((Luma(solid) - 0.82) * 4.0);  // only what is close to white
    return hot + solid * over * 0.5;
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 px = SceneSize.zw;
    float3 c = Bright(uv + float2(-px.x, -px.y)) + Bright(uv + float2(px.x, -px.y)) + Bright(uv + float2(-px.x, px.y)) +
               Bright(uv + px);
    return float4(c * 0.25, 1);
}
