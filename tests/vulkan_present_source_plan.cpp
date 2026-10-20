// Actual production helper; executable qualification is owned by build owner05.
#include <dlssnr/VulkanPresentSourcePlan.h>
#include <cstdlib>
#include <cstdio>
namespace V=DlssNr::VkPresentSources;
namespace M=DlssNr::Connections::Mixed;
namespace P=DlssNr::PresentInput;
namespace I=DlssNr::PresentInputDecision;
namespace R=DlssNr::PresentResolution;
using DlssNr::Connections::Source;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 M::Policy p{};p.revision=9;
 const R::Policy full{R::FullOutput,0};
 auto a=V::Plan(p,V::GuideKind::Native,P::Policy::AutoGuides,true,full);
 Check(a.allowed&&a.useNativeGuides&&!a.useCapturedGuides&&a.input.input==I::InputClass::Guided,"automatic native tuple stays guided");
 Check(a.sources.receipt.color==unsigned(Source::BuiltIn)&&a.sources.receipt.depth==unsigned(Source::Native)&&a.sources.receipt.motion==unsigned(Source::Native),"native actual provenance");
 Check(V::CanSubmit(a,9)&&!V::CanSubmit(a,10),"late revision change cannot submit");
 auto b=V::Plan(p,V::GuideKind::Captured,P::Policy::AutoGuides,true,full);
 Check(b.allowed&&b.useCapturedGuides&&!b.useNativeGuides&&b.sources.receipt.depth==unsigned(Source::BuiltIn)&&b.sources.receipt.motion==unsigned(Source::BuiltIn),"captured actual provenance is BuiltIn");
 p.strict=true;p.depth=p.motion=Source::Native;
 a=V::Plan(p,V::GuideKind::Native,P::Policy::RequireGuides,true,full);
 Check(a.allowed&&a.useNativeGuides,"strict native pair admitted");
 b=V::Plan(p,V::GuideKind::Captured,P::Policy::RequireGuides,true,full);
 Check(!b.allowed&&!b.useCapturedGuides&&!V::CanSubmit(b,9),"strict foreign captured source fails before bind/record");
 p.depth=p.motion=Source::BuiltIn;
 b=V::Plan(p,V::GuideKind::Captured,P::Policy::RequireGuides,true,full);
 Check(b.allowed&&b.useCapturedGuides,"strict BuiltIn pair matches captured owner");
 for(auto unavailable:{Source::None,Source::ReShade,Source::External})for(bool depth:{false,true}){
  p={};p.revision=9;p.strict=true;(depth?p.depth:p.motion)=unavailable;
  a=V::Plan(p,V::GuideKind::Native,P::Policy::RequireGuides,true,full);
  Check(a.allowed&&!a.useNativeGuides&&!a.useCapturedGuides&&a.input.input==I::InputClass::ImageOnly,"native strict refused guide pair uses existing image-only behavior");
  Check(!a.sources.receipt.depth&&!a.sources.receipt.motion,"omitted guides publish no used source");
  b=V::Plan(p,V::GuideKind::Captured,P::Policy::AutoGuides,true,full);
  Check(!b.allowed&&!b.useCapturedGuides,"captured contract never silently transfers to image-only");
 }
 p={};p.revision=9;p.strict=true;p.color=Source::External;
 a=V::Plan(p,V::GuideKind::Native,P::Policy::AutoGuides,true,full);
 Check(!a.allowed&&!V::CanSubmit(a,9),"strict unavailable required color prevents all record/submit");
 p.strict=false;
 a=V::Plan(p,V::GuideKind::Native,P::Policy::AutoGuides,true,full);
 Check(a.allowed&&a.sources.receipt.color==unsigned(Source::BuiltIn)&&a.sources.receipt.colorReason==unsigned(M::Reason::PreferenceFallback),"soft unavailable color reports actual fallback");
 p={};p.revision=9;
 a=V::Plan(p,V::GuideKind::None,P::Policy::RequireGuides,false,full);
 Check(!a.allowed&&!V::CanSubmit(a,9),"missing required tuple does no work");
 a=V::Plan(p,V::GuideKind::None,P::Policy::AutoGuides,false,{R::FollowNative,0});
 Check(a.allowed&&a.input.input==I::InputClass::ImageOnly&&a.input.workloadFallback&&a.input.resolution.mode==R::FullOutput,"missing size preserves existing automatic workload fallback");
 a=V::Plan(p,V::GuideKind::Native,P::Policy::ImageOnly,true,full);
 Check(a.allowed&&!a.useNativeGuides&&!a.useCapturedGuides&&!a.sources.receipt.depth&&!a.sources.receipt.motion,"explicit image-only never records guide consumption");
 p.strict=true;p.depth=Source::External;
 a=V::Plan(p,V::GuideKind::Native,P::Policy::ImageOnly,false,{R::FollowNative,0});
 Check(!a.allowed,"guide preference cannot relax explicit image-only render-size guard");
 p={};p.revision=9;
 a=V::Plan(p,V::GuideKind::Native,P::Policy::Invalid,true,full);
 Check(!a.allowed&&!V::CanSubmit(a,9),"invalid saved input policy fails closed");
 // Compatibility overload is used by DXGI and must retain exact old semantics.
 auto old=DlssNr::PresentSources::Choose(p,true);
 Check(old.allowed&&old.receipt.depth==unsigned(Source::Native),"DXGI default overload remains native");
 std::puts("PASS actual production Vulkan source plan: automatic/strict/soft, native/captured provenance, omission, workload and late-revision admission");
}
