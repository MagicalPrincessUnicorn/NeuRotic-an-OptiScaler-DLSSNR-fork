#ifdef VK_MODE
[[vk::binding(0,0)]]
#endif
Texture2D<float4> SourceTexture : register(t0);
#ifdef VK_MODE
[[vk::binding(1,0)]]
#endif
Texture2D<float4> OriginalPq : register(t1); // existing retained source, inverse only
#ifdef VK_MODE
[[vk::binding(2,0)]] [[vk::image_format("rgba16f")]]
#endif
RWTexture2D<float4> DestinationTexture : register(u0);
float3 PqDecode(float3 v)
{
    float3 p = pow(saturate(v), 32.0 / 2523.0);
    return 125.0 * pow(max(p - 3424.0 / 4096.0, 0.0) /
        (2413.0 / 128.0 - 2392.0 / 128.0 * p), 16384.0 / 2610.0);
}
float3 PqEncode(float3 v)
{
    float3 p = pow(saturate(v / 125.0), 2610.0 / 16384.0);
    return pow((3424.0 / 4096.0 + 2413.0 / 128.0 * p) /
        (1.0 + 2392.0 / 128.0 * p), 2523.0 / 32.0);
}
[numthreads(16,16,1)]
void Decode(uint3 id : SV_DispatchThreadID)
{
    uint w,h; DestinationTexture.GetDimensions(w,h);
    if(id.x>=w || id.y>=h) return;
    float4 v = SourceTexture.Load(int3(id.xy,0));
    const float3x3 m = {1.660491002108434,-0.587641138788550,-0.072849863319884,
        -0.124550474521591,1.132899897125960,-0.008349422604369,
        -0.018150763354905,-0.100578898008007,1.118729661362913};
    DestinationTexture[id.xy] = float4(mul(m,PqDecode(v.rgb)),v.a);
}
[numthreads(16,16,1)]
void Encode(uint3 id : SV_DispatchThreadID)
{
    uint w,h; DestinationTexture.GetDimensions(w,h);
    if(id.x>=w || id.y>=h) return;
    float4 v = SourceTexture.Load(int3(id.xy,0));
    const float3x3 m = {0.627403895934699,0.329283038377884,0.043313065687417,
        0.069097289358232,0.919540395075459,0.011362315566309,
        0.016391438875150,0.088013307877226,0.895595253247624};
    float4 source = OriginalPq.Load(int3(id.xy,0));
    const float3x3 to709 = {1.660491002108434,-0.587641138788550,-0.072849863319884,
        -0.124550474521591,1.132899897125960,-0.008349422604369,
        -0.018150763354905,-0.100578898008007,1.118729661362913};
    float3 original2020 = PqDecode(source.rgb);
    // Match the FP16 carrier quantization. Apply only its edit to the original
    // linear2020 value; do not turn gamut-transform/half rounding into an edit.
    float3 original709 = f16tof32(f32tof16(mul(to709,original2020)));
    float3 delta = v.rgb - original709;
    if (all(delta == 0.0))
        DestinationTexture[id.xy] = float4(source.rgb,v.a);
    else
        DestinationTexture[id.xy] = float4(PqEncode(original2020 + mul(m,delta)),v.a);
}
