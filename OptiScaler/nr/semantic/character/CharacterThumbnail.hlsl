
Texture2D<float4> Source:register(t0);
RWTexture2D<float4> Target:register(u0);
cbuffer Sizes:register(b0){uint SW;uint SH;uint TW;uint TH;uint ColorPolicy;float PreExposure;};
float3 PqNits(float3 v){
 float3 p=pow(saturate(v),1.0/(2523.0/32.0));
 return 10000.0*pow(max(p-3424.0/4096.0,0.0)/max(2413.0/128.0-(2392.0/128.0)*p,1e-6),1.0/(2610.0/16384.0));
}
[numthreads(8,8,1)] void main(uint3 p:SV_DispatchThreadID){
 if(p.x>=TW||p.y>=TH)return;
 float2 at=(float2(p.xy)+.5)*float2(SW,SH)/float2(TW,TH)-.5;
 int2 a=int2(floor(at));float2 f=frac(at);
 int2 hi=int2(SW-1,SH-1);
 float4 v=lerp(lerp(Source.Load(int3(clamp(a,int2(0,0),hi),0)),Source.Load(int3(clamp(a+int2(1,0),int2(0,0),hi),0)),f.x),
               lerp(Source.Load(int3(clamp(a+int2(0,1),int2(0,0),hi),0)),Source.Load(int3(clamp(a+int2(1,1),int2(0,0),hi),0)),f.x),f.y);
 if(ColorPolicy==1){
   float3 relativeNits=PqNits(v.rgb)/203.0;
   v.rgb=mul(float3x3(1.660491,-.587641,-.072850,-.124550,1.132900,-.008349,-.018151,-.100579,1.118730),relativeNits);
 }else if(ColorPolicy==2){v.rgb=max(v.rgb,0.0)*80.0/203.0;}
 else if(ColorPolicy==3){v.rgb=max(v.rgb,0.0)/max(PreExposure,1e-6);}
 if(ColorPolicy!=0){v.rgb=max(v.rgb,0.0);v.rgb=pow(v.rgb/(1.0+v.rgb),1.0/2.2);}
 Target[p.xy]=float4(saturate(v.rgb),1);
}
