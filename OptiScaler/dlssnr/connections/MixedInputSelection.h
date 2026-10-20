#pragma once
#include "ConnectionSelection.h"
#include <array>
#include <span>
#include <cstddef>
#include <limits>
#include <atomic>

namespace DlssNr::Connections::Mixed {
// Owner-established identities, not a Present counter or resource address.
// capture identifies the acquisition interval; scene identifies rendered content.
enum class IdentityScope : uint8_t { RenderScene, CoherentCapture };
struct Frame {
    uint64_t session=0,view=0,target=0,generation=0,device=0,capture=0,scene=0;
    IdentityScope scope=IdentityScope::RenderScene;
    bool operator==(const Frame&)const=default;
    bool Known()const noexcept{return session&&view&&target&&generation&&device&&capture&&
        (scope==IdentityScope::RenderScene?scene!=0:scope==IdentityScope::CoherentCapture&&scene==0);}
};
enum class Stage : uint8_t { Unknown, PreUpscale, PostUpscale, Present, AfterPresent };
enum class Signal : uint8_t { Depth, Motion, Color };
enum class Reason : uint8_t {
    Ready, Unavailable, UnknownIdentity, WrongFrame, WrongView, WrongTarget,
    WrongGeneration, WrongDevice, LateStage, UnknownConvention, WrongInterval,
    UnsupportedRaster, MissingSynchronization, MissingLease, NeedsActivation,
    PreferenceFallback, UnsupportedProvider, IntentionallyDisabled
};
struct Anchor {
    Frame frame{};
    Stage stage=Stage::Unknown;
    Source source=Source::Automatic;
    uint32_t width=0,height=0;
    uint64_t previousScene=0;
};
struct Offer {
    Signal signal=Signal::Depth;
    Source source=Source::Automatic;
    Frame frame{};
    Stage stage=Stage::Unknown;
    uint64_t previous=0,providerGeneration=0;
    uint32_t width=0,height=0,cost=0;
    bool observed=false,conventionsKnown=false,synchronized=false,immutableLease=false;
    bool needsActivation=false;
};
struct Policy {Source depth=Source::Automatic,motion=Source::Automatic;bool strict=false;uint64_t revision=0;Source color=Source::Automatic;};
class PolicyStore {
    std::atomic<uint64_t> value_{0};
public:
    Policy Read()const noexcept {const auto v=value_.load(std::memory_order_acquire);
        return {static_cast<Source>(v&7),static_cast<Source>((v>>3)&7),bool(v&64),v>>10,static_cast<Source>((v>>7)&7)};}
    bool Request(int depth,int motion,bool strict,int color=0)noexcept {
        if(depth<0||depth>5||motion<0||motion>5||color<0||color>5)return false;
        return Publish(depth,motion,strict,color);
    }
    // Keep unknown IDs in Config. The compact active policy uses an unsupported
    // sentinel: ordinary preference falls back; strict pin has no matching offer.
    bool RequestPersisted(int depth,int motion,bool strict,int color=0)noexcept {
        return Publish(depth>=0&&depth<=5?depth:7,motion>=0&&motion<=5?motion:7,strict,color>=0&&color<=5?color:7);
    }
    bool Invalidate()noexcept {
        auto old=value_.load(std::memory_order_relaxed);
        for(;;){if((old>>10)==(UINT64_MAX>>10))return false;
            if(value_.compare_exchange_weak(old,old+1024,std::memory_order_release,std::memory_order_relaxed))return true;}
    }
private:
    bool Publish(int depth,int motion,bool strict,int color)noexcept {
        const uint64_t bits=uint64_t(depth)|(uint64_t(motion)<<3)|(uint64_t(strict)<<6)|(uint64_t(color)<<7);
        auto old=value_.load(std::memory_order_relaxed);
        for(;;){if((old&1023)==bits)return true;if((old>>10)==(UINT64_MAX>>10))return false;
            const auto next=(((old>>10)+1)<<10)|bits;
            if(value_.compare_exchange_weak(old,next,std::memory_order_release,std::memory_order_relaxed))return true;}
    }
};
struct History {Source depth=Source::Automatic,motion=Source::Automatic;uint64_t depthGeneration=0,motionGeneration=0;Source color=Source::Automatic;uint64_t colorGeneration=0;};
struct Selection {
    int depth=-1,motion=-1,color=-1;
    Reason depthReason=Reason::Unavailable,motionReason=Reason::Unavailable,colorReason=Reason::Unavailable;
    bool ready=false,requestMotion=false;
};
inline constexpr std::size_t MaxOffers=16;
constexpr bool Provider(Source s)noexcept{return s>=Source::Native&&s<=Source::External;}
inline Reason Compatible(const Anchor& a,const Offer& o)noexcept {
    if(!a.frame.Known()||!o.frame.Known()||!o.providerGeneration)return Reason::UnknownIdentity;
    if(!Provider(o.source))return Reason::UnsupportedProvider;
    if(o.frame.scope!=a.frame.scope)return Reason::UnknownIdentity;
    if(o.frame.device!=a.frame.device)return Reason::WrongDevice;
    if(o.frame.session!=a.frame.session||o.frame.capture!=a.frame.capture||o.frame.scene!=a.frame.scene)return Reason::WrongFrame;
    if(o.frame.view!=a.frame.view)return Reason::WrongView;
    if(o.frame.target!=a.frame.target)return Reason::WrongTarget;
    if(o.frame.generation!=a.frame.generation)return Reason::WrongGeneration;
    if(a.stage==Stage::Unknown||o.stage==Stage::Unknown||o.stage>a.stage)return Reason::LateStage;
    if(!a.width||!a.height||o.width!=a.width||o.height!=a.height)return Reason::UnsupportedRaster;
    if(!o.conventionsKnown)return Reason::UnknownConvention;
    const auto endpoint=a.frame.scope==IdentityScope::RenderScene?a.frame.scene:a.frame.capture;
    if(o.signal==Signal::Motion&&(!a.previousScene||o.previous!=a.previousScene||o.previous>=endpoint))return Reason::WrongInterval;
    if(o.needsActivation)return Reason::NeedsActivation;
    if(!o.immutableLease)return Reason::MissingLease;
    if(!o.synchronized)return Reason::MissingSynchronization;
    return Reason::Ready;
}
// Policy ranking is shared by completed-queue acquisition and the compositor's
// already-admitted callback-local bindings. This function grants no GPU rights.
// The caller supplies physical compatibility, never UI/status flags.
template<class Check> inline Selection SelectCompatible(const Anchor& a,std::span<const Offer> offers,const Policy& p,const History& h,Check&& check)noexcept {
    Selection out;
    if(offers.size()>MaxOffers||!Provider(a.source))return out;
    for(auto signal:{Signal::Color,Signal::Depth,Signal::Motion}) {
        const auto preference=signal==Signal::Color?p.color:signal==Signal::Depth?p.depth:p.motion;
        if(preference==Source::None&&p.strict){
            (signal==Signal::Color?out.colorReason:signal==Signal::Depth?out.depthReason:out.motionReason)=Reason::IntentionallyDisabled;
            continue;
        }
        const auto incumbent=signal==Signal::Color?h.color:signal==Signal::Depth?h.depth:h.motion;
        const auto incarnation=signal==Signal::Color?h.colorGeneration:signal==Signal::Depth?h.depthGeneration:h.motionGeneration;
        int best=-1;uint64_t bestRank=(std::numeric_limits<uint64_t>::max)();
        Reason requestedReason=Reason::Unavailable;
        for(std::size_t n=0;n<offers.size();++n) {
            const auto& o=offers[n];if(o.signal!=signal)continue;
            const auto reason=check(n);
            if(o.source==preference||(preference==Source::Automatic&&requestedReason==Reason::Unavailable))requestedReason=reason;
            if(reason!=Reason::Ready||(p.strict&&preference!=Source::Automatic&&o.source!=preference))continue;
            // Valid explicit preference wins. Auto does not displace trustworthy
            // inputs for derived ones, or churn between equivalent incumbents.
            const uint64_t rank=(uint64_t(preference!=Source::Automatic&&o.source!=preference)<<60)|
                (uint64_t(!o.observed)<<59)|(uint64_t(o.source!=incumbent||o.providerGeneration!=incarnation)<<58)|
                (uint64_t(o.source!=a.source)<<57)|(uint64_t(o.cost)<<8)|uint64_t(o.source);
            if(rank<bestRank){bestRank=rank;best=static_cast<int>(n);}
        }
        auto& index=signal==Signal::Color?out.color:signal==Signal::Depth?out.depth:out.motion;
        auto& reason=signal==Signal::Color?out.colorReason:signal==Signal::Depth?out.depthReason:out.motionReason;
        index=best;
        reason=best<0?requestedReason:preference!=Source::Automatic&&offers[best].source!=preference?Reason::PreferenceFallback:Reason::Ready;
    }
    // Strict source filtering above already enforces a mandatory request.
    // A soft preference retains an admitted fallback; only genuinely missing
    // motion requests the existing estimator below.
    out.ready=out.color>=0&&out.depth>=0&&out.motion>=0;
    out.requestMotion=out.color>=0&&(p.motion!=Source::None||!p.strict)&&out.depth>=0&&out.motion<0&&(!p.strict||p.motion==Source::Automatic||p.motion==Source::BuiltIn);
    return out;
}
inline Selection Select(const Anchor& a,std::span<const Offer> offers,const Policy& p,const History& h)noexcept {
    if(!a.frame.Known())return {};
    return SelectCompatible(a,offers,p,h,[&](size_t n){return Compatible(a,offers[n]);});
}
// The existing Native compositor has a single callback-local inventory after
// encode/guide/resource admission. It does not assert a RenderScene identity or
// enroll these resources in another callback. Selection is policy only; the
// compositor retains the exact recording, bindings and allocation lifetimes.
inline Selection SelectNativeBindings(const Policy& p,const std::array<Reason,3>& admitted)noexcept {
    Anchor anchor;anchor.source=Source::Native;
    std::array<Offer,3> offers{};
    offers[0].signal=Signal::Color;offers[1].signal=Signal::Depth;offers[2].signal=Signal::Motion;
    for(auto& offer:offers){offer.source=Source::Native;offer.observed=true;}
    return SelectCompatible(anchor,offers,p,{},[&](size_t n){return admitted[n];});
}
inline const char* ReasonText(Reason r)noexcept {
    switch(r){
    case Reason::Ready:return "Ready";
    case Reason::UnknownIdentity:return "Frame identity unavailable";
    case Reason::WrongFrame:return "Wrong frame";
    case Reason::WrongView:return "Wrong view";
    case Reason::WrongTarget:return "Wrong target";
    case Reason::WrongGeneration:return "Source changed";
    case Reason::WrongDevice:return "Incompatible device";
    case Reason::LateStage:return "Unavailable before this stage";
    case Reason::UnknownConvention:return "Unknown convention";
    case Reason::WrongInterval:return "Wrong motion interval";
    case Reason::UnsupportedRaster:return "Unsupported image mapping";
    case Reason::MissingSynchronization:return "Missing synchronization";
    case Reason::MissingLease:return "Resource ownership unavailable";
    case Reason::NeedsActivation:return "Needs activation";
    case Reason::PreferenceFallback:return "Requested source unavailable; fallback active";
    case Reason::UnsupportedProvider:return "Unsupported provider";
    case Reason::IntentionallyDisabled:return "None selected; required NR input withheld";
    default:return "Unavailable";
    }
}
}
