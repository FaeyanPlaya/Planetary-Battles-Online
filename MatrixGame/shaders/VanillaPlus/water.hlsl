// Vanilla+: the game's water, without its repeats. The game's texture is read twice more, turned and at other sizes,
// drifting their own ways; a slow noise over the world chooses among the three, so no tile is the same as the one
// beside it; a broad, faint change of brightness over the sea besides. All else as the game draws it (its light, its
// reflection of the sky, its shore).
#include "common.hlsl"

float4 main(Water w) {
    if (WATERMIX <= 0) return w.color;
    float t = Time.y;
    float2 a = mul(w.uv, float2x2(0.8, -0.6, 0.6, 0.8)) * 0.61 + float2(0.37, 0.11) + float2(t, -t) * 0.004;
    float2 b = mul(w.uv, float2x2(-0.5, 0.866, -0.866, -0.5)) * 1.37 + float2(0.17, 0.53) - float2(t, t * 0.6) * 0.006;
    float3 ta = tex2D(WaterTex, a).rgb, tb = tex2D(WaterTex, b).rgb;
    // Which of the three where: each strongest somewhere, about a tile across (the world's units).
    float2 q = w.pos.xy * 0.012;
    float3 k = float3(Noise(q), Noise(q + 17.3), Noise(q - 31.7));
    k = k * k * k;
    k /= k.x + k.y + k.z;
    float3 tex = w.tex.rgb * k.x + ta * k.y + tb * k.z;
    // (three mixed are flatter than one: their contrast back round their middle)
    float3 mid = (w.tex.rgb + ta + tb) / 3.0;
    tex = mid + (tex - mid) * 1.35;
    // The texture's part of the game's colour taken for this one's.
    float3 col = w.color.rgb + (tex - w.tex.rgb) * w.light * WATERMIX;
    col *= lerp(1.0, 0.9 + 0.2 * Noise(w.pos.xy * 0.0025 + 5.1), WATERMIX);
    return float4(max(col, 0), w.color.a);
}
