// The camera's effects, the same in Spooky and Vanilla + (a copy in each): what the post pass and the temporal
// filter call. Each function takes its strength: the pack's settings are the caller's.
#ifndef PBO_POSTFX
#define PBO_POSTFX

// Where this pixel's place was on the screen a frame ago (the camera's move only: the depth's place by the previous
// frame's view).
float2 PrevUV(float2 uv) {
    float d = SceneDepth(uv);
    float3 p = WorldPos(uv, d < 1 ? d : 0.9999);
    float4 c = mul(float4(p, 1), PrevViewProj);
    return float2(c.x / c.w * 0.5 + 0.5, 0.5 - c.y / c.w * 0.5);
}

// Fast approximate antialiasing: along an edge of the brightness, two reads its way; the wider pair when it stays
// within the edge's range.
float3 Fxaa(sampler2D s, float2 uv, float2 px) {
    float3 c = tex2D(s, uv).rgb;
    float nw = Luma(tex2D(s, uv + float2(-px.x, -px.y)).rgb), ne = Luma(tex2D(s, uv + float2(px.x, -px.y)).rgb);
    float sw = Luma(tex2D(s, uv + float2(-px.x, px.y)).rgb), se = Luma(tex2D(s, uv + px).rgb);
    float m = Luma(c);
    float lo = min(m, min(min(nw, ne), min(sw, se))), hi = max(m, max(max(nw, ne), max(sw, se)));
    if (hi - lo < max(0.04, hi * 0.12)) return c;
    float2 dir = float2(-((nw + ne) - (sw + se)), (nw + sw) - (ne + se));
    float reduce = max((nw + ne + sw + se) * 0.03125, 1.0 / 128.0);
    dir = clamp(dir / (min(abs(dir.x), abs(dir.y)) + reduce), -8.0, 8.0) * px;
    float3 a = (tex2D(s, uv - dir / 6.0).rgb + tex2D(s, uv + dir / 6.0).rgb) * 0.5;
    float3 b = a * 0.5 + (tex2D(s, uv - dir * 0.5).rgb + tex2D(s, uv + dir * 0.5).rgb) * 0.25;
    float lb = Luma(b);
    return lb < lo || lb > hi ? a : b;
}

// The temporal filter: the previous frame's picture where this place was, held within the colours around it now
// (what moved or changed does not trail), mixed in.
float3 Temporal(sampler2D now, sampler2D before, float2 uv, float2 px, float keep) {
    float3 c = tex2D(now, uv).rgb, lo = c, hi = c;
    [unroll] for (int y = -1; y <= 1; y++)
        [unroll] for (int x = -1; x <= 1; x++) {
            float3 n = tex2D(now, uv + float2(x, y) * px).rgb;
            lo = min(lo, n);
            hi = max(hi, n);
        }
    float2 at = PrevUV(uv);
    if (State.z < 2 || any(at != saturate(at))) return c;
    float3 h = clamp(tex2D(before, at).rgb, lo, hi);
    return lerp(c, h, keep);
}

// The picture at a place with its colour fringes (Aberration's: red read `o` further out, blue `o` further in).
float3 Fringed(sampler2D s, float2 at, float2 o) {
    float3 c = tex2Dlod(s, float4(at, 0, 0)).rgb;
    return float3(tex2Dlod(s, float4(at + o, 0, 0)).r, c.g, tex2Dlod(s, float4(at - o, 0, 0)).b);
}

// Colour fringes' offset at a place of the screen (Aberration's).
float2 FringeAt(float2 uv, float k) { return k > 0 ? (uv - 0.5) * k * 0.006 : float2(0, 0); }

