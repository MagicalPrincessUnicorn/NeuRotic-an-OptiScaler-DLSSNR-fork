// Original packet code. SPDX-License-Identifier: MIT
// Inverse pixel-center letterbox mapping. NumericValidity is NOT model confidence.
cbuffer Materialize : register(b0) {
 uint TensorWidth,TensorHeight,ContentWidth,ContentHeight;
 uint PadLeft,PadTop,OutputWidth,OutputHeight;
};
ByteAddressBuffer RawTensor : register(t0);
RWTexture2D<float> RawDepth : register(u0);
RWTexture2D<float> NumericValidity : register(u1);
float readRaw(int2 p){return asfloat(RawTensor.Load((p.y*TensorWidth+p.x)*4));}
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
 if(id.x>=OutputWidth||id.y>=OutputHeight)return;
 float2 low=float2(PadLeft,PadTop),high=low+float2(ContentWidth,ContentHeight)-1;
 float2 p=clamp((float2(id.xy)+.5)*float2(ContentWidth,ContentHeight)/float2(OutputWidth,OutputHeight)-.5+low,low,high);
 int2 p0=int2(floor(p)),p1=min(p0+1,int2(high));float2 f=frac(p);
 float values[4]={readRaw(p0),readRaw(int2(p1.x,p0.y)),readRaw(int2(p0.x,p1.y)),readRaw(p1)};
 float weights[4]={(1-f.x)*(1-f.y),f.x*(1-f.y),(1-f.x)*f.y,f.x*f.y};
 bool valid=true;float value=0;
 [unroll]for(uint i=0;i<4;i++) {
    bool finite=isfinite(values[i])&&values[i]>=0;
    if(!finite&&weights[i]>1e-7)valid=false;
    value+=(finite?values[i]:0)*weights[i];
 }
 RawDepth[id.xy]=valid?value:0;
 NumericValidity[id.xy]=valid?1:0;
}
