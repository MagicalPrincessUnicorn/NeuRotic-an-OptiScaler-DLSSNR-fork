// Original NeuRotic conversion/validation shader. Producer grids describe
// samples, not vector units. FFX SINT is pixels; NVOF S10.5 is pixels / 32.
Texture2D<int2> Raw : register(t0);
Texture2D<uint> Scene : register(t1);
Texture2D<uint> Cost : register(t2);
Texture2D<int2> Reverse : register(t3);
Texture2D<float4> Current : register(t4);
Texture2D<float4> Previous : register(t5);
RWTexture2D<float2> Motion : register(u0);
RWTexture2D<float> Distrust : register(u1);
RWTexture2D<float> Luma : register(u2);
cbuffer Parameters : register(b0)
{
    uint Width, Height, Grid, Nvidia;
    uint Reset, Warmup, CostThreshold, Spare;
    float AppearanceThreshold, ConsistencyThreshold;
};
float luminance(float3 c) { return dot(c, float3(0.2126, 0.7152, 0.0722)); }
[numthreads(8,8,1)]
void PrepareLuma(uint3 id : SV_DispatchThreadID)
{
    if (id.x < Width && id.y < Height)
        Luma[id.xy] = saturate(luminance(Current.Load(int3(id.xy,0)).rgb));
}
[numthreads(8,8,1)]
void Convert(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= Width || id.y >= Height) return;
    uint2 gridPosition = id.xy / Grid;
    float divisor = Nvidia != 0 ? 32.0 : 1.0;
    float2 v = float2(Raw.Load(int3(gridPosition,0))) / divisor;
    bool cut = Nvidia == 0 && (Scene.Load(int3(1,0,0)) & 15u) != 0;
    bool bad = Reset != 0 || Warmup != 0 || cut || !all(isfinite(v));
    float2 oldPosition = float2(id.xy) + v;
    bool outside = any(oldPosition < 0) || oldPosition.x >= Width || oldPosition.y >= Height;
    bad = bad || outside;
    if (!outside && Reset == 0 && Warmup == 0)
    {
        int2 oldPixel = int2(round(oldPosition));
        oldPixel = clamp(oldPixel, int2(0,0), int2(Width-1,Height-1));
        float now = luminance(Current.Load(int3(id.xy,0)).rgb);
        float old = luminance(Previous.Load(int3(oldPixel,0)).rgb);
        bad = bad || !isfinite(now) || !isfinite(old) || abs(now-old) > AppearanceThreshold;
        if (Nvidia != 0)
        {
            uint2 reversePosition = uint2(oldPixel) / Grid;
            float2 reverse = float2(Reverse.Load(int3(reversePosition,0))) / 32.0;
            bad = bad || length(v + reverse) > ConsistencyThreshold ||
                Cost.Load(int3(gridPosition,0)) > CostThreshold;
        }
    }
    // Distrust is separate. A residual never changes measured flow into zero.
    Motion[id.xy] = all(isfinite(v)) ? v : float2(0,0);
    Distrust[id.xy] = bad ? 1.0 : 0.0;
}
