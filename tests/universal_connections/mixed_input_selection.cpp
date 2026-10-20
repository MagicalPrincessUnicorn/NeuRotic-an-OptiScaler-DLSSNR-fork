#include <dlssnr/connections/MixedInputSelection.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
namespace M=DlssNr::Connections::Mixed;
using DlssNr::Connections::Source;
static void Check(bool ok,const char* what){if(!ok){std::fprintf(stderr,"FAIL %s\n",what);std::exit(1);}}
static M::Anchor Anchor(){return {{1,2,3,4,5,7,7},M::Stage::Present,Source::Native,256,64,6};}
static M::Offer Offer(M::Signal signal,Source source){M::Offer o{};o.signal=signal;o.source=source;o.frame=Anchor().frame;
    o.stage=M::Stage::Present;o.previous=6;o.providerGeneration=1;o.observed=true;o.conventionsKnown=true;
    o.synchronized=true;o.immutableLease=true;o.width=256;o.height=64;return o;}
int main(){
    M::PolicyStore store;Check(store.Read().depth==Source::Automatic,"new profiles default to Auto");
    Check(store.Request(3,4,true),"valid preferences accepted");auto saved=store.Read();
    Check(store.Request(3,4,true,1)&&store.Read().color==Source::Native,"independent color policy snapshot");
    saved=store.Read();
    Check(!store.Request(99,4,false)&&store.Read().revision==saved.revision,"invalid choice preserves active intent");
    std::thread writer([&]{for(int n=0;n<20000;++n)store.Request(n%2?1:3,n%2?2:4,n%2!=0);});
    for(int n=0;n<20000;++n){auto snapshot=store.Read();Check((snapshot.depth==Source::Native&&snapshot.motion==Source::BuiltIn&&snapshot.strict)||
        (snapshot.depth==Source::ReShade&&snapshot.motion==Source::External),"one coherent policy snapshot per frame");}writer.join();
    auto a=Anchor();auto z=Offer(M::Signal::Depth,Source::ReShade),mv=Offer(M::Signal::Motion,Source::Native);
    auto ownedColor=Offer(M::Signal::Color,Source::Native);
    M::Policy p;M::History history;M::Offer offers[]{mv,z,ownedColor};
    auto r=M::Select(a,offers,p,history);
    Check(r.ready&&r.depth==1&&r.motion==0,"partial providers compose depth and motion");
    for(unsigned field=0;field<7;++field){auto wrong=z;
        switch(field){case 0:++wrong.frame.session;break;case 1:++wrong.frame.view;break;case 2:++wrong.frame.target;break;
        case 3:++wrong.frame.generation;break;case 4:++wrong.frame.device;break;case 5:++wrong.frame.capture;break;case 6:wrong.frame.scene=0;break;}
        M::Offer bad[]{mv,wrong,ownedColor};Check(!M::Select(a,bad,p,{}).ready,"wrong frame identity refused");}
    auto bad=z;bad.stage=M::Stage::AfterPresent;Check(M::Compatible(a,bad)==M::Reason::LateStage,"late publication refused");
    bad=z;bad.frame.scope=M::IdentityScope::CoherentCapture;bad.frame.scene=0;
    Check(M::Compatible(a,bad)==M::Reason::UnknownIdentity,"capture identity cannot impersonate rendered scene");
    bad=z;bad.conventionsKnown=false;Check(M::Compatible(a,bad)==M::Reason::UnknownConvention,"unknown convention refused");
    bad=z;bad.immutableLease=false;Check(M::Compatible(a,bad)==M::Reason::MissingLease,"mutable content refused");
    bad=z;bad.synchronized=false;Check(M::Compatible(a,bad)==M::Reason::MissingSynchronization,"unsynchronized content refused");
    bad=mv;bad.previous=5;Check(M::Compatible(a,bad)==M::Reason::WrongInterval,"wrong motion endpoint refused");
    bad=z;bad.width=128;Check(M::Compatible(a,bad)==M::Reason::UnsupportedRaster,"unmapped crop refused");
    p.depth=Source::External;r=M::Select(a,offers,p,{});
    Check(r.ready&&r.depth==1&&r.depthReason==M::Reason::PreferenceFallback,"manual preference visibly falls back");
    p.strict=true;r=M::Select(a,offers,p,{});Check(!r.ready&&r.depth<0,"strict preference cannot silently fall back");
    Check(store.RequestPersisted(99,0,true),"unknown saved preference retained as an unsupported request");
    r=M::Select(a,offers,store.Read(),{});Check(!r.ready&&r.depth<0,"unknown strict preference cannot become Auto");
    Check(store.RequestPersisted(99,0,false),"unknown saved preference supports explicit fallback policy");
    r=M::Select(a,offers,store.Read(),{});Check(r.ready&&r.depthReason==M::Reason::PreferenceFallback,"unknown non-strict preference falls back visibly");
    p={};M::Offer onlyDepth[]{z,ownedColor};r=M::Select(a,onlyDepth,p,{});
    for(bool strict:{false,true})for(bool motion:{false,true}){
        M::Policy withheld;withheld.strict=strict;
        (motion?withheld.motion:withheld.depth)=Source::None;
        const auto suppressed=M::Select(a,offers,withheld,{});
        Check(suppressed.ready == !strict && !suppressed.requestMotion,"None is a soft preference unless enforced");
        Check((motion?suppressed.motionReason:suppressed.depthReason)==(strict?M::Reason::IntentionallyDisabled:M::Reason::PreferenceFallback),"None reports enforced withholding or fallback");
    }
    Check(!r.ready&&r.requestMotion&&r.depth==0,"only missing motion requests estimator");
    p.motion=Source::External;p.strict=true;r=M::Select(a,onlyDepth,p,{});
    Check(!r.requestMotion,"strict external does not activate unrelated estimator");
    auto other=z;other.source=Source::BuiltIn;other.cost=1;z.cost=2;M::Offer peers[]{mv,z,other,ownedColor};
    history.depth=Source::ReShade;history.depthGeneration=1;p={};r=M::Select(a,peers,p,history);
    Check(r.depth==1,"valid incumbent is stable against similar candidates");
    peers[1].synchronized=false;r=M::Select(a,peers,p,history);Check(r.depth==2,"invalid incumbent falls back immediately");
    a.frame.capture=0;r=M::Select(a,offers,p,{});Check(!r.ready,"unknown anchor refused");
    a=Anchor();p={};
    auto color=Offer(M::Signal::Color,Source::Native);
    M::Offer colors[]{mv,z,color};
    p.color=Source::Native;
    r=M::Select(a,colors,p,{});
    Check(r.ready&&r.color==2&&r.depth==1&&r.motion==0,"Native color is independent of ReShade depth and Native motion");
    p.color=Source::External;p.strict=true;
    r=M::Select(a,colors,p,{});Check(!r.ready&&r.color<0,"strict color preference cannot fall back");
    p.strict=false;r=M::Select(a,colors,p,{});
    Check(r.ready&&r.color==2&&r.colorReason==M::Reason::PreferenceFallback,"color fallback remains visible");
    Check(store.RequestPersisted(0,0,true,99)&&uint32_t(store.Read().color)==7,"unknown color preserved as unsupported");
    Check(!M::Select(a,colors,store.Read(),{}).ready,"unknown strict color refuses");
    p.color=Source::None;r=M::Select(a,colors,p,{});
    Check(r.ready&&r.colorReason==M::Reason::PreferenceFallback,"soft None color uses compatible fallback"); p.strict=true; r=M::Select(a,colors,p,{}); Check(!r.ready&&!r.requestMotion&&r.colorReason==M::Reason::IntentionallyDisabled,"strict None color withholds the consumer");
    const std::array<M::Reason,3> nativeBound{M::Reason::Ready,M::Reason::Ready,M::Reason::Ready};
    p={};p.color=Source::Native;p.depth=Source::ReShade;
    r=M::SelectNativeBindings(p,nativeBound);
    Check(r.ready&&r.color==0&&r.depthReason==M::Reason::PreferenceFallback,"same-callback Native binding shares independent policy ranking");
    p.strict=true;Check(!M::SelectNativeBindings(p,nativeBound).ready,"Native callback cannot impersonate unavailable ReShade depth");
    p={};p.color=Source::None;Check(M::SelectNativeBindings(p,nativeBound).ready,"Native soft None falls back");p.strict=true;Check(!M::SelectNativeBindings(p,nativeBound).ready,"Native strict None withholds color");
    p={};auto revoked=nativeBound;revoked[0]=M::Reason::MissingLease;
    Check(!M::SelectNativeBindings(p,revoked).ready,"policy never overrides revoked Native color admission");
    std::puts("PASS mixed input identity, stage, semantics, preference, stability and demand");
}
