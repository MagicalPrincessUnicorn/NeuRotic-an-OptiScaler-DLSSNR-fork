// Generated verbatim from tools/nr/lab/rendering/scene/scene.hlsl.
// SHA256 c8cb552d6c8f31977f7291eaf29d5172754537d72135cfae55dd2734edfb12b4
#pragma once
namespace DlssNr::ControlledScene {
inline constexpr char Shader[]=R"NRSCENE(// Bounded analytic geometry producer. Pixel centers lie within integer-edged
// rectangles; no rasterization-edge tolerance is required by this profile.
cbuffer Frame : register(b0) { uint width; uint height; uint frame; uint inverted; }
RWTexture2D<float4> Color : register(u0);
RWTexture2D<float> Depth : register(u1);
RWTexture2D<float2> Motion : register(u2);
RWTexture2D<uint> Disocclusion : register(u3);

[numthreads(8,8,1)]
void main(uint3 tid : SV_DispatchThreadID) {
    if (tid.x >= width || tid.y >= height) return;
    int2 p = int2(tid.xy);
    int previous = max(0, int(frame)-1);
    int2 origin = int2(24,24) + int(frame)*int2(4,2);
    int2 oldOrigin = int2(24,24) + previous*int2(4,2);
    int2 local = p-origin;
    int2 oldLocal = p-oldOrigin;
    bool front = all(local >= 0) && all(local < int2(32,24));
    bool oldFront = all(oldLocal >= 0) && all(oldLocal < int2(32,24));
    uint tile = (tid.x >> 3) ^ (tid.y >> 3);
    float4 color = float4((tile & 1) ? .25 : .125, .125, .25, 1);
    float z = .75;
    float2 mv = float2(0,0);
    if (front) {
        uint material = (uint(local.x) >> 2) ^ (uint(local.y) >> 2);
        color = float4(.75, (material & 1) ? .5 : .25, .125, 1);
        z = .25;
        mv = float2(oldOrigin-origin);
    }
    Color[tid.xy] = color;
    Depth[tid.xy] = inverted ? 1-z : z;
    Motion[tid.xy] = mv;
    Disocclusion[tid.xy] = frame > 0 && oldFront && !front ? 1 : 0;
}
)NRSCENE";
}
