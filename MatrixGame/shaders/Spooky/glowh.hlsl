// Spooky: the glow blurred across (9 taps by 5 reads).
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 step = float2(ScreenSize.z, 0);
    float3 c = tex2D(Glow, uv).rgb * 0.2270270;
    c += (tex2D(Glow, uv + step * 1.3846154).rgb + tex2D(Glow, uv - step * 1.3846154).rgb) * 0.3162162;
    c += (tex2D(Glow, uv + step * 3.2307692).rgb + tex2D(Glow, uv - step * 3.2307692).rgb) * 0.0702703;
    return float4(c, 1);
}
