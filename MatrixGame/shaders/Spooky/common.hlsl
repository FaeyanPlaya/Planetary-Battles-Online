// Spooky: what the passes share.
#ifndef SPOOKY_COMMON
#define SPOOKY_COMMON

static const float3 MoonTint = float3(0.62, 0.72, 1.0);   // the night's light
static const float3 FogTint = float3(0.16, 0.165, 0.22);  // the fog and the haze far away
#define GroundZ MapInfo.x                                 // the water's level: the fog lies on it

// An ordered pattern over 4x4 pixels: each of 16 even steps of 0..1 once in any 4x4 block.
float Bayer2(float2 a) {
    a = floor(a);
    return frac(a.x * 0.5 + a.y * a.y * 0.75);
}
float Bayer4(float2 a) { return Bayer2(a * 0.5) * 0.25 + Bayer2(a) + 0.03125; }

float Hash12(float2 p) {
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

float Noise2(float2 p) {
    float2 i = floor(p), f = frac(p);
    float2 u = f * f * (3.0 - 2.0 * f);
    float a = Hash12(i), b = Hash12(i + float2(1, 0)), c = Hash12(i + float2(0, 1)), d = Hash12(i + float2(1, 1));
    return lerp(lerp(a, b, u.x), lerp(c, d, u.x), u.y);
}

// How bright a fire is now: around 1, each its own way (two waves: cheap for every light at every pixel).
float Flicker(float seed, float t) {
    float w = sin(t * 11.0 + seed * 40.0) * 0.6 + sin(t * 23.7 + seed * 91.0) * 0.4;
    return 1.0 + w * FLICKER * 0.2;
}

// The night: the day's picture darker, greyer, colder.
float3 Night(float3 c) {
    float3 grey = lerp(c, Luma(c).xxx, NIGHT * 0.9);
    return grey * lerp(float3(1, 1, 1), MoonTint, saturate(NIGHT * 1.6)) * (1.0 - NIGHT);
}

// The sky further into the night than the land, its sun and bright clouds tamed (0: a little, as at dusk).
float3 SkyNight(float3 c) {
    float k = saturate(NIGHT * 1.5 + 0.2);
    float3 grey = lerp(c, Luma(c).xxx, k * 0.75);
    grey /= 1.0 + Luma(grey) * k * 1.5;
    return grey * lerp(float3(1, 1, 1), MoonTint, k) * (1.0 - k * 0.6);
}

// The fog's and the haze's colour: the map's sky just above the horizon (its skybox's, not its fog colour, which may
// be far from it) in the night's light as the land has it (what the far land goes into: the sky's own, darker, put
// it out on a bright map), so the far land, the water and the sky meet in one colour.
float3 Haze() { return lerp(Night(SkyHorizon.rgb), FogTint, 0.35); }

// Where the view meets the water (the game draws it without depth: the bottom under it is what the depth holds).
// dir: the view's, the camera above the water; far when it never does.
float WaterAt(float3 dir) { return CameraPos.z > GroundZ && dir.z < -1e-4 ? (GroundZ - CameraPos.z) / dir.z : 1e9; }

// Softly to its end: a strong light does not burn out.
float3 Soft(float3 x) { return x / (1.0 + x * 0.6); }

#endif
