// Lens flares from the battle's bright lights, at a quarter of the screen's size (soft, and each pixel loops over
// all the lights: the full size cost 10 ms a frame); the post pass adds them.
#include "common.hlsl"
#include "postfx.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    return float4(Flares(uv, FLARE), 1);
}
