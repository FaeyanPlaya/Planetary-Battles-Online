// Spooky, at half size: where the battle's lights come from at each place (rgb: their directions summed by their
// strength; a: the strength summed), for the textures' relief in lit.hlsl. As half.hlsl's lights, without the colours.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    if (NORMALS <= 0 || LightCount.x < 0.5) return float4(0, 0, 0, 0);
    uv = ScenePixel(uv);
    float d = SceneDepth(uv);
    if (d >= 1) return float4(0, 0, 0, 0);
    float3 p = WorldPos(uv, d);
    float3 sum = 0;
    float total = 0;
    [loop] for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= LightCount.x) break;
        float3 to = LightPos[i].xyz - p;
        float reach = LightPos[i].w * 1.5;
        float len2 = dot(to, to);
        if (len2 < reach * reach) {
            float len = sqrt(len2);
            float fall = 1.0 - len / reach;
            float w = Luma(LightColor[i].rgb) * fall * fall;
            sum += to / max(len, 0.001) * w;
            total += w;
        }
    }
    return float4(sum, total);
}
