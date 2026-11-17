// Spooky's camera: the picture (after the temporal filter) onto the screen: antialiasing, depth of field and motion
// blur, colour fringes, sharpness, lens flares, the colours' balance and grading, the edges, the grain.
#include "common.hlsl"
#include "postfx.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
#if FXAA
    float3 col = Fxaa(Taa, uv, SceneSize.zw);
#else
    float3 col = tex2D(Taa, uv).rgb;
#endif
    col = Aberration(Taa, uv, col, ABERRATION);
    col = WorldBlur(Taa, uv, col, DOF ? DOFSTRENGTH : 0.0, MBLUR ? MBLURSTRENGTH : 0.0, FringeAt(uv, ABERRATION));
    col = Sharpen(Taa, uv, col, SHARPEN);
    if (FLARE > 0) col += tex2D(Flare, uv).rgb;
    // The colours' balance: brightness, contrast round the middle grey, saturation.
    col *= BRIGHTNESS;
    col = (col - 0.5) * CONTRAST + 0.5;
    col = lerp(Luma(col).xxx, col, SATURATION);
#if GRADING
    // Each channel out of all three (as the grading of the original pack).
    col = float3(dot(col, float3(MIX_RR, MIX_RG, MIX_RB)), dot(col, float3(MIX_GR, MIX_GG, MIX_GB)),
                 dot(col, float3(MIX_BR, MIX_BG, MIX_BB)));
#endif
    col = Vignette(uv, max(col, 0), VIGNETTE);
    // Grain: a little brighter or darker, never down to black.
    float noise = Hash12(uv * ScreenSize.xy + frac(Time.x * 7.31) * 911.0) - 0.5;
    col = col * (1.0 + noise * GRAIN * 0.16) + GRAIN * 0.004;
    return float4(saturate(col), 1);
}
