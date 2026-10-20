// SDK-private accounting primitive. No public SDK layouts or legacy paths change.
// The owning SDK must retain this object and every registered boundary until consume succeeds.
// This primitive is NOT the exported capability: only a fully instrumented wrapper may publish one.
#pragma once
#include "ffx_neurotic_checked_closure.h"
#include <array>
#include <atomic>
#include <mutex>
#include <utility>
#include <limits>

namespace ffx::nr {
struct FenceObservation { uint64_t value; int32_t removedReason; };
struct Domain {
    FfxNrDomainEvidenceV1 evidence{};
    FenceObservation (*observe)(void*) = nullptr;
    void* context = nullptr;
};

class CheckedClosure final {
public:
    enum class Kind : unsigned { Root, Child, Callback, List };
    class Token final {
        friend class CheckedClosure;
        CheckedClosure* owner_=nullptr;
        Kind kind_=Kind::Root;
        Token(CheckedClosure* owner,Kind kind):owner_(owner),kind_(kind) {}
    public:
        Token()=default;
        Token(const Token&)=delete;
        Token& operator=(const Token&)=delete;
        Token(Token&& other) noexcept:owner_(std::exchange(other.owner_,nullptr)),kind_(other.kind_) {}
        Token& operator=(Token&& other) noexcept {
            if(this!=&other) { reset(); owner_=std::exchange(other.owner_,nullptr); kind_=other.kind_; } return *this;
        }
        ~Token() { reset(); }
        explicit operator bool() const { return owner_!=nullptr; }
        Token child() { return owner_ ? owner_->descendant(*this,Kind::Child) : Token{}; }
        Token callback() { return owner_ ? owner_->descendant(*this,Kind::Callback) : Token{}; }
        Token borrowList() { return owner_ ? owner_->descendant(*this,Kind::List) : Token{}; }
        void reset() noexcept {
            if(!owner_) return;
            if(kind_==Kind::Callback) { callbackOwner_=nullptr; }
            owner_->counts_[unsigned(kind_)].fetch_sub(1,std::memory_order_release);
            owner_->live_.fetch_sub(1,std::memory_order_release); owner_=nullptr;
        }
    };
    explicit CheckedClosure(FfxNrContextTicketV1 identity):identity_(identity) {}
    CheckedClosure(const CheckedClosure&)=delete;
    CheckedClosure& operator=(const CheckedClosure&)=delete;
    Token enterRoot() {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || closed_ || consumed_ || failure_.load()) return {};
        return increment(Kind::Root);
    }
    // Creation instrumentation calls this once for each shared fence, pool reservation,
    // and historical queue terminal marker. Aliased queues retain separate role records.
    bool addDomain(Domain domain) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock) { coverageLost_.store(true); return false; }
        if(closed_ || inventorySealed_ || domainCount_==domains_.size() ||
           !domain.observe || !domain.context || !domain.evidence.domain_id ||
           !domain.evidence.domain_generation || !domain.evidence.fence_id ||
           !domain.evidence.fence_generation || domain.evidence.required_target==UINT64_MAX) {
            coverageLost_.store(true); return false;
        }
        for(unsigned i=0;i<domainCount_;++i) if(domains_[i].evidence.domain_id==domain.evidence.domain_id) { coverageLost_.store(true); return false; }
        domain.evidence.size=sizeof(FfxNrDomainEvidenceV1); domain.evidence.version=1;
        domains_[domainCount_++]=domain; return true;
    }
    // SDK owner assertion, never an application-provided feature bit.
    void sealInventory() { std::unique_lock lock(mutex_,std::try_to_lock); if(lock && !closed_) inventorySealed_=true; else coverageLost_.store(true); }
    void recordFailure(int32_t result) noexcept { if(result<0) { int32_t expected=0; failure_.compare_exchange_strong(expected,result); } }
    bool failed() const noexcept { return failure_.load()!=0 || coverageLost_.load(); }
    void discardUnenrolledInventory() {
        std::unique_lock lock(mutex_);
        if(closed_ || !prefixFinished()) return;
        domains_={}; domainCount_=0; inventorySealed_=false; failure_=0; coverageLost_=false;
    }
    bool updateDomain(Domain domain) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock) { coverageLost_.store(true); return false; }
        if(frozen_ || !domain.observe || !domain.context || domain.evidence.required_target==UINT64_MAX) {
            coverageLost_.store(true); return false;
        }
        for(unsigned i=0;i<domainCount_;++i) if(domains_[i].evidence.domain_id==domain.evidence.domain_id) {
            auto& previous=domains_[i].evidence;
            if(domain.evidence.required_target<previous.required_target) { coverageLost_.store(true); return false; }
            domain.evidence.size=sizeof(FfxNrDomainEvidenceV1); domain.evidence.version=1;
            domain.evidence.logical_role_bits|=previous.logical_role_bits;
            domains_[i]=domain; return true;
        }
        if(domainCount_==domains_.size()) { coverageLost_.store(true); return false; }
        domain.evidence.size=sizeof(FfxNrDomainEvidenceV1); domain.evidence.version=1;
        domains_[domainCount_++]=domain; return true;
    }
    bool prefixFinished() const {
        return live_.load(std::memory_order_acquire)==0;
    }
    FfxNrStatusV1 validate(const FfxNrDrainHandleV1& handle) const {
        std::unique_lock lock(mutex_,std::try_to_lock); if(!lock) return FFX_NR_BUSY;
        return matches(handle) ? FFX_NR_COMPLETE : FFX_NR_STALE_CONTEXT;
    }
    FfxNrStatusV1 summary(FfxNrDrainSummaryV1& out,FfxNrStatusV1 status) const {
        std::unique_lock lock(mutex_,std::try_to_lock); if(!lock) return FFX_NR_BUSY;
        out={}; out.size=sizeof(out); out.version=1; out.drain=handle_; out.status=status;
        out.evidence_revision=revision_; out.failure_sequence=failure_.load()!=0;
        out.domain_count=domainCount_; out.root_count=counts_[0].load(); out.child_count=counts_[1].load();
        out.active_callback_count=counts_[2].load(); out.outstanding_list_borrow_count=counts_[3].load();
        return status;
    }
    FfxNrDrainHandleV1 begin(uint64_t nonce) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || !nonce || consumed_) return {};
        if(closed_) return nonce==nonce_ ? handle_ : FfxNrDrainHandleV1{};
        closed_=true; nonce_=nonce;
        handle_={identity_,nextDrain_.fetch_add(1),1};
        if(!handle_.drain_id) coverageLost_.store(true);
        ++revision_; return handle_;
    }
    FfxNrStatusV1 poll(const FfxNrDrainHandleV1& handle,int64_t now,int64_t deadline) {
        if(callbackOwner_==this) return FFX_NR_REENTRANT;
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || observing_) return FFX_NR_BUSY;
        if(!matches(handle)) return FFX_NR_STALE_CONTEXT;
        if(removed_) return FFX_NR_DEVICE_REMOVED_RETAINED;
        if(failure_.load()) return FFX_NR_API_FAILURE_RETAINED;
        if(coverageLost_.load() || !inventorySealed_) return FFX_NR_INCOMPLETE_COVERAGE;
        if(now>=deadline) return FFX_NR_TIMEOUT_RETAINED;
        if(counts_[3].load(std::memory_order_acquire)) return FFX_NR_EXTERNAL_RECORDING_OUTSTANDING;
        if(!prefixFinished()) return FFX_NR_PENDING;
        // All ancestors and local children are gone: descendants can no longer publish.
        frozen_=true; observing_=true;
        auto snapshot=domains_; const auto count=domainCount_;
        lock.unlock();
        std::array<FenceObservation,MaxDomains> observations{};
        for(unsigned i=0;i<count;++i) observations[i]=snapshot[i].observe(snapshot[i].context);
        // Publication is frozen; there are no competing mutation operations here.
        if(!lock.try_lock()) { observing_.store(false); return FFX_NR_BUSY; }
        observing_=false;
        bool complete=true;
        for(unsigned i=0;i<count;++i) {
            auto& evidence=domains_[i].evidence; auto observed=observations[i];
            evidence.observed_completed_value=observed.value; evidence.device_removed_reason=observed.removedReason;
            if(observed.value==UINT64_MAX || observed.removedReason<0) removed_=true;
            if(observed.value<evidence.required_target || evidence.last_successful_target<evidence.required_target) complete=false;
        }
        ++revision_;
        if(removed_) return status_=FFX_NR_DEVICE_REMOVED_RETAINED;
        if(failure_.load()) return status_=FFX_NR_API_FAILURE_RETAINED;
        return status_=complete ? FFX_NR_COMPLETE : FFX_NR_PENDING;
    }
    uint64_t revision() const { std::unique_lock lock(mutex_,std::try_to_lock); return lock ? revision_ : 0; }
    FfxNrStatusV1 read(const FfxNrDrainHandleV1& handle,uint64_t revision,uint32_t first,
                      FfxNrDomainEvidenceV1* out,uint32_t capacity,uint32_t& written) const {
        written=0; std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || observing_) return FFX_NR_BUSY;
        if(!matches(handle) || revision!=revision_) return FFX_NR_STALE_CONTEXT;
        if(first>domainCount_ || (!out && capacity)) return FFX_NR_INVALID_ARGUMENT;
        const auto left=domainCount_-first;
        const auto count=capacity<left ? capacity:left;
        for(unsigned i=0;i<count;++i) out[i]=domains_[first+i].evidence;
        written=count; return count<left ? FFX_NR_INCOMPLETE_COVERAGE : FFX_NR_COMPLETE;
    }
    FfxNrStatusV1 validateComplete(const FfxNrDrainHandleV1& handle,uint64_t revision) const {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock) return FFX_NR_BUSY;
        if(!matches(handle) || revision!=revision_ || consumed_) return FFX_NR_STALE_CONTEXT;
        return frozen_ && status_==FFX_NR_COMPLETE && !failure_.load() && !coverageLost_.load() ? FFX_NR_COMPLETE:FFX_NR_PENDING;
    }
    FfxNrStatusV1 consume(const FfxNrDrainHandleV1& handle,uint64_t revision) {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || observing_) return FFX_NR_BUSY;
        if(!matches(handle) || revision!=revision_ || consumed_) return FFX_NR_STALE_CONTEXT;
        if(!frozen_ || status_!=FFX_NR_COMPLETE || failure_.load() || coverageLost_.load()) return FFX_NR_PENDING;
        consumed_=true; return FFX_NR_COMPLETE;
    }
