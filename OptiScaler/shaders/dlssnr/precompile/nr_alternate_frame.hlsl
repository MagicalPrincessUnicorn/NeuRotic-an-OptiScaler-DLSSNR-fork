// AFNR V3. Preserve full output; reconstruct corrections from matching surfaces.
cbuffer Controls : register(b0) {
    uint2 extent; uint2 depthOrigin;
    uint2 depthExtent; uint2 motionOrigin;
    uint2 motionExtent; uint mode; uint flags;
    float2 jitter; float2 anchorJitter;
    float2 motionScale; float exposureRatio; float invalidDepth;
};
Texture2D<float4> baseScene : register(t0);
Texture2D<float4> enhancedOrResidual : register(t1);
Texture2D<float> currentDepth : register(t2);
Texture2D<float2> motion : register(t3);
Texture2D<float> anchorDepth : register(t4);
RWTexture2D<float4> result : register(u0);
RWTexture2D<float> capturedDepth : register(u1);
RWByteAddressBuffer counts : register(u2);
groupshared uint groupCounts[6];

float storedHalfEven(float value) {
    // SM5 f32tof16/typed stores can truncate. Encode nearest-even explicitly.
    uint bits=asuint(value),sign=(bits>>16)&0x8000,mantissa=bits&0x7fffff;
    int exponent=int((bits>>23)&255)-112;
    if(exponent<=0) {
        if(exponent < -10)return f16tof32(sign);
        mantissa|=0x800000;
        uint shift=uint(14-exponent),rounded=mantissa>>shift;
        uint remainder=mantissa&((1u<<shift)-1),halfway=1u<<(shift-1);
        rounded+=(remainder>halfway||(remainder==halfway&&(rounded&1)))?1:0;
        return f16tof32(sign|rounded);
    }
    uint rounded=(uint(exponent)<<10)|(mantissa>>13),remainder=mantissa&8191;
    rounded+=(remainder>4096||(remainder==4096&&(rounded&1)))?1:0;
    return f16tof32(sign|rounded);
}
float3 storedHalfEven3(float3 value) {return float3(storedHalfEven(value.x),storedHalfEven(value.y),storedHalfEven(value.z));}

float storedUnsignedEven(float value,uint precision) {
    // E5M6 (R/G) or E5M5 (B), no sign/alpha. Return an exactly
    // representable float so the typed UAV store cannot truncate a tie.
    if(value==0)return 0;
    uint bits=asuint(value),significand=(bits&0x7fffff)|0x800000;
    int exponent=int((bits>>23)&255)-127;
    int shift=23-int(precision)+max(-14-exponent,0);
    if(shift>24)return 0;
    uint rounded=significand>>shift,remainder=significand&((1u<<shift)-1);
    uint halfway=1u<<(shift-1);
    rounded+=(remainder>halfway||(remainder==halfway&&(rounded&1)))?1:0;
    return float(rounded)*exp2(float(max(exponent,-14)-int(precision)));
}

