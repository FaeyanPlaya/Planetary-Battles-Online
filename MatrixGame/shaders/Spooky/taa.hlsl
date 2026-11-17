// The temporal filter (TAA): the frame mixed with the one before where each place was, held within today's colours
// around it. Off: the frame as it is.
#include "common.hlsl"
#include "motion.hlsl"
#include "postfx.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
#if TAA
    return float4(Temporal(Color, TaaHist, uv, SceneSize.zw, 0.85), 1);
#else
    return tex2D(Color, uv);
#endif
}
