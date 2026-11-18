// Spooky, at half size: the moon's rays in the fog. Along the view, as far as it goes, points of the air: each lit by
// the moon unless what stands between it and the moon shades it: the shadow map says (the world's depth as the moon
// sees it: robots, buildings, trees, the hills). So every shadow stands in the fog from the thing towards the ground,
// fixed in the world. Without a map (a card that gives none) a point asks the ground under it along the light: where
// that lies in the game's own shadow something above shaded it, the point too while it is low.
// r: how much fog the view goes through (0..1); g: how much of it lies in the shade, as much however long the view
// is past it (a column crossed is as dark before the sea as before a wall).
#include "common.hlsl"

// The ground the moon's light through p reaches (twice: the first guess on the water's level, then the land seen
// there), on the screen; false off it, and where the screen does not show that ground (something nearer stands before
// it there: a palm before the far sea read as the ground shaded the air over the sea, a plume over the sky that moved
// with the view). `q`: that ground in the world.
bool GroundUnder(float3 p, out float2 at, out float3 q) {
    float z = GroundZ;
    at = 0;
    q = p;
    [unroll] for (int k = 0; k < 2; k++) {
        q = p - SunDir.xyz * (max(p.z - z, 0.0) / SunDir.z);
        float3 s = ScreenPos(q);
        if (s.z <= 0 || s.z >= 1 || any(s.xy < 0) || any(s.xy > 1)) return false;  // (behind the eye, past the far)
        at = s.xy;
        float d = SceneDepth(at);
        if (d >= 1) return false;  // the sky there: nothing to ask
        z = WorldPos(at, d).z;
        // (the second guess: the land seen there is that ground, as far from the eye; nearer, it is what hides it)
        float far = ViewDepth(s.z);
        if (k == 1 && far - ViewDepth(d) > 8.0 + far * 0.05) return false;
    }
    return true;
}

// Whether something stands between p and the moon, as the screen shows it: points of the way from p towards the
// moon (as high as things stand) inside a thing (behind what is seen there, by less than a thing is thick). The
// ground's shade alone counted every point over a shaded patch as shaded, before the thing as well as behind it; the
// shade is only where the light's way is blocked. Closer points first, more of them (a thin trunk near p).
bool CasterAbove(float3 p) {
    float reach = min(max(140.0 - (p.z - GroundZ), 0.0) / SunDir.z, 500.0);
    bool hit = false;
    [unroll] for (int k = 1; k <= 8; k++) {
        float f = k / 8.0;
        float3 s = ScreenPos(p + SunDir.xyz * (reach * f * f));
        bool on = s.z > 0 && s.z < 1 && all(s.xy == saturate(s.xy));
        float z = ViewDepth(s.z);
        float behind = z - ViewDepth(SceneDepth(saturate(s.xy)));
        hit = hit || (on && behind > 1.0 && behind < 25.0 + z * 0.015);
    }
    return hit;
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
    const int N = 32;
    // The shade as far as the shadow map reaches (REACH, shaders.txt's Shadow=; the near 700 alone lost a far tower's
    // column), fading out over its last quarter (no edge where the map ends, moving with the camera); its steps
    // close together near the eye and further apart away from it (a far column is thin on the screen: the 4x4 block's
    // starts fill the gaps). The sky and the far land as far, so the shade has no step where they meet. The fog: the
    // whole way, in even steps.
    const float REACH = 5000.0;
    float shadeDist = min(dist, REACH);
    float step = dist / N;
    // Each pixel of a 4x4 block its own start along the steps, in order (no grain): raysblur.hlsl takes the block's
    // average.
    float start = Bayer4(floor(uv * ScreenSize.xy));
    float t = step * start;
    float shade = 0, fog = 0;
    [loop] for (int i = 0; i < N; i++) {
        fog += exp(-max(CameraPos.z + dir.z * t - GroundZ, 0.0) / FOGHEIGHT);
        // (along the way by the square of the step's share: its length there the square's slope)
        float f = (i + start) / N;
        float ts = shadeDist * f * f, span = shadeDist * 2.0 * f / N;
        float3 p = CameraPos.xyz + dir * ts;
        float h = max(p.z - GroundZ, 0.0);
        float dens = exp(-h / FOGHEIGHT);
        float2 at;
        float3 q;
        float l;
        if (ShadowInfo.w > 0) l = ShadowLit(p, ShadowInfo.z * 4.0);  // (some pixels of the map nearer: the ground itself is no shade)
        // Low enough to be shaded by what stands on the ground; higher, open.
        else if (!GroundUnder(p, at, q)) l = 1.0;
        else {
            l = LitAt(at);
            if (l < 0.99 && !CasterAbove(p)) l = 1.0;  // nothing between p and the moon: p in its light
            l = lerp(l, 1.0, saturate(h / (FOGHEIGHT * 3.0)));
        }
        shade += dens * (1.0 - l) * span * (1.0 - smoothstep(REACH * 0.75, REACH, ts));
        t += step;
    }
    // (as thick as the pack's fog: lit.hlsl's measure; the shade's columns are thin, so it counts more)
    float k = FOG * 0.0025;
    return float4(1.0 - exp(-fog * step * k), 1.0 - exp(-shade * k * 6.0), 0, 1);
#endif
}
