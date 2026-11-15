// Vanilla+'s camera: the picture (after the temporal filter) onto the screen. The projection and the retro screen's
// bulge and pixels first (where each screen pixel reads); then antialiasing, the painted look, depth of field and
// motion blur, sharpness, the NTSC signal; then the colour correction (exposure, brightness, the tonemapper, gamma,
// vibrance and saturation, contrast, warmth, the colourblind correction, posterize); the edges and the scanlines last.
#include "common.hlsl"
#include "postfx.hlsl"

// Panini projection: the wide view's edges as on a cylinder (round things stay round, straight uprights straight).
// From a point of the Panini picture (x, y, the tangents of the view, d its strength) to where it is in the game's
// (rectilinear) one: x (d + cos a) = (d + 1) sin a gives the angle a across; the height scales with the cylinder.
float2 PaniniUV(float2 uv, float d) {
    float2 ext = float2(1.0 / Proj._11, 1.0 / Proj._22);  // the screen's edge in tangents
    float edge = atan(ext.x);
    float fit = (d + 1.0) / (d + cos(edge)) * sin(edge) / ext.x;  // (the screen's sides stay where they were)
    float2 v = (uv * 2.0 - 1.0) * ext * fit;
    float k = v.x / (d + 1.0);
    float a = atan(k) + asin(clamp(k * d / sqrt(1.0 + k * k), -1.0, 1.0));
    float s = (d + 1.0) / (d + cos(a));
    float2 r = float2(tan(a), v.y / (s * cos(a)));
    return r / ext * 0.5 + 0.5;
}

// The painted look (a Kuwahara filter): of the 4 squares around the pixel, the colour of the one that changes least.
float3 Painted(sampler2D s, float2 uv) {
    float2 px = SceneSize.zw * 1.5;
    float3 best = 0;
    float least = 1e9;
    [unroll] for (int k = 0; k < 4; k++) {
        float2 side = float2(k % 2 == 0 ? -1 : 1, k < 2 ? -1 : 1);
        float3 m = 0, m2 = 0;
        [unroll] for (int j = 0; j < 3; j++)
            [unroll] for (int i = 0; i < 3; i++) {
                float3 c = tex2Dlod(s, float4(uv + side * float2(i, j) * px, 0, 0)).rgb;
                m += c;
                m2 += c * c;
            }
        m /= 9.0;
        float3 v = m2 / 9.0 - m * m;
        float spread = v.r + v.g + v.b;
        if (spread < least) {
            least = spread;
            best = m;
        }
    }
    return best;
}

float3 ToYIQ(float3 c) {
    return float3(dot(c, float3(0.299, 0.587, 0.114)), dot(c, float3(0.596, -0.274, -0.322)),
                  dot(c, float3(0.211, -0.523, 0.312)));
}
float3 FromYIQ(float3 c) {
    return float3(dot(c, float3(1, 0.956, 0.621)), dot(c, float3(1, -0.272, -0.647)), dot(c, float3(1, -1.106, 1.703)));
}

// An NTSC signal: the colour smeared along the line, a faint crawl of dots where there is colour.
float3 Ntsc(sampler2D s, float2 uv, float3 c) {
    float y = ToYIQ(c).x;
    float2 iq = 0;
    [unroll] for (int i = -3; i <= 3; i++) iq += ToYIQ(tex2D(s, uv + float2(i * 1.5 * SceneSize.z, 0)).rgb).yz;
    iq /= 7.0;
    float2 pix = floor(uv * SceneSize.xy);
    y += sin(pix.x * 1.5708 + pix.y * 3.1416 + Time.z * 3.1416) * length(iq) * 0.12;
    return max(FromYIQ(float3(y, iq)), 0);
}

// The tonemapper: how the brightest parts roll off (each keeps the white where it was).
float3 Tonemap(float3 x) {
#if TONEMAP == Simple
    return x * 1.35 / (1.0 + Luma(x) * 0.35);
#elif TONEMAP == Reinhard
    return x * (1.0 + x * 0.25) / (1.0 + x) * 1.6;
#elif TONEMAP == ACES
    x *= 1.1;
    return saturate(x * (2.51 * x + 0.03) / (x * (2.43 * x + 0.59) + 0.14));
#else
    return x;
#endif
}

