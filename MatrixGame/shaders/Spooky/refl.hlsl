// At half size: what the water's surface reflects that the game's water does not: the land, the robots and the
// buildings on the screen above it (screen-space reflections). rgb: the reflected colour (REFL_SOURCE, the pack's
// picture), a: how much of it (0: nothing found; the game's own reflection, the sky, stays).
#include "common.hlsl"

// How far the reflected ray at t is behind what the screen shows there (over 0: behind it); -1e9 off the screen.
// `z`: how far from the eye the ray is there.
float Behind(float3 p, float3 r, float t, out float2 at, out float z) {
    float3 s = ScreenPos(p + r * t);
    at = s.xy;
    z = ViewDepth(s.z);
    // (no early return: the June 2010 compiler got the march's loop wrong with one, every ray "off the screen")
    bool off = s.z <= 0 || s.z >= 1 || any(s.xy != saturate(s.xy));
    float b = z - ViewDepth(SceneDepth(saturate(s.xy)));
    return off ? -1e9 : b;
}
float Behind(float3 p, float3 r, float t, out float2 at) {
    float z;
    return Behind(p, r, t, at, z);
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    if (REFLECT <= 0) return 0;
    uv = ScenePixel(uv);
    float3 dir = normalize(WorldPos(uv, 1) - CameraPos.xyz);
    float d = SceneDepth(uv);
    float water = WaterAt(dir);
    // The water there: the depth holds the bottom under it, or its own surface with the waves (2.6 up and down: the
    // far view and a world pass draw it into the depth); anything higher stands above the water.
    if (water > 1e8 || (d < 1 && WorldPos(uv, d).z > GroundZ + 3.5)) return 0;
    float3 p = CameraPos.xyz + dir * water;
    // Only where the game's water shows (not a road or a base's floor over land under it), as strong as it is there
    // (barely over a shallow flood, where it is nearly clear).
    float wet = WaterSeen(uv);
    if (wet <= 0) return 0;
    // The waves bend it a little.
    float2 w = float2(sin(p.x * 0.09 + Time.y * 1.3), sin(p.y * 0.11 + Time.y * 1.1)) * 0.02;
    float3 r = reflect(dir, normalize(float3(w, 1)));
    // Along the reflected ray in steps that grow (near: fine, far: the whole island in reach) until it goes behind
    // what the screen shows; then halved 6 times to where it does.
    // From over the waves' crests (2.6 over the water; the depth may hold the water's own surface with its waves, and
    // a ray under them met the water at once and reflected the water itself).
    if (r.z < 0.003) return 0;
    float start = max(4.0, (GroundZ + 3.0 - p.z) / r.z);
    // (the first sample behind kept, the loop run to its end: with a break there the June 2010 compiler lost the hits)
    // A step that went behind what the screen shows by more than the ray itself went away in it passed behind
    // something nearer (a palm, a robot, a wall: what stands before the water on the screen): not a hit, the march
    // goes on past it (it stopped there, and the water behind such a thing reflected nothing).
    float before = start * 0.5, t = start, step = max(4.0, start * 0.25), lo = 0, hi = -1;
    float2 at = 0;
    float zBefore;
    Behind(p, r, before, at, zBefore);
    bool gone = false;
    [loop] for (int i = 0; i < 40; i++) {
        float z;
        float b = Behind(p, r, t, at, z);
        gone = gone || b < -1e8;  // off the screen
        if (!gone && hi < 0 && b > 0 && b < abs(z - zBefore) + 6.0 + t * 0.04) {
            lo = before;
            hi = t;
        }
        before = t;
        zBefore = z;
        t += step;
        step *= 1.25;
    }
    if (hi < 0) return 0;
    [unroll] for (int k = 0; k < 6; k++) {
        float mid = (lo + hi) * 0.5;
        if (Behind(p, r, mid, at) > 0) hi = mid;
        else lo = mid;
    }
    // Behind it by much: the ray passed behind something (a palm, a robot) rather than into it.
    if (Behind(p, r, hi, at) > 6.0 + hi * 0.04) return 0;
    // What it met: the water again (its surface in the depth) reflects nothing new.
    if (WorldPos(at, SceneDepth(at)).z < GroundZ + 3.0) return 0;
    float3 col = tex2Dlod(REFL_SOURCE, float4(at, 0, 0)).rgb;
    float2 edge = min(at, 1 - at);
    float fade = saturate(min(edge.x, edge.y) * 12.0) * saturate(1.5 - hi / 4000.0);
    float fresnel = lerp(0.35, 1.0, pow(1.0 - abs(dir.z), 3.0));
    return float4(col, fade * fresnel * wet * REFLECT);
}
