#include <dlssnr/PresentSourcePolicy.h>
#include <dlssnr/SourceAvailability.h>
#include <cassert>
#include <cstdio>
namespace M=DlssNr::Connections::Mixed;
namespace S=DlssNr::PresentSources;
namespace A=DlssNr::SourceAvailability;
using DlssNr::Connections::Source;
int main(){
 M::Policy p; p.revision=7;
 auto s=S::Choose(p,true);assert(s.allowed&&s.receipt.color==2&&s.receipt.depth==1&&s.receipt.motion==1);
 p.strict=true;p.depth=p.motion=Source::BuiltIn;
 auto captured=S::Choose(p,true,Source::BuiltIn);
 assert(captured.allowed&&!captured.guidesOmitted&&captured.receipt.depth==unsigned(Source::BuiltIn));
 p.depth=Source::Native;captured=S::Choose(p,true,Source::BuiltIn);
 assert(captured.allowed&&captured.guidesOmitted&&!captured.receipt.depth&&!captured.receipt.motion);
 p={};p.revision=7;
 p.color=Source::Native;s=S::Choose(p,true);assert(s.allowed&&s.receipt.colorReason==unsigned(M::Reason::PreferenceFallback));
 p.strict=true;assert(!S::Choose(p,true).allowed);
 for(auto signal:{0,1,2}){p={};(signal==0?p.color:signal==1?p.depth:p.motion)=Source::None;
  assert(S::Choose(p,true).allowed);p.strict=true;s=S::Choose(p,true);assert(s.allowed==(signal!=0));
  if(signal!=0)assert(s.guidesOmitted&&!s.receipt.depth&&!s.receipt.motion);}
 p={};s=S::Choose(p,false);assert(s.allowed&&s.receipt.color==2&&!s.receipt.depth&&!s.receipt.motion);
 p.depth=Source::Native;p.strict=true;s=S::Choose(p,false);assert(s.allowed&&s.guidesOmitted);
 p={};p.depth=Source::ReShade;assert(S::Choose(p,true).allowed);p.strict=true;s=S::Choose(p,true);assert(s.allowed&&s.guidesOmitted&&!s.receipt.depth&&!s.receipt.motion);
 for(auto source:{Source::BuiltIn,Source::ReShade,Source::External,Source::None})for(bool depth:{false,true}){p={};p.strict=true;(depth?p.depth:p.motion)=source;s=S::Choose(p,true);assert(s.allowed&&s.guidesOmitted&&s.receipt.color==2&&!s.receipt.depth&&!s.receipt.motion);}
 s=S::Choose(p,true);s.receipt.revision=9;s.receipt.capture=20;
 assert(S::AcceptCompletion(s.receipt,9,2,2,19));
 assert(!S::AcceptCompletion(s.receipt,10,2,2,19));
 assert(!S::AcceptCompletion(s.receipt,9,2,3,19));
 assert(!S::AcceptCompletion(s.receipt,9,2,2,21));
 A::Refresh cache;A::Context context;context.route=1;
 assert(cache.Update(context,100));assert(!cache.Update(context,110));
 assert(!cache.StartProbe(299));assert(cache.StartProbe(300));assert(!cache.StartProbe(301));
 assert(!cache.Ready(299));assert(cache.Ready(1100));cache.Complete();assert(!cache.Ready(9000));
 assert(!cache.Update(context,9000));context.policy=2;assert(cache.Update(context,9001));
 cache.Request(9050);assert(!cache.Ready(9100));assert(cache.Ready(10050));cache.Complete();
 context.enabled=true;assert(cache.Update(context,11000));cache.Complete();
 for(unsigned i=0;i<100000;++i)assert(!cache.Update(context,12000+i)&&!cache.Ready(12000+i));
 A::PassiveProbe probe;auto token=probe.Request();assert(probe.Pending()==token);probe.Complete(token,5);
 assert(!probe.Pending()&&probe.Read(token)==5);auto newer=probe.Request();assert(!probe.Read(newer));
 probe.Complete(token,7);assert(!probe.Read(newer));probe.Complete(newer,3);assert(probe.Read(newer)==3);
 std::puts("PASS Present actual sources, soft/strict preferences, neutral guides, change-only refresh and late observation rejection");
}