bool guide(float2 p, out int2 d, out int2 m) {
    float2 g = p + ((flags & 1) ? jitter : float2(0,0));
    d=0;m=0;
    if (!all(isfinite(g)) || any(g<0) || any(g>=1)) return false;
    d=int2(floor(g*depthExtent))+int2(depthOrigin);
    m=int2(floor(g*motionExtent))+int2(motionOrigin);
    return true;
}
bool validDepth(float z) {
    return isfinite(z) && ((flags&4) ? z>0 : (z>=0 && z<=1)) &&
        (!(flags&8) || z!=invalidDepth);
}
bool depthMatch(float a,float b) {
    // Device depth is nonlinear. A fixed .002 accepts unrelated surfaces
    // near the far plane, particularly with reversed depth.
    float da=(flags&64)?a:1-a,db=(flags&64)?b:1-b;
    return validDepth(a) && validDepth(b) &&
        ((flags&4) ? abs(a-b)<=.01*max(a,b) : abs(a-b)<=max(1e-6,.02*max(da,db)));
}
float luminance(float3 c) {return max(0,dot(c,float3(.2126,.7152,.0722)));}
bool storeScene(float4 value,out float4 stored) {
    stored=value;
    if(!all(isfinite(value))||any(abs(value)>65504))return false;
    if(flags&32) {
        if(any(value.rgb<0)||any(value.rgb>float3(65024,65024,64512)))return false;
        stored=float4(storedUnsignedEven(value.r,6),storedUnsignedEven(value.g,6),storedUnsignedEven(value.b,5),1);
    }else stored=float4(storedHalfEven3(value.rgb),value.a);
    return true;
}
uint capture(uint2 xy) {
    float4 b=baseScene.Load(int3(xy,0));
    float4 e=enhancedOrResidual.Load(int3(xy,0));
    float3 residual=e.rgb-b.rgb;
    int2 d,m;float z=0;
    bool mapped=guide((float2(xy)+.5)/float2(extent),d,m);
    if(mapped)z=currentDepth.Load(int3(d,0));
    bool valid=mapped && validDepth(z) && all(isfinite(b.rgb)) && all(isfinite(e.rgb)) &&
        all(isfinite(residual)) && all(abs(residual)<=65504);
    // Alpha stores log luminance of the original scene; -1 marks invalid.
    // Store the correction actually present on the unmodified full frame.
    result[xy]=valid ? float4(storedHalfEven3(residual),storedHalfEven(log2(1+luminance(b.rgb)))) : float4(0,0,0,-1);
    capturedDepth[xy]=valid ? z : 0;
    return valid ? 0 : 2;
}
float sampleConfidence(float4 r,float z,float current,float currentLuma) {
    if(!all(isfinite(r))||r.a<0||!depthMatch(z,current))return 0;
    float previousLuma=exposureRatio*(exp2(r.a)-1);
    float change=abs(currentLuma-previousLuma)/max(.001,max(currentLuma,previousLuma));
    return 1-smoothstep(.10,.35,change);
}
uint carry(uint2 xy) {
    float4 b=baseScene.Load(int3(xy,0));
    if(!all(isfinite(b)))return 4;
    float2 p=(float2(xy)+.5)/float2(extent);
    int2 d,m;if(!guide(p,d,m))return 1;
    float2 mv=motion.Load(int3(m,0));
    float2 q=p+mv*motionScale+((flags&2) ? jitter-anchorJitter : float2(0,0));
    if(!all(isfinite(q))||any(q<0)||any(q>1))return 1;
    float current=currentDepth.Load(int3(d,0));
    float2 u=q*float2(extent)-.5;int2 origin=int2(floor(u));float2 fraction=u-floor(u);
    float3 sum=0;float total=0;float currentLuma=luminance(b.rgb);
    [unroll] for(int y=0;y<2;y++) [unroll] for(int x=0;x<2;x++) {
        float w=(x?fraction.x:1-fraction.x)*(y?fraction.y:1-fraction.y);
        if(w>0) {
            int2 tap=origin+int2(x,y);
            if(any(tap<0)||any(tap>=int2(extent)))return 1;
            float4 r=enhancedOrResidual.Load(int3(tap,0));
            if(!isfinite(r.a)||r.a<0)return 2;
            if(!all(isfinite(r.rgb)))return 4;
            float confidence=sampleConfidence(r,anchorDepth.Load(int3(tap,0)),current,currentLuma);
            sum+=w*confidence*r.rgb;total+=w*confidence;
        }
    }
    if(total<.05) {
        // Only repair small disocclusions from nearby history that matches the
        // current surface. Never spread an unvalidated foreground correction.
        sum=0;total=0;
        int2 center=int2(floor(u+.5));
        [loop] for(int y=-2;y<=2;y++) [loop] for(int x=-2;x<=2;x++) {
            int2 tap=center+int2(x,y);
            if(any(tap<0)||any(tap>=int2(extent)))continue;
            float4 r=enhancedOrResidual.Load(int3(tap,0));
            float confidence=sampleConfidence(r,anchorDepth.Load(int3(tap,0)),current,currentLuma);
            if(confidence<=0)continue;
            float2 distance=float2(tap)-u;
            float w=confidence/(1+dot(distance,distance));
            sum+=w*r.rgb;total+=w;
        }
    }
    if(total<.05)return 3;
    // Confidence chooses samples, not effect strength. Renormalization retains
    // a constant correction as coverage changes, avoiding alternate-frame pulses.
    float4 value=float4(b.rgb+exposureRatio*(sum/total),b.a),stored;
    if(!storeScene(value,stored))return 5;
    // Rejection never writes the destination. Current alpha is copied exactly.
    // Typed UAV conversion may round toward zero. Make the reference's half
    // nearest-even storage explicit; current alpha is already stored half.
    result[xy]=stored;
    return 0;
}
[numthreads(8,8,1)]
void main(uint3 id:SV_DispatchThreadID,uint index:SV_GroupIndex) {
    if(index<6)groupCounts[index]=0;
    GroupMemoryBarrierWithGroupSync();
    if(all(id.xy<extent)) {
        uint reason;
        // HLSL conditional expressions can evaluate both operands. These
        // functions write UAVs, so use control flow to preserve rejected pixels.
        [branch] if(mode==0) reason=capture(id.xy);
        else reason=carry(id.xy);
        InterlockedAdd(groupCounts[reason],1);
    }
    GroupMemoryBarrierWithGroupSync();
    if(index<6 && (flags&16))counts.InterlockedAdd(index*4,groupCounts[index]);
}
