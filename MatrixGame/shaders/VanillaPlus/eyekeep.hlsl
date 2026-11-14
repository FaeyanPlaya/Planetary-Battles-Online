// Vanilla+: the eye's brightness kept for the next frame (Eye is drawn again then).
float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(Eye, float2(0.5, 0.5)); }
