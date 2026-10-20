Texture2D<float4> SourceTexture : register(t0);
RWTexture2D<float4> DestinationTexture : register(u0);
RWByteAddressBuffer PackedDestination : register(u0);

[numthreads(16,16,1)]
void Decode(uint3 id : SV_DispatchThreadID)
{
    uint w,h; DestinationTexture.GetDimensions(w,h);
    if (id.x >= w || id.y >= h) return;
    // A BGRA UNORM SRV loads logical RGBA; keep G22 code values and alpha.
    DestinationTexture[id.xy] = SourceTexture.Load(int3(id.xy,0));
}

[numthreads(16,16,1)]
void Encode(uint3 id : SV_DispatchThreadID)
{
    uint w,h; SourceTexture.GetDimensions(w,h);
    if (id.x >= w || id.y >= h) return;
    uint4 v = (uint4)round(saturate(SourceTexture.Load(int3(id.xy,0))) * 255.0);
    // D3D12 BGRA has no typed UAV store. Write actual BGRA bytes to a raw
    // buffer using the standard 256-byte texture-copy row alignment.
    uint rowPitch = ((w * 4 + 255) / 256) * 256;
    PackedDestination.Store(id.y * rowPitch + id.x * 4,
                            v.b | (v.g << 8) | (v.r << 16) | (v.a << 24));
}
