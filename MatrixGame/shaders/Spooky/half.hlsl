// Spooky, at half size: the battle's lights (rgb) and the shade of corners and short shadows (a, 1 open).
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    uv = ScenePixel(uv);  // one screen pixel's depth, where it is (not the corner of four: lines across the view)
    float d = SceneDepth(uv);
    if (d >= 1) return float4(0, 0, 0, 1);
    float3 p = WorldPos(uv, d);
    float3 n = SceneNormal(uv);
#if GEOMETRY
    // The surface's own normal where the world pass drew one (world.hlsl): a model's is smooth over its rounded
    // parts, the depth's is each triangle's.
    float3 own = tex2Dlod(WorldN, float4(uv, 0, 0)).rgb;
    if (dot(own, own) > 0.01) n = normalize(own * 2.0 - 1.0);
#endif
    // Under the water: its surface is what shows (no corner shade or shadow of the bottom; the lights on the water).
    float3 view = normalize(p - CameraPos.xyz);
    float water = WaterAt(view);
    bool under = water < length(p - CameraPos.xyz);
    if (under) {
        p = CameraPos.xyz + view * water;
        n = float3(0, 0, 1);
    }
    float dist = length(p - CameraPos.xyz);
    float jitter = Hash12(uv * ScreenSize.xy);
    float shade = 1;
    if (AO > 0 && !under) {
        // Points around it over its surface: how many something nearer to the camera hides (close by only). Lifted
        // off the surface and hidden by more than a fold's depth: the landscape's triangles meet in slight folds, and
        // points along the surface fell behind every concave one (dark lines along its rows across the view).
        float radius = 4.0 + dist * 0.008;
        float3 t = normalize(cross(n, abs(n.z) < 0.9 ? float3(0, 0, 1) : float3(1, 0, 0)));
        float3 b = cross(n, t);
        float hidden = 0;
        [loop] for (int i = 0; i < 8; i++) {
            float f = (i + 0.5) / 8.0;
            float a = jitter * 6.2831853 + i * 2.3999632;
            float r = sqrt(f);
            float3 dir = normalize(t * (cos(a) * r) + b * (sin(a) * r) + n * (sqrt(1.0 - f) + 0.35));
            float3 sp = ScreenPos(p + dir * radius * lerp(0.3, 1.0, f));
            if (any(sp.xy != saturate(sp.xy))) continue;
            float behind = ViewDepth(sp.z) - ViewDepth(SceneDepth(sp.xy));
            hidden += behind > 0.3 + radius * 0.12 && behind < radius * 2.0 ? 1.0 : 0.0;
        }
        shade = 1.0 - saturate(hidden / 8.0 * AO * 0.6);
    }
    if (SHADOWS > 0 && !under) {
        // A few short steps towards the map's sun: something just in the way, a shadow (thin things only, so what
        // merely stands between it and the camera casts none).
        float sun = saturate(dot(n, SunDir.xyz) * 2.0);
        if (sun > 0) {
            float step = (1.2 + dist * 0.002) * (1.0 + jitter * 0.5);
            float hit = 0;
            [loop] for (int i = 1; i <= 8; i++) {
                float3 sp = ScreenPos(p + SunDir.xyz * step * i);
                if (any(sp.xy != saturate(sp.xy))) break;
                float behind = ViewDepth(sp.z) - ViewDepth(SceneDepth(sp.xy));
                if (behind > 0.3 && behind < 6.0) {
                    hit = 1.0 - i / 9.0;
                    break;
                }
            }
            shade *= 1.0 - 0.55 * hit * sun * SHADOWS;
        }
    }
    // The lights: the facets of the landscape's triangles softened, the side away from a light not quite dark.
    float3 ns = normalize(n + float3(0, 0, 0.6));
    float3 sum = 0;
    [loop] for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= LightCount.x) break;
        float3 to = LightPos[i].xyz - p;
        float reach = LightPos[i].w * 1.5;
        float len2 = dot(to, to);
        if (len2 < reach * reach) {
            float len = sqrt(len2);
            float fall = 1.0 - len / reach;
            float facing = saturate(dot(ns, to / max(len, 0.001)) * 0.5 + 0.5);
            float k = LightInfo[i].x == LIGHT_FIRE ? Flicker(LightInfo[i].y, Time.y) : 1.0;
            sum += LightColor[i].rgb * (fall * fall * facing * k);
        }
    }
    return float4(sum, shade);
}
