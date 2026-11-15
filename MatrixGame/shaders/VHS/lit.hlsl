// VHS: the soft lighting on the game's picture (the effects over it as the game drew them), onto the screen. The
// interface comes on top of this; then, with "Effects on the interface", the tape's passes take both.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    uv = Lens(uv);  // (the tube's glass: here, so the interface drawn after it is never bent)
    float3 scene = tex2D(SceneTex, uv).rgb;
    if (ABERRATION > 0) {
        // Colour fringes towards the edges: the red read further out, the blue further in.
        float2 o = (uv - 0.5) * ABERRATION * 0.006;
        scene.r = tex2D(SceneTex, uv + o).r;
        scene.b = tex2D(SceneTex, uv - o).b;
    }
    if (SOFTLIGHT <= 0) return float4(scene, 1);
    float d = SceneDepth(uv);
    if (d >= 1) return float4(scene, 1);
    float3 solid = tex2D(SolidTex, uv).rgb;
    // Of Shade's 4 nearest, those about as far away as this pixel.
    float z = ViewDepth(d);
    float2 px = SceneSize.zw;
    float sum = 0, weight = 0;
    [unroll] for (int i = 0; i < 4; i++) {
        float2 at = uv + float2(i % 2 == 0 ? -px.x : px.x, i < 2 ? -px.y : px.y);
        float w = 1.0 / (1.0 + abs(ViewDepth(SceneDepth(at)) - z) * 0.3);
        sum += tex2Dlod(Shade, float4(at, 0, 0)).r * w;
        weight += w;
    }
    // Into the game's fog it fades: far off the fog's colour is what shows.
    float dist = length(WorldPos(uv, d) - CameraPos.xyz);
    float near = FogColor.w > 0.5 ? 1.0 - smoothstep(FogRange.x, FogRange.y, dist) : 1.0;
    float shade = lerp(1.0, sum / weight, near);
    return float4(solid * shade + (scene - solid), 1);
}
