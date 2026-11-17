// Spooky: how far a place moved on the screen since the previous frame, by the world pass (world.hlsl keeps it in
// WorldM's green and blue): a robot's, a turret's, a flyer's own move too, not the camera's alone. postfx.hlsl's
// PrevAt asks it first (the temporal filter, the motion blur). Only while the world pass is drawn (GEOMETRY); where
// it drew nothing (the sky, WorldM's red 0): the camera's move.
#ifndef PBO_MOTION
#define PBO_MOTION
#if GEOMETRY
#define PBO_OBJECT_MOTION
bool ObjectMotion(float2 uv, out float2 motion) {
    float4 k = tex2Dlod(WorldM, float4(uv, 0, 0));
    motion = k.gb;
    return k.r > 0.01;
}
#endif
#endif
