#pragma once
#include "Contract.h"
#include <algorithm>

namespace DlssNr::CandidateObserver {
class Reducer {
    struct Address {uintptr_t device=0,resource=0;uint64_t sequence=0,token=0;bool used=false;};
    Summary records_[1024]{};
    Address addresses_[1024]{};
    struct Tombstone {uint64_t sequence=0,epoch=0,token=0,removedAt=0;bool expired=false;};
    Tombstone tombstones_[256]{};
    uint32_t tombstoneNext_=0,tombstoneCount_=0;
    uint32_t count_=0;
    uint64_t last_=0,epoch_=0,token_=0,clock_=0;
    void Remove(uint32_t position,uint64_t removalSequence,bool expired=false) noexcept {
        const auto& old=records_[position];
        tombstones_[tombstoneNext_]={old.sequence,old.epoch,old.token,removalSequence,expired};
        tombstoneNext_=(tombstoneNext_+1)%256;
        if(tombstoneCount_<256)++tombstoneCount_;
        // Correlation with an evicted occurrence is unavailable, never backfilled.
        for(auto& a:addresses_)if(a.used && a.sequence==old.sequence)a={};
        records_[position]=records_[--count_];records_[count_]={};
    }
public:
    bool Apply(const Event& event) noexcept {
        if(event.major!=2 || event.sequence<=last_) return false;
        last_=event.sequence;
        for(uint32_t i=0;i<count_;i++) {
            auto& r=records_[i];
            if(r.retention==Retention::Retained &&
               (event.epoch!=r.epoch || (event.timeKnown && r.timeKnown && event.elapsedMs>=r.elapsedMs &&
                event.elapsedMs-r.elapsedMs>=2000))) r.retention=Retention::Stale;
        }
        epoch_=event.epoch; if(event.timeKnown) clock_=event.elapsedMs;
        for(uint32_t i=0;i<count_;) {
            const auto& r=records_[i];
            if(event.timeKnown && r.timeKnown && event.elapsedMs>=r.elapsedMs && event.elapsedMs-r.elapsedMs>=10000)
                Remove(i,event.sequence,true);
            else ++i;
        }
        if(!Eligible(event.kind)) return true;
        if(token_==UINT64_MAX) return false;
        Summary value{}; value.sequence=event.sequence;value.epoch=event.epoch;value.window=event.window;
        value.elapsedMs=event.elapsedMs;value.timeKnown=event.timeKnown;value.descriptor=event.descriptor;
        value.descriptorKnown=event.descriptorKnown;
        value.kind=event.kind;value.token=++token_;value.depthSupported=event.descriptorKnown && DepthSupport(event.descriptor);
        value.cautions=static_cast<uint8_t>((event.descriptor.depth>1)+(event.descriptor.mips>1));
        Address* match=nullptr;Address* slot=nullptr;
        for(auto& a:addresses_) {
            if(a.used && a.device==event.deviceBits && a.resource==event.resourceBits) {match=&a;break;}
            if(!a.used && !slot) slot=&a;
        }
        if(match) {
            value.predecessor=match->sequence;slot=match;
            for(uint32_t i=0;i<count_;i++) if(records_[i].sequence==match->sequence) records_[i].retention=Retention::Superseded;
        }
        if(!slot) {slot=&addresses_[0];for(auto& a:addresses_) if(a.sequence<slot->sequence) slot=&a;}
        *slot={event.deviceBits,event.resourceBits,event.sequence,value.token,true};
        uint32_t position=count_;
        if(count_==1024) {
            position=0;
            auto priority=[&](const Summary& r) {
                if(event.timeKnown && r.timeKnown && event.elapsedMs>=r.elapsedMs && event.elapsedMs-r.elapsedMs>=10000) return 0;
                if(r.retention==Retention::Superseded) return 1;
                if(r.retention==Retention::Stale) return 2;
                return 3;
            };
            for(uint32_t i=1;i<count_;i++) if(priority(records_[i])<priority(records_[position]) ||
                (priority(records_[i])==priority(records_[position]) && records_[i].sequence<records_[position].sequence)) position=i;
            Remove(position,event.sequence);position=count_;
        }
        ++count_;
        records_[position]=value; return true;
    }
    uint32_t Count() const noexcept {return count_;}
    uint32_t TombstoneCount() const noexcept {return tombstoneCount_;}
    void AdvanceTime(uint64_t elapsed) noexcept {
        if(elapsed<clock_)return;
        clock_=elapsed;
        for(uint32_t i=0;i<count_;) {
            auto& r=records_[i];
            if(r.timeKnown && elapsed>=r.elapsedMs && elapsed-r.elapsedMs>=10000)Remove(i,last_,true);
            else {
                if(r.timeKnown && elapsed>=r.elapsedMs && elapsed-r.elapsedMs>=2000 && r.retention==Retention::Retained)
                    r.retention=Retention::Stale;
                ++i;
            }
        }
    }
    Snapshot Snapshot() const noexcept {
        CandidateObserver::Snapshot s{};s.publication=last_;s.elapsedMs=clock_;
        uint32_t indices[1024]{};for(uint32_t i=0;i<count_;i++) indices[i]=i;
        std::sort(indices,indices+count_,[&](uint32_t a,uint32_t b){
            const auto& x=records_[a];const auto& y=records_[b];
            if(x.depthSupported!=y.depthSupported) return x.depthSupported;
            if(x.cautions!=y.cautions) return x.cautions<y.cautions;
            return x.sequence>y.sequence;
        });
        s.count=std::min(count_,64u);for(uint32_t i=0;i<s.count;i++) s.candidates[i]=records_[indices[i]];
        return s;
    }
    uint64_t Token(uint64_t sequence) const noexcept {
        for(uint32_t i=0;i<count_;i++) if(records_[i].sequence==sequence) return records_[i].token;
        return 0;
    }
    uint64_t Predecessor(uint64_t sequence) const noexcept {
        for(uint32_t i=0;i<count_;i++) if(records_[i].sequence==sequence) return records_[i].predecessor;
        return 0;
    }
    void ClearPrivate() noexcept {for(auto& a:addresses_) a={};}
};
static_assert(sizeof(Reducer)<3*1024*1024);
}
