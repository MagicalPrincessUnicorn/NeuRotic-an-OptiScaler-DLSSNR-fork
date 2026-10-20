#pragma once
#include "NrSignalReceipts.h"
#include "VulkanNrRecording.h"
#include "VulkanNrFrameContract.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace DlssNr {
struct VkNrSignalContext {
    VkDevice device=VK_NULL_HANDLE;VkNrFrameContract frame;
    uint64_t applied=0,reset=0;uint32_t productRoute=0;uint64_t policyRevision=0,resetEpoch=0;
};
inline bool SameVkNrSignalContext(const VkNrSignalContext& a,const VkNrSignalContext& b) {
    const auto& x=a.frame;const auto& y=b.frame;
    if(a.device!=b.device||a.applied!=b.applied||a.reset!=b.reset||a.productRoute!=b.productRoute||a.policyRevision!=b.policyRevision||a.resetEpoch!=b.resetEpoch||
       x.route!=y.route||x.placement!=y.placement||x.deviceGeneration!=y.deviceGeneration||
       x.routeEpoch!=y.routeEpoch||x.settingsRevision!=y.settingsRevision||x.resumeGeneration!=y.resumeGeneration||
       x.inputInterruptionEpoch!=y.inputInterruptionEpoch||x.queue!=y.queue||x.queueFamily!=y.queueFamily||
       x.swapchain!=y.swapchain||x.swapchainGeneration!=y.swapchainGeneration||x.guideGeneration!=y.guideGeneration||
       x.output.width!=y.output.width||x.output.height!=y.output.height||x.work.width!=y.work.width||
       x.work.height!=y.work.height||x.representation!=y.representation||
       x.evaluation.featureGeneration!=y.evaluation.featureGeneration||bool(x.temporal)!=bool(y.temporal))return false;
    if(x.temporal){const auto& p=*x.temporal;const auto& q=*y.temporal;
        if(p.color!=q.color||p.depth!=q.depth||p.motion!=q.motion||p.featureFlags!=q.featureFlags||
           p.featureGeneration!=q.featureGeneration||p.providerGeneration!=q.providerGeneration||p.viewport!=q.viewport||
           p.depthInverted!=q.depthInverted||p.providerFrameKnown!=q.providerFrameKnown)return false;}
    // Invocation/acquisition and animated scalar values belong to the sample,
    // not the stable owner context of an older completed sample.
    return true;
}
struct VkNrSignalPayload {
    VkNrSignalContext context;VkNrUseId use;VkNrEvaluationIdentity sourceEvaluation;
    std::array<SignalReceipts::Fact,size_t(SignalReceipts::Signal::Count)> facts{};
    uint32_t recorded=0;uint64_t time=0;bool deliveryRecorded=false,held=false;
};
template<class Owner> class VkNrSignalDiagnostics {
    struct Ticket {
        Owner* owner=nullptr;VkNrUseId use;uint64_t time=0;
        Ticket(Owner& o,VkNrUseId u,uint64_t t):owner(o.RetainUse(u)?&o:nullptr),use(u),time(t){}
        ~Ticket(){if(owner)owner->ReleaseUse(use);}
    };
    struct Pending {VkNrSignalPayload payload;std::shared_ptr<Ticket> ticket;};
    std::array<std::optional<Pending>,32> recorded_{};
    std::optional<VkNrSignalContext> context_;uint64_t contextSerial_=0;
    SignalReceipts::Epoch epoch_{};
    SignalReceipts::Tracker<std::shared_ptr<Ticket>> tracker_;
public:
    void Clear(){recorded_={};tracker_={};context_.reset();++contextSerial_;}
    void Match(const VkNrSignalContext& c){if(!context_||!SameVkNrSignalContext(*context_,c)){
        Clear();context_=c;epoch_={contextSerial_,c.frame.settingsRevision,c.frame.resumeGeneration,
            c.frame.inputInterruptionEpoch,c.applied,c.reset,c.productRoute};}}
    void Store(const VkNrSignalPayload& p,Owner& owner){
        if(!p.use||!p.recorded)return;
        Match(p.context);
        for(auto& slot:recorded_)if(slot&&slot->payload.use==p.use&&
            slot->payload.context.frame.evaluation==p.context.frame.evaluation){slot->payload=p;return;}
        for(auto& slot:recorded_)if(slot&&p.time>=slot->payload.time&&p.time-slot->payload.time>2000)slot.reset();
        auto ticket=std::make_shared<Ticket>(owner,p.use,p.time);if(!ticket->owner)return;
        for(auto& slot:recorded_)if(!slot){slot=Pending{p,std::move(ticket)};return;}
        // Diagnostic eviction releases only observer metadata, never renderer ownership.
        auto oldest=std::min_element(recorded_.begin(),recorded_.end(),[](const auto& a,const auto& b){return a->payload.time<b->payload.time;});
        *oldest=Pending{p,std::move(ticket)};
    }
    void Delivery(VkNrUseId use,const VkNrEvaluationIdentity& evaluation,bool recorded,
        std::optional<SignalReceipts::Fact> actual={}){
        for(auto& p:recorded_)if(p&&p->payload.use==use&&p->payload.context.frame.evaluation==evaluation)
            {p->payload.deliveryRecorded=recorded;
             if(actual)p->payload.facts[size_t(SignalReceipts::Signal::Delivery)]=*actual;}
    }
    bool Select(VkNrUseId use,const VkNrEvaluationIdentity& evaluation,bool accepted,uint64_t now){
        for(auto& p:recorded_)if(p&&p->payload.use==use&&p->payload.context.frame.evaluation==evaluation){
            auto selected=std::move(*p);p.reset();Match(selected.payload.context);
            const auto sequence=tracker_.Begin(epoch_,selected.payload.time);
            for(size_t i=0;i<selected.payload.facts.size();++i)
                tracker_.Observe(sequence,static_cast<SignalReceipts::Signal>(i),selected.payload.facts[i]);
            uint32_t mask=selected.payload.recorded;
            if(accepted&&selected.payload.deliveryRecorded)mask|=SignalReceipts::Bit(SignalReceipts::Signal::Delivery);
            tracker_.Record(sequence,std::move(selected.ticket),mask);
            (void)now;return true;
        }return false;
    }
    void Poll(Owner& owner,uint64_t now){
        for(auto& p:recorded_)if(p&&now>=p->payload.time&&now-p->payload.time>2000)p.reset();
        tracker_.Poll([&](const std::shared_ptr<Ticket>& t){
            if(!t||t->owner!=&owner||now<t->time||now-t->time>2000)return SignalReceipts::Proof{};
            if(owner.GpuComplete(t->use))return SignalReceipts::Proof{true,true,true};
            // No completion proof is not proof of non-submission.
            bool submitted=false;
            if constexpr(requires {owner.ProducerProof(t->use);})submitted=bool(owner.ProducerProof(t->use));
            return SignalReceipts::Proof{!owner.Reusable(t->use),submitted,false};
        });
    }
    SignalReceipts::Reading Read(SignalReceipts::Signal s,uint64_t now){
        auto r=tracker_.Read(s,epoch_,now,SignalReceipts::Mode::Active);
        if(r.state==SignalReceipts::State::Stale)r.fact.reason="Bounded Vulkan diagnostic expired; current completion unknown";
        else if(r.state==SignalReceipts::State::Blocked&&r.fact.reason&&
            std::string_view(r.fact.reason)=="Recording revoked or abandoned")
            {r.state=SignalReceipts::State::Stale;r.fact.reason="Vulkan use completion unavailable; retirement is not consumption proof";}
        return r;
    }
};
inline double VkNrDiagnosticMilliseconds(){
    return std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now().time_since_epoch()).count();
}
inline std::optional<std::array<double,4>> VkNrTimestampStages(std::span<const uint64_t,5> ticks,uint32_t bits,float period,
    std::optional<double> maximumElapsedMs={}){
    if(!bits||bits>64||!std::isfinite(period)||period<=0)return {};
    // A modulo delta cannot distinguish multiple wraps. The CPU interval bounds
    // the actual query span; ambiguous narrow counters remain unavailable.
    if(maximumElapsedMs&&(!std::isfinite(*maximumElapsedMs)||*maximumElapsedMs<0||
        *maximumElapsedMs>=std::ldexp(double(period),int(bits))/1e6))return {};
    const auto mask=bits==64?UINT64_MAX:(uint64_t(1)<<bits)-1;
    std::array<double,4> result{};double total=0;
    for(size_t i=0;i<4;++i){result[i]=double((ticks[i+1]-ticks[i])&mask)*double(period)/1e6;
        if(!std::isfinite(result[i])||result[i]<0||result[i]>1000)return {};total+=result[i];}
    return total<=1000?std::optional(result):std::nullopt;
}
struct VkNrTimingView {
    bool ready=false;uint64_t use=0,lastQueryUse=0,at=0;uint32_t productRoute=0;
    VkNrSignalContext sample,current;std::array<double,4> ms{};
};
struct VkNrTimingAggregate {uint64_t use=0;double milliseconds=0;};
inline std::optional<VkNrTimingAggregate> AggregateVkNrTiming(std::span<const VkNrTimingView> views,uint32_t route,uint64_t now){
    if(route>2||views.empty()||views.size()>10||!views[0].use)return {};
    const auto apiRoute=route?VkNrRoute::Present:VkNrRoute::Native;
    VkNrTimingAggregate out{views[0].use,0};
    for(const auto& v:views){if(!v.ready||v.use!=out.use||v.use!=v.lastQueryUse||v.productRoute!=route||
        v.sample.productRoute!=route||v.current.productRoute!=route||v.sample.frame.route!=apiRoute||v.current.frame.route!=apiRoute||
        now<v.at||now-v.at>2000||!SameVkNrSignalContext(v.sample,v.current))return {};
        for(auto ms:v.ms){if(!std::isfinite(ms)||ms<0||ms>1000)return {};out.milliseconds+=ms;}}
    return out;
}
class VkNrTimingLatch {
    std::optional<VkNrSignalContext> context_;uint64_t use_=0,samples_=0;
public:
    void Clear(){context_.reset();use_=samples_=0;}
    void Match(const VkNrSignalContext& context){
        if(!context_||!SameVkNrSignalContext(*context_,context)){Clear();context_=context;}}
    void Accept(const VkNrTimingAggregate& aggregate,const VkNrSignalContext& context){
        Match(context);
        if(aggregate.use!=use_){use_=aggregate.use;++samples_;}
    }
    uint64_t Samples()const{return samples_;}
};// Completed query scalars only: no retained use, image, submission or transport owner.
// The caller holds g_vkMutex and supplies all current pass contexts in one snapshot.
class VkNrCompletedTimings {
public:
    static constexpr size_t PendingCapacity=32,SeenCapacity=64;
private:
    struct Group {uint64_t use=0,first=0;uint32_t route=0,passes=0;
        std::array<VkNrTimingView,10> stages{};};
    struct Storage {std::array<std::optional<Group>,PendingCapacity> pending{};
        std::array<uint64_t,SeenCapacity> seen{};size_t nextSeen=0;uint64_t discardedThrough=0;};
    std::unique_ptr<Storage> storage_;
    std::array<VkNrSignalContext,10> contexts_{};uint32_t requested_=0;uint64_t samples_=0;
    void Discard(uint64_t use){storage_->discardedThrough=std::max(storage_->discardedThrough,use);}
public:
    void Clear(){storage_.reset();contexts_={};requested_=0;samples_=0;}
    uint64_t Samples()const{return samples_;}
    std::optional<VkNrTimingAggregate> Record(uint32_t pass,uint32_t requested,uint32_t route,
        VkNrTimingView completed,std::span<const VkNrSignalContext> current,uint64_t now){
        if(!requested||requested>10||pass>=requested||current.size()!=requested||route>2||
           !completed.ready||!completed.use||completed.use!=completed.lastQueryUse||
           completed.productRoute!=route||now<completed.at||now-completed.at>2000)return {};
        const auto apiRoute=route?VkNrRoute::Present:VkNrRoute::Native;
        for(const auto& c:current)if(c.productRoute!=route||c.frame.route!=apiRoute)return {};
        completed.current=current[pass];
        if(!SameVkNrSignalContext(completed.sample,completed.current))return {};
        bool changed=requested_!=requested;
        for(uint32_t i=0;!changed&&i<requested;++i)changed=!SameVkNrSignalContext(contexts_[i],current[i]);
        if(changed){Clear();requested_=requested;std::copy(current.begin(),current.end(),contexts_.begin());}
        if(!storage_)storage_=std::make_unique<Storage>();
        auto& storage=*storage_;
        for(auto& item:storage.pending)if(item){
            bool stale=item->route!=route||item->passes!=requested||now<item->first||now-item->first>2000;
            for(uint32_t i=0;!stale&&i<requested;++i)if(item->stages[i].ready&&
                !SameVkNrSignalContext(item->stages[i].sample,current[i]))stale=true;
            if(stale){Discard(item->use);item.reset();}
        }
        if(std::find(storage.seen.begin(),storage.seen.end(),completed.use)!=storage.seen.end())return {};
        auto slot=std::find_if(storage.pending.begin(),storage.pending.end(),[&](const auto& p){return p&&p->use==completed.use;});
        if(slot==storage.pending.end()){
            // Once bounded history is forgotten, an old late set remains unknown;
            // it may not be reconstructed into a new completion count.
            if(completed.use<=storage.discardedThrough)return {};
            slot=std::find_if(storage.pending.begin(),storage.pending.end(),[](const auto& p){return !p;});
            if(slot==storage.pending.end()){
                slot=std::min_element(storage.pending.begin(),storage.pending.end(),[](const auto& a,const auto& b){return a->first<b->first;});
                Discard((*slot)->use);
            }
            *slot=Group{completed.use,completed.at,route,requested,{}};
        }
        auto& group=**slot;
        if(group.stages[pass].ready)return {};
        group.first=std::min(group.first,completed.at);group.stages[pass]=completed;
        std::array<VkNrTimingView,10> views=group.stages;
        for(uint32_t i=0;i<requested;++i)views[i].current=current[i];
        const auto aggregate=AggregateVkNrTiming(std::span(views.data(),requested),route,now);
        if(!aggregate)return {};
        if(storage.seen[storage.nextSeen])Discard(storage.seen[storage.nextSeen]);
        storage.seen[storage.nextSeen]=completed.use;storage.nextSeen=(storage.nextSeen+1)%SeenCapacity;
        ++samples_;slot->reset();return aggregate;
    }
};
}
