// Preserve NGX depth and motion numeric values at integer texel coordinates.
[[vk::binding(0, 0)]] Texture2D<float4> Source;
[[vk::binding(1, 0)]] RWTexture2D<float> DepthDestination;
[[vk::binding(1, 0)]] RWTexture2D<float4> MotionDestination;
[[vk::binding(2, 0)]] cbuffer Rectangle { uint2 Origin; uint2 Extent; };
[numthreads(8, 8, 1)]
void CopyDepth(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Extent)) return;
    DepthDestination[id.xy] = Source.Load(int3(id.xy + Origin, 0)).x;
}
[numthreads(8, 8, 1)]
void CopyMotion(uint3 id : SV_DispatchThreadID)
{
    if (any(id.xy >= Extent)) return;
    MotionDestination[id.xy] = Source.Load(int3(id.xy + Origin, 0));
}
