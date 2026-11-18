// VHS, at half size: soft lighting: corners, the foot of walls, the ground under robots softly darker
// (r: 1 open). Wide and gentle, over the game's own light.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    if (SOFTLIGHT <= 0) return float4(1, 1, 1, 1);
    uv = ScenePixel(uv);
    float d = SceneDepth(uv);
    if (d >= 1) return float4(1, 1, 1, 1);
    float3 p = WorldPos(uv, d);
    float3 view = normalize(p - CameraPos.xyz);
    float dist = length(p - CameraPos.xyz);
    if (WaterAt(view) < dist) return float4(1, 1, 1, 1);  // (under the water: its surface shows)
    float3 n = SceneNormal(uv);
    float jitter = Hash12(uv * ScreenSize.xy);
    // Points around it over its surface, out to a wide radius: how many something nearer to the camera hides, the
    // nearer ones counting more (lifted off the surface: the landscape's triangles meet in slight folds).
    float radius = 7.0 + dist * 0.012;
    float3 t = normalize(cross(n, abs(n.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
    float3 b = cross(n, t);
    float hidden = 0, total = 0;
    [loop] for (int i = 0; i < 12; i++) {
        float f = (i + 0.5) / 12.0;
        float a = jitter * 6.2831853 + i * 2.3999632;
        float r = sqrt(f);
        float3 dir = normalize(t * (cos(a) * r) + b * (sin(a) * r) + n * (sqrt(1.0 - f) + 0.3));
        float3 sp = ScreenPos(p + dir * radius * lerp(0.25, 1.0, f));
        if (any(sp.xy != saturate(sp.xy))) continue;
        float behind = ViewDepth(sp.z) - ViewDepth(SceneDepth(sp.xy));
        float w = 1.0 - f * 0.5;
        hidden += (behind > 0.3 + radius * 0.1 && behind < radius * 2.5 ? 1.0 : 0.0) * w;
        total += w;
    }
    float shade = 1.0 - saturate(hidden / max(total, 1.0) * SOFTLIGHT * 0.55);
    return float4(shade, shade, shade, 1);
}
