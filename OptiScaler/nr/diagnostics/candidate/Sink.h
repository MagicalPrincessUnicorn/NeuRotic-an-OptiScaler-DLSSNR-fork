#pragma once
#include "Contract.h"
#include <array>
#include <algorithm>
#include <chrono>

namespace DlssNr::CandidateObserver {
#ifdef NR_CANDIDATE_MANUAL_TEST
void EmitLockedForTest(const Event&) noexcept;
#endif
class Sink {
    struct Quota { uintptr_t device=0, resource=0; uint32_t count=0; bool used=false; };
    struct Frame { uint64_t ordinal=0; uint32_t count=0; bool used=false; Quota quota[64]{}; };
    struct Stream { uint32_t contract=0; uint64_t scope=0,generation=0,high=0; bool used=false; Frame frames[4]{}; };
    std::atomic_flag lock_=ATOMIC_FLAG_INIT;
    std::atomic<uint32_t> state_{0}, loss_{0}, highWater_{0}, reason_{0};
    std::atomic<bool> discontinuity_{false}, dropExact_{true};
    std::atomic<uint64_t> deadline_{0};
    std::atomic<uint64_t> dropped_{0}, publishedEnqueued_{0}, publishedSequence_{0}, publishedEpoch_{1};
    std::atomic<uint64_t> familyEnqueued_[4]{},familyDropped_[4]{};
    std::atomic<bool> familyInexact_[4]{};
    std::atomic<uint64_t> budgetRefused_[BudgetCauseCount]{};
    std::atomic<bool> budgetInexact_[BudgetCauseCount]{};
    Event ring_[4096]{};
    Quota windowQuota_[1024]{};
    Stream streams_[16]{};
    uint64_t sequence_=0,epoch_=1,window_=1,elapsed_=0;
    uint32_t head_=0,tail_=0,count_=0, dataCount_=0,controlCount_=0,windowData_=0,windowControl_=0;
    struct Unlock { std::atomic_flag& l; ~Unlock(){l.clear(std::memory_order_release);} };
    void Loss(Reason reason,uint16_t source=0) noexcept {
        loss_.fetch_or(1u << static_cast<uint32_t>(reason), std::memory_order_relaxed);
        discontinuity_.store(true,std::memory_order_release);
        auto n=dropped_.load(std::memory_order_relaxed);
        if(n==UINT64_MAX || !dropped_.compare_exchange_strong(n,n+1,std::memory_order_relaxed))
            dropExact_.store(false,std::memory_order_relaxed);
        auto family=source<=3?source:0;
        n=familyDropped_[family].load(std::memory_order_relaxed);
        if(n==UINT64_MAX || !familyDropped_[family].compare_exchange_strong(n,n+1,std::memory_order_relaxed))
            familyInexact_[family].store(true,std::memory_order_relaxed);
    }
    EmitResult BudgetLoss(BudgetCause cause,uint16_t source) noexcept {
        const auto index=static_cast<uint32_t>(cause);
        auto count=budgetRefused_[index].load(std::memory_order_relaxed);
        if(count==UINT64_MAX || !budgetRefused_[index].compare_exchange_strong(count,count+1,std::memory_order_relaxed))
            budgetInexact_[index].store(true,std::memory_order_relaxed);
        Loss(Reason::Budget,source);
        return EmitResult::BudgetRefused;
    }
    static uint32_t Bucket(uintptr_t d,uintptr_t r,uint32_t buckets) noexcept {
        uint64_t h=static_cast<uint64_t>(d)^((static_cast<uint64_t>(r)>>4)*0x9e3779b97f4a7c15ULL);
        return static_cast<uint32_t>((h^(h>>32))%buckets)*4;
    }
    static Quota* Find(Quota* q,uint32_t buckets,uintptr_t d,uintptr_t r) noexcept {
        auto b=Bucket(d,r,buckets); Quota* empty=nullptr;
        for(uint32_t i=0;i<4;i++) {
            if(q[b+i].used && q[b+i].device==d && q[b+i].resource==r) return &q[b+i];
            if(!q[b+i].used && !empty) empty=&q[b+i];
        }
        return empty;
    }
public:
    static uint64_t Clock() noexcept {
        return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count());
    }
    void SetDeadline(uint64_t deadline) noexcept {deadline_.store(deadline,std::memory_order_release);}
    void NoteInexactLoss(Reason reason) noexcept {
        Loss(reason);dropExact_.store(false);familyInexact_[0].store(true);
    }
    bool Start(uint64_t initialSequence=0,uint64_t initialEpoch=1) noexcept {
        uint32_t expected=0;
        if(initialEpoch==0 || !state_.compare_exchange_strong(expected,1)) return false;
        sequence_=initialSequence; epoch_=initialEpoch;
        publishedSequence_.store(sequence_); publishedEpoch_.store(epoch_);
        expected=1;return state_.compare_exchange_strong(expected,2,std::memory_order_release);
    }
    bool Enabled() const noexcept {
        const auto deadline=deadline_.load(std::memory_order_acquire);
        return state_.load(std::memory_order_acquire)==2 && (!deadline || Clock()<deadline);
    }
    void Close(Reason reason) noexcept {
        const auto code=static_cast<uint32_t>(reason);
        if(reason==Reason::WriterFailure || reason==Reason::FileLimit || reason==Reason::CounterExhausted)
            reason_.store(code,std::memory_order_relaxed);
        else {uint32_t expected=0;(void)reason_.compare_exchange_strong(expected,code,std::memory_order_relaxed);}
        state_.store(3,std::memory_order_release);
    }
    EmitResult TryEmit(Event value) noexcept {
        auto state=state_.load(std::memory_order_acquire);
        if(state!=2) return state>=3?EmitResult::Closing:EmitResult::Disabled;
        if(lock_.test_and_set(std::memory_order_acquire)) { Loss(Reason::Contended,value.source); return EmitResult::Contended; }
        Unlock unlock{lock_};
        if(!Enabled()) return EmitResult::Closing;
#ifdef NR_CANDIDATE_MANUAL_TEST
        EmitLockedForTest(value);
#endif
        if(discontinuity_.exchange(false,std::memory_order_acq_rel)) {
            if(epoch_==UINT64_MAX) { Close(Reason::CounterExhausted); return EmitResult::CounterExhausted; }
            ++epoch_; publishedEpoch_.store(epoch_,std::memory_order_relaxed);
        }
        if(value.major!=2 || value.minor!=0 || value.kind>Kind::End) {
            Loss(Reason::Unsupported,value.source); return EmitResult::Unsupported;
        }
        const bool control=Control(value.kind);
        // First matching limit wins, including when session and window limits coincide.
        if((control && (controlCount_>=32 || windowControl_>=32)) ||
           (!control && (dataCount_>=65504 || windowData_>=512))) {
            const auto cause=control ? (controlCount_>=32?BudgetCause::SessionControl:BudgetCause::WindowControl) :
                                      (dataCount_>=65504?BudgetCause::SessionData:BudgetCause::WindowData);
            const auto result=BudgetLoss(cause,value.source);
            // Preserve the original cross-family closure policy on these refusals.
            if(dataCount_>=65504 || controlCount_>=32)Close(Reason::Budget);
            return result;
        }
        Quota *w=nullptr,*f=nullptr; Frame* frame=nullptr;
        if(!control) {
            w=Find(windowQuota_,256,value.deviceBits,value.resourceBits);
            if(!w)return BudgetLoss(BudgetCause::WindowQuotaSlots,value.source);
            if(w->count>=8)return BudgetLoss(BudgetCause::ResourceWindow,value.source);
            if(value.frameKnown) {
                if(value.frameContract==0) { Loss(Reason::Unsupported,value.source); return EmitResult::Unsupported; }
                Stream* stream=nullptr; Stream* empty=nullptr;
                for(auto& s:streams_) {
                    if(s.used && s.contract==value.frameContract && s.scope==value.frameScope &&
                       s.generation==value.frameGeneration) {stream=&s;break;}
                    if(!s.used && !empty) empty=&s;
                }
                if(!stream) {
                    if(!empty)return BudgetLoss(BudgetCause::FrameStreams,value.source);
                    stream=empty; stream->used=true; stream->contract=value.frameContract;
                    stream->scope=value.frameScope; stream->generation=value.frameGeneration;
                    stream->high=value.frameOrdinal;
                }
                for(auto& item:stream->frames) if(item.used && item.ordinal==value.frameOrdinal) {frame=&item;break;}
                if(!frame) {
                    bool any=false; for(auto& item:stream->frames) any|=item.used;
                    if(any && value.frameOrdinal<=stream->high)return BudgetLoss(BudgetCause::FrameOrder,value.source);
                    frame=&stream->frames[0];
                    for(auto& item:stream->frames) if(!item.used || item.ordinal<frame->ordinal) frame=&item;
                    *frame={}; frame->used=true; frame->ordinal=value.frameOrdinal; stream->high=value.frameOrdinal;
                }
                f=Find(frame->quota,16,value.deviceBits,value.resourceBits);
                if(frame->count>=512)return BudgetLoss(BudgetCause::FrameData,value.source);
                if(!f)return BudgetLoss(BudgetCause::FrameQuotaSlots,value.source);
                if(f->count>=8)return BudgetLoss(BudgetCause::ResourceFrame,value.source);
            }
        }
        if(count_>=(control?4096u:4032u)) {Loss(Reason::QueueFull,value.source);return EmitResult::QueueFull;}
        if(sequence_==UINT64_MAX) {Close(Reason::CounterExhausted);return EmitResult::CounterExhausted;}
        if(value.kind==Kind::Boundary) {
            if(epoch_==UINT64_MAX) {Close(Reason::CounterExhausted);return EmitResult::CounterExhausted;}
            ++epoch_; publishedEpoch_.store(epoch_,std::memory_order_relaxed);
        }
        value.sequence=++sequence_; value.epoch=epoch_; value.window=window_;
        value.elapsedMs=elapsed_; value.timeKnown=true;
        ring_[tail_]=value; tail_=(tail_+1)%4096; ++count_;
        if(control) {++controlCount_;++windowControl_;}
        else {
            ++dataCount_;++windowData_;
            if(!w->used) {w->used=true;w->device=value.deviceBits;w->resource=value.resourceBits;}
            ++w->count;
            if(f) {if(!f->used){f->used=true;f->device=value.deviceBits;f->resource=value.resourceBits;} ++f->count;++frame->count;}
        }
        publishedEnqueued_.fetch_add(1,std::memory_order_relaxed);
        familyEnqueued_[value.source<=3?value.source:0].fetch_add(1,std::memory_order_relaxed);
        publishedSequence_.store(sequence_,std::memory_order_relaxed);
        if(count_>highWater_.load(std::memory_order_relaxed)) highWater_.store(count_,std::memory_order_relaxed);
        return EmitResult::Recorded;
    }
    uint32_t Drain(Event* output,uint32_t capacity) noexcept {
        if(!output || !capacity || lock_.test_and_set(std::memory_order_acquire)) return 0;
        Unlock unlock{lock_}; auto n=std::min({capacity,count_,64u});
        for(uint32_t i=0;i<n;i++) {output[i]=ring_[head_];ring_[head_]={};head_=(head_+1)%4096;}
        count_-=n; return n;
    }
    bool EmptyAfterClose() noexcept {
        if(Enabled() || lock_.test_and_set(std::memory_order_acquire)) return false;
        Unlock unlock{lock_}; return count_==0;
    }
    void AdvanceWindow(uint64_t elapsed) noexcept {
        if(lock_.test_and_set(std::memory_order_acquire)) return;
        Unlock unlock{lock_};
        if(elapsed<elapsed_ || window_==UINT64_MAX) {Close(Reason::CounterExhausted);return;}
        if(elapsed-elapsed_<16) return;
        elapsed_=elapsed; ++window_; windowData_=windowControl_=0;
        for(auto& q:windowQuota_) q={};
    }
    CaptureStats Stats() const noexcept {
        CaptureStats s{};s.enqueued=publishedEnqueued_.load();s.dropped=dropped_.load();
        s.sequence=publishedSequence_.load();s.epoch=publishedEpoch_.load();s.lossMask=loss_.load();
        s.highWater=highWater_.load();s.dropExact=dropExact_.load();
        for(unsigned i=0;i<4;i++){s.familyEnqueued[i]=familyEnqueued_[i].load();s.familyDropped[i]=familyDropped_[i].load();s.familyDropExact[i]=!familyInexact_[i].load();}
        for(uint32_t i=0;i<BudgetCauseCount;i++){s.budgetRefused[i]=budgetRefused_[i].load();s.budgetRefusalExact[i]=!budgetInexact_[i].load();}
        s.reason=static_cast<Reason>(reason_.load());
        // Re-read sticky loss after all counters/latches: another producer may
        // have rotated the discontinuity epoch during this statistics read.
        const auto pending=discontinuity_.load();s.lossMask=loss_.load();
        s.complete=s.lossMask==0 && !pending && s.dropped==0 && s.dropExact &&
            s.reason!=Reason::WriterFailure && s.reason!=Reason::FileLimit && s.reason!=Reason::CounterExhausted;
        for(unsigned i=0;i<4;i++)s.complete=s.complete && s.familyDropExact[i] && s.familyDropped[i]==0;
        for(uint32_t i=0;i<BudgetCauseCount;i++)s.complete=s.complete && s.budgetRefusalExact[i] && s.budgetRefused[i]==0;
        return s;
    }
    void NoteLoss(Reason reason) noexcept { Loss(reason); }
};
static_assert(sizeof(Sink)<4*1024*1024);
}
