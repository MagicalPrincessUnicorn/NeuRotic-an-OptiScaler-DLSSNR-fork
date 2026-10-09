#include "../../apps/NeuRoticWindowWorker/diagnostics/FrameProbe.h"
#include <iostream>
namespace nrw {
int RunFrameProbeContractTests(){
 int failed=0;auto check=[&](bool ok,const char* reason){std::cout<<(ok?"PASS ":"FAIL ")<<reason<<'\n';failed+=!ok;};
 check(ProbeGridCount(1,1)==1&&ProbeGridCount(5,9)==45&&ProbeGridCount(17,9)==144&&ProbeGridCount(65,33)==256&&ProbeGridCount(1278,1391)==256,"grid deduplicates tiny and odd extents without multiplying the denominator");
 check(ProbeGridCount(0,9)==0&&ProbeGridCount(16385,9)==0,"grid refuses empty or unsupported extents");
 ProbeIdentity token;token.requestId=7;token.frame={9,3,42,17,9,false,1,2,4};token.targetGeneration=2;token.outputEpoch=2;token.requestedRevision=token.appliedRevision=token.processingRevision=token.comparisonRevision=4;token.workWidth=token.outputWidth=17;token.workHeight=token.outputHeight=9;
 ProbeAdmission gate;
 check(gate.Request(7)&&gate.Bind(token),"explicit request binds exactly one immutable eligible frame");
 check(!gate.Request(8)&&gate.RequestId()==7,"repeated request cannot replace a pending owner");
 gate.Record(ProbeStage::Nr);gate.Require(ProbeStage::Nr,12);
 check(!gate.Observe(ProbeStage::Nr,11)&&!gate.Retired(),"stale completed value cannot retire newly recorded reads");
 gate.Invalidate("geometry changed");check(!gate.Request(8)&&gate.Status()["summaries"].is_null(),"invalidation hides metrics but does not free pending GPU ownership");
 check(gate.Observe(ProbeStage::Nr,12)&&gate.Retired()&&gate.Request(8),"late exact completion permits reuse only after prior last use retires");
 auto next=token;next.requestId=8;check(gate.Bind(next),"next manual request reuses the single retired owner");
 gate.Record(ProbeStage::Nr);gate.Require(ProbeStage::Nr,13);gate.Unknown("signal failed");
 check(!gate.Observe(ProbeStage::Nr,99)&&!gate.Request(9)&&!gate.Retired(),"unknown signal ownership remains quarantined even after unrelated later values");
 ProbeAdmission lost;check(lost.Request(1),"independent test owner admits request");auto one=token;one.requestId=1;check(lost.Bind(one),"independent owner binds token");lost.Record(ProbeStage::Publication);lost.Require(ProbeStage::Publication,5);
 check(!lost.Observe(ProbeStage::Publication,UINT64_MAX)&&!lost.Retired()&&!lost.Request(2),"device-loss fence sentinel cannot prove retirement");
 ProbeAdmission mismatch;check(mismatch.Request(7)&&mismatch.Bind(token),"identity test starts from admitted frame");auto wrong=token;++wrong.frame.geometryEpoch;
 check(!mismatch.Matches(wrong)&&mismatch.Status()["summaries"].is_null(),"same dimensions cannot hide a mismatched geometry epoch");
 auto changed=token;changed.transferStrength=.5f;check(!mismatch.Matches(changed),"transfer strength is part of the immutable frame configuration");changed=token;changed.colourStrength=.5f;check(!mismatch.Matches(changed),"colour strength is part of immutable configuration");changed=token;changed.modelStyle=2;check(!mismatch.Matches(changed),"model style is part of immutable configuration");changed=token;changed.comparisonDirection=7;check(!mismatch.Matches(changed),"comparison direction is part of immutable configuration");
 ProbeAdmission unsupported;check(unsupported.Request(7),"unsupported route still uses independent request identity");auto scaled=token;scaled.workWidth=12;
 check(!unsupported.Bind(scaled)&&unsupported.Status()["state"]=="unsupported"&&unsupported.Status()["summaries"].is_null(),"scaled route returns unsupported without a zero-effect result");
 ProbeRecord record;record.words={7,0,9,0,3,0,17,9,432,0,144,144,0,0,0,0,144,0,144,0,0,0,0,144,0,144,144,0,0,144,0,0};
 auto opaque=DecodeProbeRecord(record,token,0);
 check(opaque&&(*opaque)["rgb"]["equalPixels"]==144&&(*opaque)["rgb"]["meanAbsoluteDifference"]==0&&(*opaque)["alpha"]["lhs"]["notOne"]==0,"aggregate decoder exposes finite opaque identity with correct independent denominators");
 record.words[27]=record.words[28]=record.words[30]=record.words[31]=144;auto transparent=DecodeProbeRecord(record,token,0);
 check(transparent&&(*transparent)["alpha"]["equalSamples"]==144&&(*transparent)["alpha"]["lhs"]["zero"]==144,"zero-alpha identity is distinguishable from opaque despite equal alpha differences");
 record.words[16]=record.words[18]=record.words[26]=record.words[27]=record.words[28]=0;record.words[17]=144;
 auto nonfinite=DecodeProbeRecord(record,token,0);
 check(nonfinite&&(*nonfinite)["alpha"]["meanAbsoluteDifference"].is_null()&&(*nonfinite)["alpha"]["lhs"]["zero"].is_null()&&(*nonfinite)["alpha"]["rhs"]["zero"]==144,"one-sided nonfinite alpha cannot hide the other side's absolute zero counts");
 record.words[0]=6;check(!DecodeProbeRecord(record,token,0),"mismatched aggregate request identity cannot report complete");
 record.words[0]=7;record.words[25]=0;check(!DecodeProbeRecord(record,token,0),"zero sample denominator cannot report equality");
 std::cout<<"Frame probe contract failures: "<<failed<<'\n';return failed?1:0;
}
}
#ifdef NRW_PROBE_CONTRACT_TEST_MAIN
int main(){return nrw::RunFrameProbeContractTests();}
#endif