private:
    static constexpr unsigned MaxDomains=256;
    inline static std::atomic<uint64_t> nextDrain_{1};
    inline static thread_local CheckedClosure* callbackOwner_=nullptr;
    FfxNrContextTicketV1 identity_{};
    FfxNrDrainHandleV1 handle_{};
    mutable std::mutex mutex_;
    std::array<std::atomic<uint32_t>,4> counts_{};
    std::atomic<uint64_t> live_{0};
    std::array<Domain,MaxDomains> domains_{};
    unsigned domainCount_=0;
    uint64_t nonce_=0,revision_=1;
    std::atomic<int32_t> failure_{0};
    std::atomic<bool> coverageLost_{false};
    std::atomic<bool> observing_{false};
    bool inventorySealed_=false,closed_=false,frozen_=false,removed_=false,consumed_=false;
    FfxNrStatusV1 status_=FFX_NR_PENDING;
    bool matches(const FfxNrDrainHandleV1& h) const {
        return closed_ && h.drain_id==handle_.drain_id && h.closed_epoch==handle_.closed_epoch &&
            h.context.owner_id==identity_.owner_id && h.context.incarnation==identity_.incarnation &&
            h.context.registration_generation==identity_.registration_generation;
    }
    Token increment(Kind kind) {
        auto& counter=counts_[unsigned(kind)];
        if(counter.fetch_add(1)==UINT32_MAX) { counter.fetch_sub(1); coverageLost_.store(true); return {}; }
        live_.fetch_add(1,std::memory_order_release);
        return Token(this,kind);
    }
    Token descendant(const Token& parent,Kind kind) {
        // A live ancestor prevents freezing. No lock is held across foreign calls.
        if(parent.owner_!=this || parent.kind_==Kind::List) return {};
        if(kind==Kind::Callback && callbackOwner_) { coverageLost_.store(true); return {}; }
        auto result=increment(kind); if(result && kind==Kind::Callback) callbackOwner_=this; return result;
    }
};
} // namespace ffx::nr
