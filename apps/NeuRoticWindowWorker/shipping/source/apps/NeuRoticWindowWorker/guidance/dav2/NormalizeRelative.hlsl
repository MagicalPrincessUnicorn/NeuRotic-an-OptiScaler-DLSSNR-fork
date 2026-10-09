// Original packet code. SPDX-License-Identifier: MIT
// OPTIONAL consumer experiment, not conversion to engine device Z or meters.
// Range[0] = {low, high, numericRangeValid, unused}, from current-generation GPU range owner.
Texture2D<float> RawDepth : register(t0);
StructuredBuffer<float4> Range : register(t1);
Texture2D<float> NumericValidity : register(t2);
RWTexture2D<float> RelativeNearHigh : register(u0);
[numthreads(8,8,1)]
void CSMain(uint3 id:SV_DispatchThreadID) {
 uint w,h;RelativeNearHigh.GetDimensions(w,h);if(id.x>=w||id.y>=h)return;
 float4 r=Range[0];float p=RawDepth.Load(int3(id.xy,0));
 bool ok=r.z>0&&isfinite(p)&&isfinite(r.x)&&isfinite(r.y)&&r.y-r.x>1e-6&&NumericValidity.Load(int3(id.xy,0))>0;
 RelativeNearHigh[id.xy]=ok?saturate((p-r.x)/(r.y-r.x)):0;
 // Host must suppress guide publication for an invalid range; zeros are not a valid substitute.
}
