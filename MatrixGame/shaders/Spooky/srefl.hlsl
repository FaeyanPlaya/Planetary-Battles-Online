// At full size: what the smooth surfaces of a resource pack's _s reflect with LabPBR materials (refl.hlsl's
// SurfaceRefl): rgb what the ray met on the screen, a how much of it (0: it went off the screen; final.hlsl puts the
// sky there and blurs it by the surface's roughness).
#define SURFACE_PASS
#include "refl.hlsl"
