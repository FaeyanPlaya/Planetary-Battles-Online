// Vanilla+: what the passes share.
#ifndef VANILLA_COMMON
#define VANILLA_COMMON

#define GroundZ MapInfo.x  // the water's level

float Hash12(float2 p) {
    float3 p3 = frac(p.xyx * 0.1031);
    p3 += dot(p3, p3.yzx + 33.33);
    return frac((p3.x + p3.y) * p3.z);
}

// How bright a fire is now: around 1, each its own way (a gentle tremble).
float Flicker(float seed, float t) {
    return 1.0 + (sin(t * 11.0 + seed * 40.0) * 0.6 + sin(t * 23.7 + seed * 91.0) * 0.4) * 0.12;
}

// Where the view meets the water (the game draws it without depth: the bottom under it is what the depth holds).
float WaterAt(float3 dir) { return CameraPos.z > GroundZ && dir.z < -1e-4 ? (GroundZ - CameraPos.z) / dir.z : 1e9; }

// Softly to its end: a strong light does not burn out.
float3 Soft(float3 x) { return x / (1.0 + x * 0.6); }

// What the effects add that glows (fire, flashes, shots; not smoke).
float3 Hot(float3 scene, float3 solid) {
    float3 fx = max(scene - solid, 0);
    return fx * saturate(Luma(fx) * 4.0 - 0.2);
}

// An ordered pattern over 4x4 pixels: each of 16 even steps of 0..1 once in any 4x4 block.
float Bayer2(float2 a) {
    a = floor(a);
    return frac(a.x * 0.5 + a.y * a.y * 0.75);
}
float Bayer4(float2 a) { return Bayer2(a * 0.5) * 0.25 + Bayer2(a) + 0.03125; }

// The sun's rays (rays.hlsl, as Spooky's): a haze low over the ground; the water's reflections (refl.hlsl) of the
// game's own picture.
#define FOG 1.0
#define FOGHEIGHT 60.0
// LabPBR materials: a resource pack's maps (world.hlsl's WorldS, the shine, the surfaces' reflections).
#define LABPBR (PBR == 2)
#define REFL_SOURCE SceneTex

// The words of the settings that choose (the engine writes `#define TONEMAP ACES`).
#define None 0
#define Simple 1
#define Reinhard 2
#define ACES 3
#define Protanopia 1
#define Deuteranopia 2
#define Tritanopia 3
#define Default 0
#define Panini 1

// The shine of a light on a surface (GGX, Smith's shadowing, Schlick's Fresnel, as Spooklementary and I Like Vanilla
// light it): how much of the light comes off it towards the eye. n: the surface's normal, v: towards the eye, l:
// towards the light (each of length 1); rough: LabPBR's roughness, (1 - smoothness) squared; f0: how much it
// reflects head on (a metal: its colour). 0 for a light behind the surface.
float3 Shine(float3 n, float3 v, float3 l, float rough, float3 f0) {
    float nl = saturate(dot(n, l));
    float3 h = normalize(v + l);
    float nv = max(dot(n, v), 0.02), nh = saturate(dot(n, h)), vh = saturate(dot(v, h));
    float a2 = rough * rough;
    float q = nh * nh * (a2 - 1.0) + 1.0;
    float spread = a2 / (3.14159265 * q * q);
    float k = rough * 0.5;
    float seen = 1.0 / ((nl * (1.0 - k) + k) * (nv * (1.0 - k) + k));
    float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - vh, 5.0);
    return fresnel * (spread * seen * 0.25 * nl);
}

// A resource pack's surface as world.hlsl keeps it (WorldS): its roughness (from _s's smoothness, never quite a
// mirror), whether it is a metal (_s's green 230 and up), its F0 (a metal's: the picture's own colour, `tint`).
float Rough(float4 s) { return max((1.0 - s.r) * (1.0 - s.r), 0.06); }
float Metal(float4 s) { return s.g > 229.5 / 255.0 ? 1.0 : 0.0; }
float3 Reflectance(float4 s, float3 picture) {
    float3 tint = picture / max(max(picture.r, max(picture.g, picture.b)), 0.05);
    return lerp(s.ggg, tint * 0.9, Metal(s));
}

#endif

// Smooth noise over the world: 0..1.
float Noise(float2 p) {
    float2 i = floor(p), f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(Hash12(i), Hash12(i + float2(1, 0)), f.x), lerp(Hash12(i + float2(0, 1)), Hash12(i + 1.0), f.x),
                f.y);
}
