// Vanilla+: the game's own picture, a little richer: shaded corners, short shadows and the sun's real shadows, the
// relief of the textures under the sun, light on the bottom under the water, the warm light of fires, the outlines
// and the cel bands, the water's reflections, a soft glow of what is bright, the sun's rays in the haze. The camera's
// effects and the colours: post.hlsl.
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

// Light through the waves on the bottom: bright lines that move.
float Caustic(float2 p, float t) {
    float v = sin(p.x + t) + sin(p.y * 1.3 - t * 0.8) + sin((p.x + p.y) * 0.7 + t * 1.1) +
              sin((p.x - p.y) * 0.9 - t * 0.6);
    return pow(1.0 - abs(sin(v * 1.2)), 6.0);
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float3 scene = tex2D(SceneTex, uv).rgb;
    float3 solid = tex2D(SolidTex, uv).rgb;
    float3 fx = scene - solid;  // what the effects added (smoke: below 0 too)
    float d = SceneDepth(uv);
    float3 col = solid;
    float3 dir = normalize(WorldPos(uv, 1) - CameraPos.xyz);
    if (d < 1) {
        float3 p = WorldPos(uv, d);
        float reach = length(p - CameraPos.xyz), water = WaterAt(dir);
        float dist = min(reach, water);
        // Into the game's fog all of it fades: far off the fog's colour is what shows.
        float near = FogColor.w > 0.5 ? 1.0 - smoothstep(FogRange.x, FogRange.y, dist) : 1.0;
        float4 h = HalfAt(uv, ViewDepth(d));
        float shade = lerp(1.0, h.a, near);
        // The textures' relief under the map's sun (their light and dark taken for height): not on the water.
        // A resource pack's _n where it has one (world.hlsl, WorldG's alpha): its relief instead.
        float packed = tex2D(WorldG, uv).a;
        if (packed > 0.005 && reach <= water) shade *= lerp(1.0, packed * 2.0, near);
        else if (PBR == 1 && RELIEF > 0 && reach <= water) {  // (generated normals: Internal only)
            float3 slope = ReliefSlope(uv) * (RELIEF * 6.0);
            shade *= lerp(1.0, clamp(1.0 - dot(slope, SunDir.xyz) * 0.8, 0.75, 1.25), near);
        }
#if SHADOWMAP
        // The sun's real shadows (the shadow map): where the game's own shadows are not. 8 reads on a disc two of the
        // map's pixels wide, turned each pixel (the temporal filter smooths what is left: no squares of the map's
        // pixels); towards the map's edge they fade out (no line where it ends when the camera is far out).
        if (ShadowInfo.w > 0 && reach <= water) {
            float b = ShadowInfo.z * 3.5, o = ShadowInfo.z * 3.0;
            float turn = Bayer4(uv * SceneSize.xy) * 6.2831853;
            float lit = 0;
            [unroll] for (int k = 0; k < 12; k++) {
                float a = turn + k * 2.3999632, f = sqrt((k + 0.5) / 12.0) * o;
                lit += ShadowLit(p + float3(cos(a) * f, sin(a) * f, 0), b);
            }
            lit /= 12.0;
            float2 at = ShadowPos(p).xy, edge = min(at, 1.0 - at);
            float inside = saturate(min(edge.x, edge.y) * 8.0);
            float open = State.w > 0.5 ? tex2D(SolidTex, uv).a : 1.0;
            shade *= 1.0 - (1.0 - lit) * open * near * inside * 0.4;
        }
#endif
        col *= shade;
        // The bottom under the water: the light the waves gather, less the deeper. Only where the game's water shows
        // and the point lies under its level (not the shore's relief, a road or the water's own surface, which the
        // view's ray met beyond the water's plane at a low camera).
        float under = CAUSTICS > 0 && reach > water ? WaterSeen(uv) * saturate((GroundZ - 0.5 - p.z) / 2.0) : 0.0;
        if (under > 0) {
            float below = max(GroundZ - p.z, 0.0);
            float2 q = p.xy * 0.07 + float2(Noise(p.xy * 0.004), Noise(p.xy * 0.004 + 17.3)) * 9.0;
            float2 q2 = mul(q, float2x2(0.6, -0.8, 0.8, 0.6)) * 1.37 + 3.7;
            float c = lerp(Caustic(q, Time.y * 1.5), Caustic(q2, Time.y * 1.3), smoothstep(0.3, 0.7, Noise(p.xy * 0.006 - 9.1)));
            c *= lerp(0.35, 1.2, Noise(p.xy * 0.003 + 5.1));
            col *= 1.0 + c * CAUSTICS * 0.6 * exp(-below / 25.0) * near * under;
        }
        // The fires' light: warm, on what is near them.
        float3 light = Soft(h.rgb * FIRELIGHT) * near;
        col += (solid * 1.1 + 0.02) * light;
        // Cel shading: the brightness in 4 bands.
        if (CEL > 0) {
            float l = Luma(col), f = l * 4.0;
            float q = (floor(f) + smoothstep(0.4, 0.6, frac(f))) / 4.0;
            col *= lerp(1.0, q / max(l, 0.01), CEL);
        }
    }
    // Outlines: a dark line along the edge of whatever stands before something farther.
    if (OUTLINES > 0) {
        float2 px = SceneSize.zw;
        float z = ViewDepth(min(d, 0.9999)), far = 0;
        far = max(far, ViewDepth(min(SceneDepth(uv + float2(px.x, 0)), 0.9999)));
        far = max(far, ViewDepth(min(SceneDepth(uv - float2(px.x, 0)), 0.9999)));
        far = max(far, ViewDepth(min(SceneDepth(uv + float2(0, px.y)), 0.9999)));
        far = max(far, ViewDepth(min(SceneDepth(uv - float2(0, px.y)), 0.9999)));
        col *= 1.0 - saturate(((far - z) / z - 0.05) * 6.0) * OUTLINES * 0.7;
    }
    // The water's reflections of the land and the things on it (refl.hlsl).
    float4 refl = tex2D(Refl, uv);
    col = lerp(col, refl.rgb, saturate(refl.a));
    // The effects as the game drew them.
    col += fx;
    // The glow: around what is bright, not over it again.
    float3 glow = tex2D(GlowB, uv).rgb * 0.8 + WideAt(uv) * 0.8;
    col += max(glow - Hot(scene, solid) * 1.2, 0) * GLOW;
#if SHAFTS
    // The sun's rays in the haze: the shade of what stands in its way, its light where it gets through, the more
    // looking towards the sun.
    float2 rays = tex2D(RaysB, uv).rg;
    float phase = 0.6 + 0.8 * saturate(dot(dir, SunDir.xyz));
    col *= 1.0 - saturate(rays.g * phase * RAYS * 2.0) * 0.35;
    col += SunColor.rgb * max(rays.r - rays.g, 0) * phase * RAYS * 0.35;
#endif
    col += tex2D(WorldG, uv).rgb * PACKGLOW;  // what a resource pack's maps make glow
    return float4(max(col, 0), 1);
}
