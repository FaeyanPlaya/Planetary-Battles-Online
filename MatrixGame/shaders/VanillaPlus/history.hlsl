// The temporal filter's picture kept for the next frame (Taa is drawn again then).
float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(Taa, uv); }
