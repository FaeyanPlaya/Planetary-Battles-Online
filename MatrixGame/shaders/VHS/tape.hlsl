// VHS: the picture as played from a tape: the brightness with its halos, the blurred colour run to the right, the
// noise of both, lines that shake a little, a tracking fault now and then, the worn bottom edge, fewer colours.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float px = PxX();
    float row = TapeLine(uv);
    float field = fmod(floor(Time.x * FIELDS), 1000.0);  // (small: the hashes keep their precision)
    // Each line a little to the side, as the tape's speed wavers.
    float shift = (Noise2(float2(row * 0.15, field * 0.7)) - 0.5) * JITTER * 2.5 * px;
    // A tracking fault: its band torn sideways, snow in it.
    float2 fault = Tracking();
    float y = uv.y;
    float band = fault.x * saturate(1.0 - abs(y - fault.y) / 0.06);
    shift += (Noise2(float2(row * 0.4, field)) - 0.3) * band * 90.0 * px;
    // The bottom edge of a tape's picture: the heads switching, a few lines torn to the right.
    float bottom = smoothstep(0.975, 1.0, y) * (0.3 + TRACKING);
    shift += bottom * (Noise2(float2(row * 0.5, field * 0.3)) + 0.5) * 25.0 * px;
    float2 at = uv + float2(shift, 0);
    // The brightness, with the halo the tape's sharpening leaves; the colour, blurred and run on.
    float2 yy = tex2D(TapeY, at).rg;
    float lum = yy.r + clamp((yy.r - yy.g) * SHARPEN * 0.8, -0.12, 0.12);  // (a fringe, not a black hole in it)
    float2 iq = tex2D(TapeC, at).rg;
    // Noise: the brightness's in linear light (so it shows in the dark and fades out over the bright), soft grains
    // about 4 pixels across; blots about 16 across in the colour, stronger in the dark; a new pattern every
    // half-frame.
    float2 pix = at * SceneSize.xy * (1080.0 / SceneSize.y);  // (pixels at 1080 rows)
    float2 move = frac(float2(field * 0.618, field * 0.382)) * 517.0;
    lum += (Noise2(pix / 4.0 + move) - 0.5) * LUMANOISE * 0.5;
    float dark = 1.0 - saturate(lum * 3.0);
    iq += (float2(Noise2(pix / 16.0 + move * 1.3), Noise2(pix / 16.0 + move * 1.7 + 31.0)) - 0.5) * CHROMANOISE *
          lerp(0.2, 0.5, dark);
    // Snow in the fault and at the bottom edge.
    float snow = Hash12(float2(floor(at.x / (px * 3.0)), row + field * 31.0));
    lum = lerp(lum, 1.0, step(1.0 - (band * 0.25 + bottom * 0.03), snow) * 0.9);
    lum = lerp(lum, lum * 0.7 + 0.25, band * 0.6);  // (the band washed out)
    iq *= 1.0 - band * 0.7;
    // The brightness back to the picture's gamma, the colour with it.
    float3 col = ToLinear(saturate(FromYIQ(float3(pow(saturate(lum), 1.0 / 2.2), iq))));
    // The colour depth: each channel in COLOURDEPTH steps of linear light (coarse in the dark), as the original
    // pack; the steps' edges dithered.
    col = floor(col * COLOURDEPTH + Bayer4(uv * SceneSize.xy)) / COLOURDEPTH;
    col = FromLinear(col);
    // Faded: the colour pales, the blacks lift and the whites dim a little, a warm cast over it all.
    col = lerp(col, Luma(col).xxx, FADE * 0.45);
    col = col * (1.0 - FADE * 0.16) + FADE * 0.07;
    col *= float3(1.0 + FADE * 0.04, 1.0 + FADE * 0.01, 1.0 - FADE * 0.06);
    return float4(saturate(col), 1);
}
