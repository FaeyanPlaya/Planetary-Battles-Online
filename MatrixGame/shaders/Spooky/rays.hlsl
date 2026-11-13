// Spooky, at half size: the moon's rays in the fog. Along the view, as far as it goes, points of the air: each lit by
// the moon unless what stands between it and the moon shades it: the shadow map says (the world's depth as the moon
// sees it: robots, buildings, trees, the hills). So every shadow stands in the fog from the thing towards the ground,
// fixed in the world. Without a map (a card that gives none) a point asks the ground under it along the light: where
// that lies in the game's own shadow something above shaded it, the point too while it is low.
// r: how much fog the view goes through (0..1); g: how much of it lies in the shade, as much however long the view
// is past it (a column crossed is as dark before the sea as before a wall).
#include "common.hlsl"

// The ground the moon's light through p reaches (twice: the first guess on the water's level, then the land seen
// there), on the screen; false off it.
bool GroundUnder(float3 p, out float2 at) {
    float z = GroundZ;
    at = 0;
    [unroll] for (int k = 0; k < 2; k++) {
        float3 q = p - SunDir.xyz * (max(p.z - z, 0.0) / SunDir.z);
        float3 s = ScreenPos(q);
        if (s.z <= 0 || s.z >= 1 || any(s.xy < 0) || any(s.xy > 1)) return false;  // (behind the eye, past the far)
        at = s.xy;
        float d = SceneDepth(at);
        if (d >= 1) return false;  // the sky there: nothing to ask
        z = WorldPos(at, d).z;
    }
    return true;
}

// How lit the ground is there by the moon: the game's shadows (SolidTex.a: robots', buildings', trees'). The pack's own
// corner shade is no shadow of a thing: with it whole slopes counted as shade and the far view only dimmed.
float LitAt(float2 at) { return State.w > 0.5 ? tex2Dlod(SolidTex, float4(at, 0, 0)).a : 1.0; }

float4 main(float2 uv : TEXCOORD0) : COLOR {
#if !SHAFTS
    return float4(0, 0, 0, 1);
#else
    if (RAYS <= 0 || SunDir.z < 0.05) return float4(0, 0, 0, 1);
    float3 dir = normalize(WorldPos(uv, 1) - CameraPos.xyz);
    float d = SceneDepth(uv);
    float dist = d < 1 ? length(WorldPos(uv, d) - CameraPos.xyz) : 1e9;
    dist = min(min(dist, WaterAt(dir)), FogRange.z * 0.6);
    const int N = 16;
    float step = dist / N;
    // Each pixel of a 4x4 block its own start along the steps, in order (no grain): raysblur.hlsl takes the block's
    // average.
    float t = step * Bayer4(floor(uv * ScreenSize.xy));
    float shade = 0, fog = 0;
    [loop] for (int i = 0; i < N; i++) {
        float3 p = CameraPos.xyz + dir * t;
        float h = max(p.z - GroundZ, 0.0);
        float dens = exp(-h / FOGHEIGHT);
        float2 at;
        float l;
        if (ShadowInfo.w > 0) l = ShadowLit(p, ShadowInfo.z * 4.0);  // (some pixels of the map nearer: the ground itself is no shade)
        // Low enough to be shaded by what stands on the ground; higher, open.
        else l = GroundUnder(p, at) ? lerp(LitAt(at), 1.0, saturate(h / (FOGHEIGHT * 3.0))) : 1.0;
        shade += dens * (1.0 - l);
        fog += dens;
        t += step;
    }
    // (as thick as the pack's fog: lit.hlsl's measure; the shade's columns are thin, so it counts more)
    float k = step * FOG * 0.0025;
    return float4(1.0 - exp(-fog * k), 1.0 - exp(-shade * k * 6.0), 0, 1);
#endif
}
