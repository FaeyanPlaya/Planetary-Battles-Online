// VHS: the picture held at the tape's rate (FIELDS a second): taken anew when a half-frame begins, kept otherwise.
#include "common.hlsl"

float4 main(float2 uv : TEXCOORD0) : COLOR {
    bool begun = floor(Time.x * FIELDS) != floor((Time.x - Time.w) * FIELDS);
    return begun || State.z < 2 ? tex2D(Signal, uv) : tex2D(TapeOld, uv);
}
