// Spooky: the night with its glow, rays and reflections: the colours' curve, cold shadows, warm fire (into Color;
// post.hlsl puts it on the screen).
#include "common.hlsl"

// Up to 0.65 as it is, then softly on: bright fire does not burn out white.
float3 Curve(float3 x) {
    float3 over = max(x - 0.65, 0);
    return min(x, 0.65) + over / (1.0 + over * 1.6);
}

#if LABPBR
// What a smooth surface reflects here (srefl.hlsl): rgb, a how much was met on the screen. A rougher surface's
// reflection blurred (5 reads up to 8 pixels apart; a smooth one's as sharp as the screen).
float4 SurfaceSeen(float2 uv, float4 surface) {
    float2 px = SceneSize.zw * (1.0 + Rough(surface) * 24.0);
    return tex2D(SRefl, uv) * 0.4 + (tex2D(SRefl, uv + float2(px.x, 0)) + tex2D(SRefl, uv - float2(px.x, 0)) +
                                     tex2D(SRefl, uv + float2(0, px.y)) + tex2D(SRefl, uv - float2(0, px.y))) * 0.15;
}
#endif

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float4 lit = tex2D(Lit, uv);
    float3 col = min(max(lit.rgb, 0), 64.0);
    float4 refl = tex2D(Refl, uv);  // (the water's reflections of the land)
#if LABPBR
    // A smooth surface of a resource pack's _s: what its ray met on the screen (srefl.hlsl), the night's haze where it
    // went off it; by Fresnel (a metal's in its own colour), the clearer the smoother.
    float4 surface = tex2D(WorldS, uv);
    if (surface.r >= 0.45 && SURFREFL > 0 && SceneDepth(uv) < 1) {
        float3 n = normalize(tex2D(WorldN, uv).rgb * 2.0 - 1.0);
        float3 v = -normalize(WorldPos(uv, 1) - CameraPos.xyz);
        float3 f0 = Reflectance(surface, tex2D(SolidTex, uv).rgb);
        float3 fresnel = f0 + (1.0 - f0) * pow(1.0 - saturate(dot(n, v)), 5.0);
        float4 met = SurfaceSeen(uv, surface);
        float3 seen = lerp(Haze() + MoonTint * 0.04, met.rgb, saturate(met.a));
        // (what it reflects takes the place of as much of its own light: a metal's mostly)
        float clear = saturate((surface.r - 0.45) * 2.5) * SURFREFL;
        col = col * (1.0 - Luma(fresnel) * clear) + seen * fresnel * clear;
        refl = 0;
    }
#endif
    col = lerp(col, refl.rgb, saturate(refl.a));
    float2 px = SceneSize.zw * 8.0;
    float3 wide = (tex2D(Wide, uv + float2(-px.x, -px.y)).rgb + tex2D(Wide, uv + float2(px.x, -px.y)).rgb +
                   tex2D(Wide, uv + float2(-px.x, px.y)).rgb + tex2D(Wide, uv + px).rgb) * 0.25;
    // The halo: around what glows, not over it again (a big shot near the camera, its own glow on it, burnt out
    // white): less what glows right here, as glow.hlsl takes it.
    float3 fx = max(tex2D(SceneTex, uv).rgb - tex2D(SolidTex, uv).rgb, 0);
    float3 hot = fx * saturate(Luma(fx) * 4.0 - 0.2);
    col += max(tex2D(GlowB, uv).rgb * 0.9 + wide * 0.7 - hot * 1.6, 0) * GLOW;
    // What a resource pack's maps make glow: as bright in the night as by day.
    col += tex2D(WorldG, uv).rgb * PACKGLOW;
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
    return float4(saturate(col), 1);
}
