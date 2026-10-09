#include "../../apps/NeuRoticWindowWorker/diagnostics/FrameProbe.h"
#include <iostream>
int main(){
 using namespace nrw;unsigned failed=0;auto check=[&](bool ok,const char* name){std::cout<<(ok?"PASS ":"FAIL ")<<name<<'\n';failed+=!ok;};
 ProbeIdentity id;id.requestId=7;id.frame={9,3,42,17,9,false,1,2,4};id.targetGeneration=id.outputEpoch=2;id.requestedRevision=id.appliedRevision=4;id.workWidth=id.outputWidth=17;id.workHeight=id.outputHeight=9;
 ProbeRecord original;original.words={7,0,9,0,3,0,17,9,432,0,144,144,0,0,0,0,144,0,144,0,0,0,0,144,0,144,144,0,0,144,0,0};
 check(DecodeProbeRecord(original,id,0).has_value(),"Canonical opaque aggregate is valid");
 auto bad=original;bad.words[14]=bad.words[15]=std::bit_cast<uint32_t>(.25f);check(!DecodeProbeRecord(bad,id,0),"All-equal RGB comparisons cannot contain positive differences");
 bad=original;bad.words[11]=0;bad.words[12]=144;bad.words[14]=bad.words[15]=std::bit_cast<uint32_t>(1e20f);check(!DecodeProbeRecord(bad,id,0),"Finite raw differences cannot exceed the FP16 domain");
 bad=original;bad.words[14]=std::bit_cast<uint32_t>(.5f);bad.words[15]=std::bit_cast<uint32_t>(.25f);bad.words[11]=143;bad.words[12]=1;check(!DecodeProbeRecord(bad,id,0),"Nonnegative channel sum cannot be smaller than its maximum");
 bad=original;bad.words[14]=std::bit_cast<uint32_t>(.5f);bad.words[15]=std::bit_cast<uint32_t>(432.f);bad.words[11]=0;bad.words[12]=144;check(!DecodeProbeRecord(bad,id,0),"Channel sum cannot exceed finite-channel count times maximum");
 bad=original;bad.words[8]=bad.words[10]=bad.words[11]=0;bad.words[9]=432;bad.words[14]=bad.words[15]=std::bit_cast<uint32_t>(.25f);check(!DecodeProbeRecord(bad,id,0),"Zero finite-channel denominator cannot retain numeric differences");
 bad=original;bad.words[20]=bad.words[21]=std::bit_cast<uint32_t>(.25f);check(!DecodeProbeRecord(bad,id,0),"All-equal alpha comparisons cannot contain positive differences");
 bad=original;bad.words[16]=bad.words[18]=0;bad.words[17]=144;check(!DecodeProbeRecord(bad,id,0),"Two fully finite alpha sides require fully finite pair comparisons");
 bad=original;bad.words[23]=0;check(!DecodeProbeRecord(bad,id,0),"Fully finite RGB pairs require finite model RGB denominator");
 auto unorm=original;unorm.words[24]=2;unorm.words[23]=0;check(DecodeProbeRecord(unorm,id,2).has_value(),"Canonical byte-boundary aggregate is valid");
 bad=unorm;bad.words[11]=0;bad.words[12]=144;bad.words[14]=bad.words[15]=std::bit_cast<uint32_t>(2.f);check(!DecodeProbeRecord(bad,id,2),"Normalized byte-boundary maximum cannot exceed one");
 // A partially nonfinite RGB pixel can have finite differing channels even
 // when every fully finite pixel is equal; do not reject that valid case.
 auto partial=original;partial.words[8]=431;partial.words[9]=1;partial.words[10]=partial.words[11]=143;partial.words[14]=partial.words[15]=std::bit_cast<uint32_t>(.5f);check(DecodeProbeRecord(partial,id,0).has_value(),"Partially finite differing channels retain their own denominator");
 std::cout<<"Aggregate validation failures: "<<failed<<'\n';return failed?1:0;
}
