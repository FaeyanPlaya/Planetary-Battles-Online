// Spooky: the night, the lights, the fog and the haze far away; the effects on top.
#include "common.hlsl"

// Half's lights and shade here: of its 4 nearest, those about as far away as this pixel.
float4 HalfAt(float2 uv, float z) {
    float2 px = SceneSize.zw;
    float4 sum = 0;
    float weight = 0;
    [unroll] for (int i = 0; i < 4; i++) {
        float2 at = uv + float2(i % 2 == 0 ? -px.x : px.x, i < 2 ? -px.y : px.y);
        float w = 1.0 / (1.0 + abs(ViewDepth(SceneDepth(at)) - z) * 0.3);
        sum += tex2Dlod(Half, float4(at, 0, 0)) * w;
        weight += w;
    }
    return sum / weight;
}

// Smoothed 1/16 glow (4 reads half its pixel apart).
float3 WideAt(float2 uv) {
    float2 px = SceneSize.zw * 8.0;
    return (tex2D(Wide, uv + float2(-px.x, -px.y)).rgb + tex2D(Wide, uv + float2(px.x, -px.y)).rgb +
            tex2D(Wide, uv + float2(-px.x, px.y)).rgb + tex2D(Wide, uv + px).rgb) * 0.25;
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float3 scene = tex2D(SceneTex, uv).rgb;
    float3 solid = tex2D(SolidTex, uv).rgb;
    float3 fx = scene - solid;  // what the effects added (smoke: below 0 too)
    float d = SceneDepth(uv);
    float3 dir = normalize(WorldPos(uv, 1) - CameraPos.xyz);
    float3 haze = Haze();
    // Where the view ends: the land, or the water's surface over it (out past the map's edge too).
    float water = WaterAt(dir);
    float reach = d < 1 ? length(WorldPos(uv, d) - CameraPos.xyz) : 1e9;
    float dist = min(reach, water);
    float3 col;
    float air;  // how much air the view goes through: where the moon's rays show (alpha, for final)
    if (dist > 1e8) {
        // The sky: the night's, into the haze down at the horizon (where the far land and water go into it too).
        col = lerp(SkyNight(solid), haze, saturate(1.0 - dir.z * 5.0));
        air = saturate(1.0 - dir.z * 5.0) * 0.7;
    }
    else {
        float3 p = CameraPos.xyz + dir * dist;
        float4 h = d < 1 ? HalfAt(uv, ViewDepth(d)) : float4(0, 0, 0, 1);
        // The game's volumetric shadow (SolidTex.a 0 where it fell): taken out here and laid again over the fog, as
        // dark as the map makes it (under the night's fog it was all but gone). Not on the fires' light.
        float lay = State.w > 0.5 ? (1.0 - tex2D(SolidTex, uv).a) * ShadowColor.a : 0.0;
        float3 bare = lay > 0.001 ? max(solid - ShadowColor.rgb * lay, 0) / max(1.0 - lay, 0.05) : solid;
        float shadow = lay > 0.001 ? saturate(Luma(solid) / max(Luma(bare), 1e-3)) : 1.0;
        // The textures' relief (generated normals: the picture's own light and dark as height): a slope facing a
        // light brighter, one facing away darker, under the moon and under the battle's lights (where they come
        // from: HalfDir). Not on the water's surface.
        float moonRelief = 1.0, lightRelief = 1.0;
        if (NORMALS > 0 && d < 1 && reach <= water) {
            float3 slope = ReliefSlope(uv) * (NORMALS * 6.0);  // (as GeneratedNormal bends by)
            float4 from = tex2D(HalfDir, uv);
            moonRelief = clamp(1.0 - dot(slope, SunDir.xyz) * 1.2, 0.45, 1.6);
            lightRelief = clamp(1.0 - dot(slope, from.rgb / max(from.a, 1e-4)) * 1.8, 0.3, 1.8);
        }
        float3 moon = Night(bare) * h.a * moonRelief;
        // The lights and what glows on the screen near it.
        h.rgb *= lightRelief;
        float3 glow = tex2D(GlowB, uv).rgb * 0.6 + WideAt(uv) * 0.9;
        // Not under the glowing thing itself: a shot near the camera is far above the ground behind it, and drawn
        // over its own light it burnt out white.
        float3 own = max(fx, 0);
        glow *= 1.0 - saturate(max(own.r, max(own.g, own.b)) * 2.5) * 0.85;
        // (what glows on the screen is no light of a place: it does not fill the shadow; the fires do)
        float3 light = Soft((h.rgb + glow * shadow) * FIRELIGHT * 1.6);
        float3 fire = (bare * 0.8 + 0.05) * light;
        // The ground fog: its thickness along the view (as much at a high camera as at a low one), drifting.
        const float H = FOGHEIGHT;  // how high it reaches (it thins out upwards: a third of it at this height)
        float hc = max(CameraPos.z - GroundZ, 0.0), hp = max(p.z - GroundZ, 0.0);
        float ec = exp(-hc / H), ep = exp(-hp / H), dh = hp - hc;
        float column = abs(dh) > 0.01 ? H * (ec - ep) / dh : ec;
        float2 at = p.xy * 0.01 + Time.y * float2(0.02, 0.013);
        float drift = Noise2(at) * 0.6 + Noise2(at * 2.7 + 5.1) * 0.4;
        float fog = 1.0 - exp(-dist * column * FOG * 0.0025 * lerp(0.5, 1.5, drift));
        // The far end of the view goes into the haze, wherever the view ends (the far view mod moves it): the
        // game's own fog is off (Game.Fog), this is its place.
        float far = smoothstep(FogRange.z * 0.45, FogRange.z * 0.93, dist);
        fog = max(fog, far);
        float s = lerp(shadow, 1.0, far);  // (far off it goes into the haze with the rest)
        col = lerp(moon * s + fire, haze * s + light * 0.1, saturate(fog));
        air = saturate(fog);
    }
    // The effects: what glows as it is, the rest (smoke, dust) in the night's light; what they darken grey. What they
    // add: its own colour where it stands out from what is behind it (a shot: green over a bright sky as over the
    // ground); a faint difference from a bright place in the picture's colour there (a flame drawn over an orange sky
    // differs from it by a green: added so to the night's sky it would turn the flame's edge green).
    float3 up = max(fx, 0), down = min(fx, 0);
    float top = max(up.r, max(up.g, up.b));
    float3 hue = scene / max(max(scene.r, max(scene.g, scene.b)), 1e-3);
    float behind = max(solid.r, max(solid.g, solid.b));
    float own = saturate(top / max(behind, 0.05) * 1.5 - 0.2);
    float3 added = lerp(hue * top, up, own) + Luma(down).xxx;
    float hot = saturate((top - 0.08) * 3.0);
    col += lerp(added * (1.0 - NIGHT), added, hot);
    return float4(min(max(col, 0), 64.0), air);
}
