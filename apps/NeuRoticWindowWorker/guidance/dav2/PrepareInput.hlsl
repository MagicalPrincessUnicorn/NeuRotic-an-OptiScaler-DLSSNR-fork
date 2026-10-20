// Original packet code. SPDX-License-Identifier: MIT
// Input SRV is display-like RGB [0,1], NOT an sRGB-decoding SRV and NOT raw HDR.
// Output is a shared committed D3D12 byte-address BUFFER imported by CUDA.
cbuffer Prepare : register(b0) {
 uint SourceWidth,SourceHeight,TensorWidth,TensorHeight;
 uint ContentWidth,ContentHeight,PadLeft,PadTop;
 uint SourceX,SourceY,Reserved0,Reserved1;
};
Texture2D<float4> Source : register(t0);
RWByteAddressBuffer Tensor : register(u0);
float cubic(float t) {
 t=abs(t); const float a=-0.75;
 if(t<=1) return ((a+2)*t-(a+3))*t*t+1;
 if(t<2) return ((a*t-5*a)*t+8*a)*t-4*a;
 return 0;
}
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=TensorWidth||id.y>=TensorHeight)return;
 const uint p=id.y*TensorWidth+id.x,plane=TensorWidth*TensorHeight;
 float3 normalized=0; // Mean-color padding -> normalized zero.
 if(id.x>=PadLeft && id.y>=PadTop && id.x<PadLeft+ContentWidth && id.y<PadTop+ContentHeight) {
    float2 pos=(float2(id.xy-uint2(PadLeft,PadTop))+.5)*float2(SourceWidth,SourceHeight)/float2(ContentWidth,ContentHeight)-.5;
    int2 base=int2(floor(pos));float3 rgb=0;
    [unroll] for(int j=-1;j<=2;++j) [unroll] for(int i=-1;i<=2;++i) {
        int2 q=clamp(base+int2(i,j),int2(0,0),int2(SourceWidth-1,SourceHeight-1));
        rgb+=Source.Load(int3(q+int2(SourceX,SourceY),0)).rgb*cubic(pos.x-(base.x+i))*cubic(pos.y-(base.y+j));
    }
    // Do not clamp bicubic overshoot; upstream interpolation can overshoot as well.
    normalized=(rgb-float3(.485,.456,.406))/float3(.229,.224,.225);
 }
 Tensor.Store(p*4,asuint(normalized.r));
 Tensor.Store((plane+p)*4,asuint(normalized.g));
 Tensor.Store((2*plane+p)*4,asuint(normalized.b));
}
