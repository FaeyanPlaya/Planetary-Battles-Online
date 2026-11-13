// Spooky, at a quarter: what glows (the effects' bright part: fire, flashes, shots), every pixel of its 4x4 read.
float3 Hot(float2 at) {
    float3 fx = max(tex2D(SceneTex, at).rgb - tex2D(SolidTex, at).rgb, 0);
    return fx * saturate(Luma(fx) * 4.0 - 0.2);  // not smoke
}

float4 main(float2 uv : TEXCOORD0) : COLOR {
    float2 px = SceneSize.zw;
    float3 c = Hot(uv + float2(-px.x, -px.y)) + Hot(uv + float2(px.x, -px.y)) + Hot(uv + float2(-px.x, px.y)) +
               Hot(uv + px);
    return float4(c * 0.25, 1);
}
