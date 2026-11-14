// VHS: what the passes share.
#ifndef VHS_COMMON
#define VHS_COMMON

#define GroundZ MapInfo.x  // the water's level
#define FIELDS (INTERLACERATE * 2.0)  // the tape's half-frames a second

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

// An ordered pattern over 4x4 pixels: each of 16 even steps of 0..1 once in any 4x4 block.
float Bayer2(float2 a) {
    a = floor(a);
    return frac(a.x * 0.5 + a.y * a.y * 0.75);
}
float Bayer4(float2 a) { return Bayer2(a * 0.5) * 0.25 + Bayer2(a) + 0.03125; }

// The screen's pixels to the tape's: a tape has about 540 lines a picture (2 screen rows each at 1080).
float LinePx() { return max(1.0, floor(SceneSize.y / 540.0)); }
float TapeLine(float2 uv) { return floor(uv.y * SceneSize.y / LinePx()); }
// A screen pixel at 1080 rows, in uv across (wider screens keep the tape's look).
float PxX() { return SceneSize.y / 1080.0 * SceneSize.z; }

// Brightness and colour (YIQ: what a tape keeps apart, the colour much blurrier) and back.
float3 ToYIQ(float3 c) {
    return float3(dot(c, float3(0.299, 0.587, 0.114)), dot(c, float3(0.596, -0.274, -0.322)),
                  dot(c, float3(0.211, -0.523, 0.312)));
}
float3 FromYIQ(float3 y) {
    return float3(dot(y, float3(1.0, 0.956, 0.621)), dot(y, float3(1.0, -0.272, -0.647)),
                  dot(y, float3(1.0, -1.106, 1.703)));
}

// The tape works in linear light (the picture's gamma 2.2 taken off): noise and blur there show in the dark and
// wash the colours out, as the tape's look has it.
float3 ToLinear(float3 c) { return pow(max(c, 0), 2.2); }
float3 FromLinear(float3 c) { return pow(max(c, 0), 1.0 / 2.2); }

// Where the view meets the water (the game draws it without depth: the bottom under it is what the depth holds).
float WaterAt(float3 dir) { return CameraPos.z > GroundZ && dir.z < -1e-4 ? (GroundZ - CameraPos.z) / dir.z : 1e9; }

// A tracking fault now: how strong (0 none) and where its band is (y, 0 top). Once in a while, for a moment.
float2 Tracking() {
    const float period = 9.0;
    float k = floor(Time.x / period), t = frac(Time.x / period) * period;
    float start = Hash12(float2(k, 3.7)) * (period - 1.5), span = 0.4 + Hash12(float2(k, 9.1)) * 0.8;
    float on = Hash12(float2(k, 1.3)) < 0.6 ? smoothstep(start, start + 0.1, t) * (1.0 - smoothstep(start + span, start + span + 0.15, t)) : 0.0;
    float y = frac(Hash12(float2(k, 5.5)) + (t - start) * 0.35);
    return float2(on * saturate(TRACKING * 1.4), y);
}

#endif