// Depth of field and motion blur by reads around the pixel: a disc as wide as its blur (the view's sharp distance:
// the screen's middle) and a line along the camera's move. `fringe`: the colour fringes' offset there (FringeAt), so
// the blur smears the fringed picture: red and blue read sharp beside a smeared green parted into ghosts on a fast
// turn.
float3 WorldBlur(sampler2D s, float2 uv, float3 c, float dof, float motion, float2 fringe) {
    float d = SceneDepth(uv);
    float z = ViewDepth(d < 1 ? d : 0.9999), focus = ViewDepth(min(SceneDepth(float2(0.5, 0.5)), 0.9999));
    float radius = dof > 0 ? min(abs(1.0 - focus / z) * dof * 14.0, 12.0) : 0.0;  // in pixels at 1080 rows
    float2 move = motion > 0 ? (uv - PrevUV(uv)) * motion : float2(0, 0);
    move = clamp(move, -0.04, 0.04);
    if (radius < 0.5 && dot(move, move) * SceneSize.x * SceneSize.x < 1.0) return c;
    float2 px = SceneSize.zw * (SceneSize.y / 1080.0) * radius;
    float3 sum = c;
    bool fringed = any(fringe != 0);
    [unroll] for (int i = 0; i < 12; i++) {
        float f = (i + 0.5) / 12.0;
        float a = i * 2.3999632;
        float2 at = uv + float2(cos(a), sin(a)) * sqrt(f) * px + move * (f - 0.5);
        sum += fringed ? Fringed(s, at, fringe) : tex2Dlod(s, float4(at, 0, 0)).rgb;
    }
    return sum / 13.0;
}

// Colour fringes towards the screen's edges.
float3 Aberration(sampler2D s, float2 uv, float3 c, float k) {
    if (k <= 0) return c;
    float2 o = FringeAt(uv, k);
    return float3(tex2D(s, uv + o).r, c.g, tex2D(s, uv - o).b);
}

// Sharper: the brightness's own detail.
float3 Sharpen(sampler2D s, float2 uv, float3 c, float k) {
    if (k <= 0) return c;
    float2 px = SceneSize.zw;
    float around = Luma(tex2D(s, uv + float2(px.x, 0)).rgb + tex2D(s, uv - float2(px.x, 0)).rgb +
                        tex2D(s, uv + float2(0, px.y)).rgb + tex2D(s, uv - float2(0, px.y)).rgb) * 0.25;
    return c + clamp(Luma(c) - around, -0.1, 0.1) * k * 1.5;
}

// Lens flares from the battle's bright lights in view (fire, blasts, shots): a glare round each, ghosts along the
// line through the screen's middle, a thin streak across. Not from a light something stands before.
#ifndef FLARE_FROM
#define FLARE_FROM 0  // (a pack without the choice: from every light)
#endif
float3 Flares(float2 uv, float k) {
    if (k <= 0) return 0;
    float aspect = SceneSize.x / SceneSize.y;
    float3 sum = 0;
    [loop] for (int i = 0; i < MAX_LIGHTS; i++) {
        if (i >= LightCount.x) break;
        float3 s = ScreenPos(LightPos[i].xyz);
        if (s.z <= 0 || s.z >= 1 || any(s.xy < -0.1) || any(s.xy > 1.1)) continue;
        float2 at = saturate(s.xy);
        if (ViewDepth(SceneDepth(at)) < ViewDepth(s.z) - LightPos[i].w * 0.3) continue;  // (behind something)
        // (FLARES: from every light, or explosions only, or the mighty ones only: LightInfo.z)
        if (LightInfo[i].z < FLARE_FROM) continue;
        float3 col = LightColor[i].rgb * saturate(LightPos[i].w / 60.0);
        if (Luma(col) < 0.08) continue;
        float2 v = (uv - s.xy) * float2(aspect, 1);
        float glare = 1.0 / (1.0 + dot(v, v) * 900.0);
        float streak = exp(-abs(v.y) * 150.0) * exp(-abs(v.x) * 6.0);
        float ghosts = 0;
        [unroll] for (int g = 0; g < 3; g++) {
            float t = g == 0 ? -0.4 : g == 1 ? -0.9 : 0.5;
            float2 c = 0.5 + (s.xy - 0.5) * t;
            float r = 0.02 + g * 0.015;
            ghosts += smoothstep(r, r * 0.6, length((uv - c) * float2(aspect, 1))) * (0.25 - g * 0.05);
        }
        sum += col * (glare * 0.6 + streak * 0.35 + ghosts * 0.35);
    }
    return sum * k * 0.5;
}

// The edges darker.
float3 Vignette(float2 uv, float3 c, float k) {
    float2 v = uv - 0.5;
    return c * (1.0 - k * 0.6 * saturate(dot(v, v) * 2.2));
}

#endif
