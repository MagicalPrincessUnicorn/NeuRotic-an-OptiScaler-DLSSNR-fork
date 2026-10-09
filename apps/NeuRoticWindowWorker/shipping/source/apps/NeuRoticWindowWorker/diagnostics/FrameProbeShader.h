#pragma once
namespace nrw {
// One invocation, no groupshared data/atomics or raw-sample array. The local
// record is 128 bytes; loops retain only the current point and accumulators.
inline constexpr char FrameProbeShader[]=R"HLSL(
cbuffer Token : register(b0) {uint widthTag,height,requestLo,requestHi,sessionLo,sessionHi,sequenceLo,sequenceHi;}
Texture2D<float4> A : register(t0);Texture2D<float4> B : register(t1);
Texture2D<float4> C : register(t2);Texture2D<float4> D : register(t3);
struct Record {uint w[32];};RWStructuredBuffer<Record> results : register(u0);
uint axis(uint extent,uint i){return min(extent-1,((2*i+1)*extent)/32);}
[numthreads(1,1,1)]
void main(uint3 unused:SV_DispatchThreadID){
 uint width=widthTag&65535,tag=widthTag>>16;
 for(uint pair=0;pair<(tag==0?2:1);++pair){
  Record r;for(uint k=0;k<32;++k)r.w[k]=0;
  r.w[0]=requestLo;r.w[1]=requestHi;r.w[2]=sessionLo;r.w[3]=sessionHi;r.w[4]=sequenceLo;r.w[5]=sequenceHi;r.w[6]=width;r.w[7]=height;
  r.w[24]=tag==0?pair:tag+1;
  float maxRgb=0,sumRgb=0,maxAlpha=0,sumAlpha=0;
  for(uint j=0;j<16;++j){uint y=axis(height,j);if(j&&y==axis(height,j-1))continue;
   for(uint i=0;i<16;++i){uint x=axis(width,i);if(i&&x==axis(width,i-1))continue;
    float4 a,b;[branch]if(tag==0&&pair==1){a=C.Load(int3(x,y,0));b=D.Load(int3(x,y,0));}
    else {a=A.Load(int3(tag==2?uint2(i,j):uint2(x,y),0));b=B.Load(int3(x,y,0));}
    ++r.w[25];bool allFinite=true,equalRgb=true,above=false;
    // Dynamic float4 indexing lowers to dp4 with a one-hot mask: 0*NaN
    // from an unrelated component poisons a finite channel. Read RGB scalars.
    [unroll]for(uint channel=0;channel<3;++channel){
     if(isfinite(a[channel])&&isfinite(b[channel])){float delta=abs(a[channel]-b[channel]);++r.w[8];maxRgb=max(maxRgb,delta);sumRgb+=delta;equalRgb=equalRgb&&(delta==0);
      above=above||((tag==0&&pair==0)?delta>1e-5:abs(round(a[channel]*255)-round(b[channel]*255))>1);}
     else {++r.w[9];allFinite=false;}
    }
    if(allFinite){++r.w[10];if(equalRgb)++r.w[11];else ++r.w[12];if(above)++r.w[13];}
    if(isfinite(a.a)){++r.w[26];if(a.a==0)++r.w[27];if(a.a!=1)++r.w[28];}
    if(isfinite(b.a)){++r.w[29];if(b.a==0)++r.w[30];if(b.a!=1)++r.w[31];}
    if(isfinite(a.a)&&isfinite(b.a)){float delta=abs(a.a-b.a);++r.w[16];if(delta==0)++r.w[18];else ++r.w[19];maxAlpha=max(maxAlpha,delta);sumAlpha+=delta;}else ++r.w[17];
    if(tag==0&&pair==0&&all(isfinite(a.rgb))){++r.w[23];if(dot(a.rgb,float3(0.2126,0.7152,0.0722))<=1e-5)++r.w[22];}
   }
  }
  r.w[14]=asuint(maxRgb);r.w[15]=asuint(sumRgb);r.w[20]=asuint(maxAlpha);r.w[21]=asuint(sumAlpha);
  results[tag==2?1:pair]=r;
 }
}
)HLSL";
}
