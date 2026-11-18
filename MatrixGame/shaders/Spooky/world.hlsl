// Spooky's world pass: the engine draws the land, the map's things, robots and buildings once more by this shader,
// into WorldN, WorldM and WorldG. The surfaces' own normals (a model's, smooth over its rounded parts; the depth alone
// gives each triangle's; a resource pack's _n bends them, MATERIALS LabPBR) and what each thing is: for the lights in half.hlsl; how far
// each place moved on the screen since the previous frame (WorldM's green and blue): motion.hlsl, for the temporal
// filter and the motion blur; the light a resource pack's maps make a surface give (_s's emission, _e): glow.hlsl
// and final.hlsl; and how its _n changes the moon's light there: lit.hlsl.
void main(World w, out float4 normal, out float4 kind, out float4 glow) {
#if MATERIALS == 2
    // LabPBR: the pack's maps (only then does the engine read them). _n by NORMALMAP's strength; in glow's alpha how
    // it changes the moon's light here, halved (0: no _n).
    float3 n = normalize(lerp(w.normal, w.mapped, NORMALMAP));
    float flat = dot(w.normal, SunDir.xyz) + 0.25, bent = dot(n, SunDir.xyz) + 0.25;
    float relief = w.maps.x > 0.5 && NORMALMAP > 0 ? clamp(bent / max(flat, 0.05), 0.04, 2.0) : 0.0;
    glow = float4(w.glow, relief * 0.5);
#else
    float3 n = w.normal;
    glow = 0;
#endif
    normal = float4(n * 0.5 + 0.5, 1);
    kind = float4(w.material / 16.0, w.motion, 1);
}
