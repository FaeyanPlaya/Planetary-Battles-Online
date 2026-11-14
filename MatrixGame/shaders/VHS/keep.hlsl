// VHS: the held half-frame kept for this frame (Tape is drawn again next).
float4 main(float2 uv : TEXCOORD0) : COLOR { return tex2D(Tape, uv); }
