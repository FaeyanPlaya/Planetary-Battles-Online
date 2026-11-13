// Spooky: the night with its glow and rays onto the screen: the colours' curve, cold shadows, warm fire, the edges.
#include "common.hlsl"

// Up to 0.65 as it is, then softly on: bright fire does not burn out white.
float3 Curve(float3 x) {
    float3 over = max(x - 0.65, 0);
    return min(x, 0.65) + over / (1.0 + over * 1.6);
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float4 lit = tex2D(Lit, uv);
    float3 col = min(max(lit.rgb, 0), 64.0);
    float2 px = SceneSize.zw * 8.0;
    float3 wide = (tex2D(Wide, uv + float2(-px.x, -px.y)).rgb + tex2D(Wide, uv + float2(px.x, -px.y)).rgb +
                   tex2D(Wide, uv + float2(-px.x, px.y)).rgb + tex2D(Wide, uv + px).rgb) * 0.25;
    // The halo: around what glows, not over it again (a big shot near the camera, its own glow on it, burnt out
    // white): less what glows right here, as glow.hlsl takes it.
    float3 fx = max(tex2D(SceneTex, uv).rgb - tex2D(SolidTex, uv).rgb, 0);
    float3 hot = fx * saturate(Luma(fx) * 4.0 - 0.2);
    col += max(tex2D(GlowB, uv).rgb * 0.9 + wide * 0.7 - hot * 1.6, 0) * GLOW;
#if SHAFTS
    // The moon's rays in the fog: its light where it gets through, the shade where something stands in its way, the
    // more fog the view goes through the more; brighter looking towards the moon (its light scatters forwards).
    float2 rays = tex2D(RaysB, uv).rg;
    float3 view = normalize(WorldPos(uv, 1) - CameraPos.xyz);
    float phase = 0.6 + 0.8 * saturate(dot(view, SunDir.xyz));
    float3 moonlight = lerp(Haze(), MoonTint * 0.5, 0.5);
    // Darker where the view goes through the shade (the night's fog is too dark to take its own light out: what is
    // seen through it darkens, never below 40%), a little more moonlight in the lit fog.
    col *= 1.0 - saturate(rays.g * phase * RAYS * 3.0) * 0.6;
    col += moonlight * rays.r * phase * RAYS * 0.25;
#endif
    col = Curve(col);
    // Cold in the dark, warm where it is bright.
    float l = Luma(col);
    col = lerp(col * float3(0.93, 0.96, 1.08), col * float3(1.07, 0.99, 0.88), saturate(l * 1.6));
    // The edges darker.
    float2 v = uv - 0.5;
    col *= 1.0 - VIGNETTE * 0.6 * saturate(dot(v, v) * 2.2);
    // Grain: a little brighter or darker, never down to black.
    float noise = Hash12(uv * ScreenSize.xy + frac(Time.x * 7.31) * 911.0) - 0.5;
    col = col * (1.0 + noise * GRAIN * 0.16) + GRAIN * 0.004;
    return float4(saturate(col), 1);
}
