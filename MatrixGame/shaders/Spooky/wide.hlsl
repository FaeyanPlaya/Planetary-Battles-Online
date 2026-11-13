// Spooky, at 1/16: the glow spread wide; 4 reads between GlowB's pixels take all 4x4 of them (no holes).
float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 px = SceneSize.zw * 4.0;  // one GlowB pixel
    float3 c = tex2D(GlowB, uv + float2(-px.x, -px.y)).rgb + tex2D(GlowB, uv + float2(px.x, -px.y)).rgb +
               tex2D(GlowB, uv + float2(-px.x, px.y)).rgb + tex2D(GlowB, uv + px).rgb;
    return float4(c * 0.25, 1);
}