// The colourblind correction (daltonize): what the kind of sight loses (by the LMS cone responses) moved into the
// colours it still tells apart.
float3 Daltonize(float3 c, float k) {
    float3 lms = float3(dot(c, float3(17.8824, 43.5161, 4.11935)), dot(c, float3(3.45565, 27.1554, 3.86714)),
                        dot(c, float3(0.0299566, 0.184309, 1.46709)));
#if COLORBLIND == Protanopia
    lms.x = 2.02344 * lms.y - 2.52581 * lms.z;
#elif COLORBLIND == Deuteranopia
    lms.y = 0.494207 * lms.x + 1.24827 * lms.z;
#else
    lms.z = -0.395913 * lms.x + 0.801109 * lms.y;
#endif
    float3 seen = float3(dot(lms, float3(0.0809444479, -0.130504409, 0.116721066)),
                         dot(lms, float3(-0.0102485335, 0.0540193266, -0.113614708)),
                         dot(lms, float3(-0.000365296938, -0.00412161469, 0.693511405)));
    float3 lost = c - seen;
    return saturate(c + float3(0, lost.r * 0.7 + lost.g, lost.r * 0.7 + lost.b) * k);
}

float3 ToHSV(float3 c) {
    float4 p = c.g < c.b ? float4(c.bg, -1.0, 2.0 / 3.0) : float4(c.gb, 0.0, -1.0 / 3.0);
    float4 q = c.r < p.x ? float4(p.xyw, c.r) : float4(c.r, p.yzx);
    float d = q.x - min(q.w, q.y);
    return float3(abs(q.z + (q.w - q.y) / (6.0 * d + 1e-10)), d / (q.x + 1e-10), q.x);
}
float3 FromHSV(float3 c) {
    float3 p = abs(frac(c.xxx + float3(1.0, 2.0 / 3.0, 1.0 / 3.0)) * 6.0 - 3.0);
    return c.z * lerp(1.0, saturate(p - 1.0), c.y);
}

float4 main(float2 screen : TEXCOORD0) : COLOR {
    float2 uv = screen;
#if PROJECTION == Panini
    uv = PaniniUV(uv, PANINI);
#endif
    if (BARREL > 0) {
        float2 c = uv * 2.0 - 1.0;
        // (drawn a little larger: the corners reach the screen's, no black edges)
        c *= (1.0 + dot(c, c) * BARREL * 0.1) / (1.0 + 2.0 * BARREL * 0.1);
        uv = c * 0.5 + 0.5;
    }
    if (PIXELS > 1) {
        float2 n = SceneSize.xy / PIXELS;
        uv = (floor(uv * n) + 0.5) / n;
    }
#if FXAA
    float3 col = Fxaa(Taa, uv, SceneSize.zw);
#else
    float3 col = tex2D(Taa, uv).rgb;
#endif
#if PAINTED
    col = Painted(Taa, uv);
#endif
    col = WorldBlur(Taa, uv, col, DOF ? DOFSTRENGTH : 0.0, MBLUR ? MBLURSTRENGTH : 0.0);
    col = Sharpen(Taa, uv, col, SHARPEN);
    col = Aberration(Taa, uv, col, ABERRATION);
#if NTSC
    col = Ntsc(Taa, uv, col);
#endif
    // The exposure: the eye gets used to a dark or a bright view (eye.hlsl: the picture's average, followed slowly).
#if AUTOEXP
    col *= clamp(pow(0.3 / max(tex2D(Eye, float2(0.5, 0.5)).r, 0.01), 0.6), 0.7, 1.8);
#endif
    col = saturate(Tonemap(col * BRIGHTNESS));
    col = pow(col, 1.0 / GAMMA);
    // Livelier where dull, the saturation as the settings say (in the dark and in the light each its own), a gentle
    // S of contrast, warmer or colder.
    float l = Luma(col);
    float sat = max(col.r, max(col.g, col.b)) - min(col.r, min(col.g, col.b));
    float more = (1.0 + VIBRANCE * (1.0 - sat) * 0.9) * SATURATION * lerp(SATDARK, SATLIGHT, smoothstep(0.15, 0.75, l));
    col = saturate(lerp(l.xxx, col, more));
    col = lerp(col, col * col * (3.0 - 2.0 * col), CONTRAST * 0.6);
    col *= float3(1.0 + WARMTH * 0.06, 1.0 + WARMTH * 0.01, 1.0 - WARMTH * 0.08);
#if COLORBLIND
    col = Daltonize(saturate(col), CBSTRENGTH);
#endif
    if (POSTERIZE > 0) {
        float3 h = ToHSV(saturate(col));
        h = float3(floor(h.x * POSTERIZE * 2.0 + 0.5) / (POSTERIZE * 2.0), floor(h.yz * POSTERIZE + 0.5) / POSTERIZE);
        col = FromHSV(h);
    }
    col = Vignette(screen, saturate(col), VIGNETTE);
    // Scanlines: every third row at 1080 darker, softly.
    if (SCANLINES > 0) col *= 1.0 - SCANLINES * 0.4 * (0.5 + 0.5 * cos(uv.y * SceneSize.y * (1080.0 / SceneSize.y) * 2.0944));
    return float4(saturate(col), 1);
}
