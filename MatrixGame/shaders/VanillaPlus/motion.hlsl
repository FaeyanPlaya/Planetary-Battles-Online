// Vanilla +: how far a place moved on the screen since the previous frame, by the world pass (world.hlsl keeps it in
// WorldV's green and blue): a robot's, a turret's, a flyer's own move too, not the camera's alone. postfx.hlsl's
// PrevAt asks it first (the temporal filter, the motion blur). Only while the world pass is drawn (MOVING); where it
// drew nothing (the sky, WorldV's red 0): the camera's move.
#ifndef PBO_MOTION
#define PBO_MOTION
#if MOVING
#define PBO_OBJECT_MOTION
bool ObjectMotion(float2 uv, out float2 motion) {
    float4 k = tex2Dlod(WorldV, float4(uv, 0, 0));
    motion = k.gb;
    return k.r > 0.5;
}
#endif
#endif
