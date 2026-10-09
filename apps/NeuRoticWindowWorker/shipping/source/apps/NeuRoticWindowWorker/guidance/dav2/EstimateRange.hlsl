// Original packet code. SPDX-License-Identifier: MIT
// One group: stratified 16x16 raw-depth samples, approximate 2nd/98th percentiles.
// Not confidence estimation. Compare against full-image CPU statistics in qualification.
// PreviousRange and NextRange MUST be different ping-pong resources, serialized by the owner.
cbuffer RangeParams : register(b0) {
 uint TensorWidth,ContentWidth,ContentHeight,PadLeft;
 uint PadTop,ResetRange,SourceWidth,SourceHeight;
 float DeltaSeconds,TimeConstantSeconds;float2 Reserved2;
};
ByteAddressBuffer RawTensor : register(t0);
StructuredBuffer<float4> PreviousRange : register(t1);
RWStructuredBuffer<float4> NextRange : register(u0);
Texture2D<float4> SourceColor : register(t2);
groupshared float Samples[256];
groupshared float SceneDifference[256];
[numthreads(256,1,1)]
void CSMain(uint3 tid:SV_GroupThreadID) {
 uint i=tid.x;
 uint2 source=min(uint2(SourceWidth,SourceHeight)-1,uint2((float2(i%16,i/16)+.5)*float2(SourceWidth,SourceHeight)/16));
 float3 color=SourceColor.Load(int3(source,0)).rgb;
 // Read old samples only when range history was initialized and remains valid.
 float difference=0;
 if(ResetRange==0&&PreviousRange[0].z>0){difference=dot(abs(color-PreviousRange[i+1].rgb),float3(1.0/3,1.0/3,1.0/3));}
 SceneDifference[i]=difference;NextRange[i+1]=float4(color,1);
 uint x=PadLeft+min(ContentWidth-1,uint((float(i%16)+.5)*ContentWidth/16));
 uint y=PadTop+min(ContentHeight-1,uint((float(i/16)+.5)*ContentHeight/16));
 float p=asfloat(RawTensor.Load((y*TensorWidth+x)*4));
 Samples[i]=isfinite(p)&&p>=0?p:3.402823466e+38F;
 GroupMemoryBarrierWithGroupSync();
 for(uint k=2;k<=256;k<<=1)for(uint j=k>>1;j>0;j>>=1){
    uint other=i^j;
    if(other>i){
        float a=Samples[i],b=Samples[other];bool ascending=(i&k)==0;
        if((ascending&&a>b)||(!ascending&&a<b)){Samples[i]=b;Samples[other]=a;}
    }
    GroupMemoryBarrierWithGroupSync();
 }
 if(i==0){
    uint valid=0;for(uint n=0;n<256;n++)if(Samples[n]<3.402823466e+38F)valid++;
    if(valid<244){NextRange[0]=0;return;}
    float low=Samples[uint(floor(.02*(valid-1)))],high=Samples[uint(floor(.98*(valid-1)))];
    if(!isfinite(high-low)||high-low<=1e-6){NextRange[0]=0;return;}
    float4 previous=PreviousRange[0];
    float sceneDifference=0;for(uint s=0;s<256;++s)sceneDifference+=SceneDifference[s]/256;
    // A mean RGB change above 0.18 resets only range history; it does not create source metadata.
    if(ResetRange==0&&sceneDifference<=.18&&previous.z>0&&isfinite(previous.x)&&isfinite(previous.y)&&previous.y-previous.x>1e-6){
        float alpha=1-exp(-max(0,DeltaSeconds)/max(1e-6,TimeConstantSeconds));
        low=lerp(previous.x,low,alpha);high=lerp(previous.y,high,alpha);
    }
    NextRange[0]=isfinite(high-low)&&high-low>1e-6?float4(low,high,1,float(valid)/256):0;
 }
}
