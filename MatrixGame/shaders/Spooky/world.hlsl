// Spooky's world pass: the engine draws the land, the map's things, robots and buildings once more by this shader,
// into WorldN, WorldM, WorldG and WorldS. The surfaces' own normals (a model's, smooth over its rounded parts; the
// depth alone gives each triangle's; a resource pack's _n bends them, MATERIALS LabPBR) and what each thing is: for
// the lights in half.hlsl; how far each place moved on the screen since the previous frame (WorldM's green and
// blue): motion.hlsl, for the temporal filter and the motion blur; the light a resource pack's maps make a surface
// give (_s's emission, _e): glow.hlsl and final.hlsl; how its _n changes the moon's light there, with the relief's
// own shadows and its occlusion: lit.hlsl; what its _s says of the surface (WorldS: smoothness, F0 or a metal): the
// shine in lit.hlsl, the reflections in srefl.hlsl; and the picture's shift by the parallax (WorldS's blue and alpha,
// WorldM's alpha): lit.hlsl.
void main(World w, out float4 normal, out float4 kind, out float4 glow, out float4 shine) {
#if MATERIALS == 2
    // LabPBR: the pack's maps (only then does the engine read them). Where the view meets the relief of _n's height
    // (the parallax, POM 2: robots, buildings, turrets, the map's things; the land mixes several textures and stays
    // where it is), the maps are read there.
    float at = 0;
    float2 uv = w.uv;
    bool still = w.material == MAT_LAND || w.material == MAT_WATER;
    if (POM >= 2 && !still) {
        uv = ParallaxUV(w, POMDEPTH * 0.025, POMSTEPS, at);
        // At most 4 of the screen's pixels: a shift over fine detail seen small (a wall at a slant, far off)
        // jumps between unrelated texels, a noise of specks.
        float2 off = uv - w.uv;
        float most = 4.0 * max(length(w.grad.xy), length(w.grad.zw));
        uv = w.uv + off * min(1.0, most / max(length(off), 1e-6));
    }
    float4 nm = MapNAt(w, uv), sm = MapSAt(w, uv);
    // _n by NORMALMAP's strength; in glow's alpha how it changes the moon's light here, halved (0: no _n), with the
    // relief's own shadows towards the moon (POM 1 and 2) and its occlusion.
    float3 n = normalize(lerp(w.normal, MappedBy(w, nm), NORMALMAP));
    float flat = dot(w.normal, SunDir.xyz) + 0.25, bent = dot(n, SunDir.xyz) + 0.25;
    float relief = NORMALMAP > 0 ? clamp(bent / max(flat, 0.05), 0.04, 2.0) : 1.0;
    float self = 1.0;
    if (POM >= 1 && w.material != MAT_WATER)
        self = ParallaxShadow(w, uv, at, SunDir.xyz, POMDEPTH * 0.025, POMSTEPS / 2);
    relief *= lerp(1.0, self, 0.85);
    // _n's occlusion (its blue) in the same light: the hollows darker (never quite 0: 0 is "no _n").
    relief *= 1.0 - (1.0 - nm.b) * OCCLUSION;
    glow = float4(w.glow, w.maps.x > 0.5 ? max(relief, 0.02) * 0.5 : 0.0);
    // The picture's shift: how much brighter each colour of the texture is where the view meets its relief than here
    // (a quarter of it; 0: none), for lit.hlsl to lay over the game's colour (its light, the sides' colours and the
    // fog stay): red and green in WorldS, blue in WorldM's alpha.
    float3 shifted = 0;
    if (POM >= 2 && !still && w.maps.x > 0.5)
        shifted = clamp((PictureAt(w, uv) + 0.02) / (PictureAt(w, w.uv) + 0.02), 0.5, 2.0) * 0.25;
    // _s: smoothness, F0 (230 and up: a metal) where it has one.
    bool smooth = w.maps.y > 0.5 && w.material != MAT_WATER;
    shine = float4(smooth ? sm.rg : float2(0, 0), shifted.rg);
#else
    float3 n = w.normal;
    float3 shifted = 0;
    glow = 0;
    shine = 0;
#endif
    normal = float4(n * 0.5 + 0.5, 1);
    kind = float4(w.material / 16.0, w.motion, shifted.b);
}
