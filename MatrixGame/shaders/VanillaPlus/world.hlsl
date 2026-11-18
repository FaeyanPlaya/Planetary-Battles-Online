// Vanilla +'s world pass: the engine draws the land, the map's things, robots, buildings and turrets once more by this
// shader, into WorldV: how far each place moved on the screen since the previous frame (green, blue; red 1: drawn
// here). motion.hlsl reads it for the temporal filter and the motion blur. Into WorldG: the light a resource pack's
// maps make a surface give (_s's emission, _e), for glow.hlsl and final.hlsl, and how its _n changes the sun's
// light there (alpha): final.hlsl.
void main(World w, out float4 moved, out float4 glow) {
    moved = float4(1, w.motion, 1);
#if PBR == 2
    // LabPBR 1.3: the pack's maps (only then does the engine read them). _n by NORMALMAP's strength; in glow's alpha
    // how it changes the sun's light here, halved (0: no _n).
    float3 n = normalize(lerp(w.normal, w.mapped, NORMALMAP));
    float flat = dot(w.normal, SunDir.xyz) + 0.25, bent = dot(n, SunDir.xyz) + 0.25;
    float relief = w.maps.x > 0.5 && NORMALMAP > 0 ? clamp(bent / max(flat, 0.05), 0.04, 2.0) : 0.0;
    glow = float4(w.glow, relief * 0.5);
#else
    glow = 0;
#endif
}
