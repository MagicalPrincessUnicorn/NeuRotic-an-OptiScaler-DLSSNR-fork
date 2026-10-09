#pragma once
#include "CharacterObservation.h"
namespace Neurotic::Semantic::Character {
constexpr std::uint64_t ResultBudgetNs=500'000'000;
constexpr std::uint64_t HandoffWindowNs=100'000'000;
inline std::uint64_t BoxLifetimeNs(unsigned holdMs,bool missed,bool smart,bool awaiting=false) noexcept {
    if(!holdMs)return 0;
    const auto configured=static_cast<std::uint64_t>(std::min(holdMs,2000u))*1'000'000;
    if(smart&&missed)return HandoffWindowNs;
    return smart&&awaiting?std::max(configured,HandoffWindowNs):configured;
}
struct HeldSnapshot {
    Snapshot geometry;
    std::array<FrameKey,MaxInstances> sources{};
    std::array<std::uint64_t,MaxInstances> receivedNs{};
    std::array<bool,MaxInstances> missed{};
    bool awaitingReplacement=false;
};
inline std::uint64_t MotionDisplayLifetimeNs(const DisplayContext& display) noexcept {
    const auto interval=std::min<std::uint64_t>(display.realFrameIntervalNs,83'333'333);
    return display.fgActive&&interval?std::max<std::uint64_t>(100'000'000,interval*3):100'000'000;
}
inline std::uint64_t DisplayHoldNs(const DisplayContext& display,unsigned holdMs,bool missed,bool smart,bool awaiting) noexcept {
    const auto base=BoxLifetimeNs(holdMs,missed,smart,awaiting);
    if(!base||!display.fgActive||!display.realFrameIntervalNs||missed)return base;
    return std::max(base,std::min<std::uint64_t>(display.realFrameIntervalNs,125'000'000)*2);
}
inline Admission AdmitHeld(const HeldSnapshot& value,const DisplayContext& display,unsigned holdMs,bool smart=false) noexcept {
    const auto& s=value.geometry;
    if(!display.enabled)return Admission::Disabled;
    if(s.origin!=Origin::LiveModel)return Admission::NotLive;
    if(s.key.epoch!=display.epoch)return Admission::WrongEpoch;
    if(!s.key.sequence||!ValidSnapshot(s))return Admission::Malformed;
    if(s.key.sequence>display.frameSequence||s.key.captureNs>display.nowNs)return Admission::Future;
    if(display.fgActive&&!display.postFgQualified)return Admission::UnqualifiedFg;
    for(std::size_t i=0;i<s.count;++i){
        const auto holdNs=DisplayHoldNs(display,holdMs,value.missed[i],smart,value.awaitingReplacement);
        const auto& source=value.sources[i];const auto received=value.receivedNs[i];
        if(source.epoch!=display.epoch)return Admission::WrongEpoch;
        if(!source.sequence||source.sequence>display.frameSequence||source.captureNs>received||received>display.nowNs)return Admission::Future;
        if(!received||received-source.captureNs>ResultBudgetNs)return Admission::Stale;
        const auto& item=s.items[i];
        if(item.motionTracked&&(display.nowNs-source.captureNs>MotionDisplayLifetimeNs(display)||
            display.nowNs-item.confirmedNs>350'000'000))return Admission::Stale;
        if(holdNs ? display.nowNs-received>holdNs : display.nowNs-source.captureNs>DisplayTtlNs)return Admission::Stale;
    }
    return Admission::Ready;
}
class BoxHold {
    struct Entry {bool used=false;TrackItem item;FrameKey source;std::uint64_t received=0;bool missed=false;};
    std::array<Entry,MaxInstances> entries_{};
    FrameKey last_{};
public:
    void Clear() noexcept {entries_={};last_={};}
    bool Update(const Snapshot& sample,std::uint64_t receivedNs,unsigned holdMs,bool smart=false,bool authoritativeMotion=false) noexcept {
        if(!ValidSnapshot(sample)||sample.origin!=Origin::LiveModel||!sample.key.sequence||sample.key.captureNs>receivedNs||
            receivedNs-sample.key.captureNs>(holdMs?ResultBudgetNs:DisplayTtlNs))return false;
        if(last_.sequence&&last_.epoch==sample.key.epoch&&
            (sample.key.sequence<=last_.sequence||sample.key.captureNs<=last_.captureNs))return false;
        if(last_.epoch!=sample.key.epoch)Clear();
        for(auto& e:entries_) {
            // Keep potential replacements eligible until the bounded handoff
            // deadline. A miss never changes the last positive receipt.
            const auto holdNs=BoxLifetimeNs(holdMs,e.missed,smart,true);
            if(e.used&&(receivedNs<e.received||
                (holdNs?receivedNs-e.received>holdNs:receivedNs-e.source.captureNs>DisplayTtlNs)))e.used=false;
        }
        // A successful motion tick is authoritative, including zero tracks after
        // an occlusion/cut. Never resurrect its previous boxes through static hold.
        if(!holdMs||authoritativeMotion)entries_={};
        std::array<bool,MaxInstances> updated{};
        for(std::size_t i=0;i<sample.count;++i){
            const auto& item=sample.items[i];std::size_t selected=MaxInstances;
            for(std::size_t j=0;j<MaxInstances;++j)if(entries_[j].used&&!updated[j]&&entries_[j].item.id==item.id){selected=j;break;}
            // Association can issue a new ID after a gap. Replace an overlapping
            // old box rather than draw a second copy of the same observation.
            if(selected==MaxInstances)for(std::size_t j=0;j<MaxInstances;++j)
                if(entries_[j].used&&!updated[j]&&entries_[j].item.classId==item.classId&&IoU(entries_[j].item.body,item.body)>.6){selected=j;break;}
            if(selected==MaxInstances)for(std::size_t j=0;j<MaxInstances;++j)if(!entries_[j].used){selected=j;break;}
            if(selected==MaxInstances)for(std::size_t j=0;j<MaxInstances;++j)if(!updated[j]&&
                (selected==MaxInstances||entries_[j].received<entries_[selected].received))selected=j;
            if(selected==MaxInstances)return false;
            entries_[selected]={true,item,sample.key,receivedNs,false};updated[selected]=true;
        }
        for(std::size_t j=0;j<MaxInstances;++j)if(entries_[j].used&&!updated[j])entries_[j].missed=true;
        last_=sample.key;
        return true;
    }
    bool Read(const DisplayContext& display,unsigned holdMs,HeldSnapshot& result,unsigned maximumPersons=MaxInstances,bool smart=false,bool awaitingReplacement=false) const noexcept {
        result={};result.geometry.origin=Origin::LiveModel;result.geometry.key=last_;result.awaitingReplacement=awaitingReplacement;
        std::array<const Entry*,MaxInstances> eligible{};std::size_t count=0;
        for(const auto& e:entries_){const auto holdNs=DisplayHoldNs(display,holdMs,e.missed,smart,awaitingReplacement);
            if(e.used&&e.item.motionTracked&&(display.nowNs<e.source.captureNs||display.nowNs<e.item.confirmedNs||
                display.nowNs-e.source.captureNs>MotionDisplayLifetimeNs(display)||display.nowNs-e.item.confirmedNs>350'000'000))continue;
            if(e.used&&e.received<=display.nowNs&&
            (holdNs?display.nowNs-e.received<=holdNs:display.nowNs-e.source.captureNs<=DisplayTtlNs)){
            // Fixed-size insertion keeps the newest observations first.
            auto pos=count;while(pos&&eligible[pos-1]->received<e.received){eligible[pos]=eligible[pos-1];--pos;}
            eligible[pos]=&e;++count;
        }}
        const auto limit=std::min<std::size_t>(count,std::clamp(maximumPersons,1u,static_cast<unsigned>(MaxInstances)));
        for(std::size_t i=0;i<limit;++i){const auto& e=*eligible[i];result.geometry.items[i]=e.item;result.sources[i]=e.source;result.receivedNs[i]=e.received;result.missed[i]=e.missed;}
        result.geometry.count=limit;
        return result.geometry.count&&AdmitHeld(result,display,holdMs,smart)==Admission::Ready;
    }
};
inline Snapshot PredictTrackedGeometry(Snapshot snapshot,std::uint64_t nowNs,bool predictMotion=true) noexcept {
    if(nowNs<snapshot.key.captureNs)return snapshot;
    const auto seconds=predictMotion?static_cast<double>(std::min<std::uint64_t>(nowNs-snapshot.key.captureNs,20'000'000))/1e9:0.;
    for(std::size_t i=0;i<snapshot.count;++i){auto& item=snapshot.items[i];if(!item.motionTracked)continue;
        const auto dx=std::clamp(item.velocity.x*seconds,-.04,.04),dy=std::clamp(item.velocity.y*seconds,-.04,.04);
        Rect moved{std::clamp(item.body.x0+dx,0.,1.),std::clamp(item.body.y0+dy,0.,1.),
            std::clamp(item.body.x1+dx,0.,1.),std::clamp(item.body.y1+dy,0.,1.)};
        if(ValidRect(moved)){item.body=moved;for(auto& p:item.pose){p.position.x+=dx;p.position.y+=dy;}}
        if(item.hasPose&&nowNs-item.poseNs>250'000'000)item.hasPose=false;
    }return snapshot;
}
}
