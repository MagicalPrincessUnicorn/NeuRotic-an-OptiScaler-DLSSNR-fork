// Creation and scheduler identity owned by the SDK, outside frozen SDK layouts.
#pragma once
#include "CheckedClosure.h"
#include "CheckedOwnedOutput.h"
#include "CheckedAlgorithm.h"
#include <memory>
#include <unordered_map>

namespace ffx::nr {
inline std::atomic<bool> checkedModeRequested{false};
inline thread_local bool registryLookupBusy=false;
struct CreatedContext final {
    const FfxNrContextTicketV1 identity;
    const uint64_t provider;
    void* deviceIdentity=nullptr;
    CheckedClosure closure;
    std::mutex lifecycleMutex;
    std::atomic<bool> destroying{false};
    std::atomic<bool> quarantined{false};
    std::atomic<bool> boundedWorkers{false};
    std::atomic<uint64_t> workerWakeEpoch{0};
    std::array<std::atomic<uint64_t>,2> workerAcknowledged{};
    std::atomic<bool> enrolled{false};
    std::atomic<bool> enrolling{false};
    std::atomic<uint32_t> legacyRoots{0};
    void* object=nullptr;
    FfxNrStatusV1 (*enrollOwner)(void*)=nullptr;
    FfxNrStatusV1 (*stopWorkers)(void*)=nullptr;
    void (*retainPin)(void*)=nullptr;
    void (*releasePin)(void*)=nullptr;
    std::atomic<bool> destroyReady{false};
    FfxNrSubmissionObserverV1 observer{};
    std::atomic<uint64_t> nextDispatch{1};
    std::atomic<uint64_t> nextRecording{1};
    std::atomic<bool> rawListEscaped{false};
    std::atomic<bool> rawBufferEscaped{false},writerActive{false},outputReady{false},outputConsumed{false};
    std::atomic<uint32_t> enrollmentFlags{0};
    std::shared_ptr<OwnedOutputLease> ownedOutput;
    std::shared_ptr<AlgorithmLease> algorithm;
    FfxNrStatusV1 (*writeOwnedOutput)(void*,const OwnedWriterCallbacks&,void*,OwnedWriteReceipt&)=nullptr;
    FfxNrStatusV1 (*finishWriter)(void*,OwnedWriterRetirementReceipt&)=nullptr;
    void* ownedWriterCommands=nullptr;
    OwnedWriterRetirementReceipt writerRetirement{};
    std::atomic<bool> workerShutdownRequested{false};
    std::atomic<bool> polling{false};
    std::array<void*,3> queues{};
    std::array<void*,3> queueMarkers{};
    bool markersPublished=false;
    std::array<std::atomic<void*>,2> workerHandles{};
    std::array<void*,2> workerObservations{};
    explicit CreatedContext(FfxNrContextTicketV1 id,uint64_t providerId):identity(id),provider(providerId),closure(id) {}
    // Called under the existing scheduled-frame lock, so copying a queued PacingData
    // and moving its token happen in the same critical section.
    bool publish(unsigned slot,CheckedClosure::Token token) {
        if(slot>=jobs_.size() || !token) { closure.recordFailure(-1); return false; }
        std::unique_lock lock(jobsMutex_,std::try_to_lock);
        if(!lock || jobs_[slot]) { closure.recordFailure(-1); return false; }
        jobs_[slot]=std::move(token); return true;
    }
    CheckedClosure::Token take(unsigned slot) {
        if(slot>=jobs_.size()) { closure.recordFailure(-1); return {}; }
        std::unique_lock lock(jobsMutex_,std::try_to_lock);
        if(!lock) { closure.recordFailure(-1); return {}; }
        return std::move(jobs_[slot]);
    }
private:
    std::mutex jobsMutex_;
    std::array<CheckedClosure::Token,2> jobs_{};
};
// Wrapper destruction and enrollment share this claim before accessing the raw owner.
class DestroyScope final {
    std::shared_ptr<CreatedContext> context_;
    std::unique_lock<std::mutex> lock_;
    bool allowed_=true;
public:
    explicit DestroyScope(std::shared_ptr<CreatedContext> context):context_(std::move(context)) {
        if(!context_) return;
        lock_=std::unique_lock<std::mutex>(context_->lifecycleMutex,std::try_to_lock);
        if(!lock_ || context_->destroying.load() || context_->enrolling.load() || context_->quarantined.load() ||
           (context_->algorithm && !context_->algorithm->inspect().released) ||
           (context_->enrolled.load() && !context_->destroyReady.load())) { allowed_=false; return; }
        context_->destroying=true;
    }
    explicit operator bool() const { return allowed_; }
};
class CreatedRegistry final {
    mutable std::mutex mutex_;
    std::unordered_map<void*,std::shared_ptr<CreatedContext>> identities_;
    inline static std::atomic<uint64_t> next_{1};
public:
    std::shared_ptr<CreatedContext> create(void* exact,void* alias,uint64_t provider) {
        if(!exact || !alias || !provider) return {};
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock || identities_.count(exact) || identities_.count(alias)) return {};
        const auto id=next_.fetch_add(1); if(!id) return {};
        auto state=std::make_shared<CreatedContext>(FfxNrContextTicketV1{id,id,1},provider);
        state->object=exact;
        identities_.emplace(exact,state); identities_.emplace(alias,state); return state;
    }
    // Lookup compares registry keys only; it never dereferences a caller's pointer.
    std::shared_ptr<CreatedContext> find(void* exact) const {
        std::unique_lock lock(mutex_,std::try_to_lock); registryLookupBusy=!lock; if(!lock) return {};
        const auto at=identities_.find(exact); return at==identities_.end()?nullptr:at->second;
    }
    std::shared_ptr<CreatedContext> findForDestruction(void* exact) const {
        std::unique_lock lock(mutex_);
        const auto at=identities_.find(exact); return at==identities_.end()?nullptr:at->second;
    }
    bool bindAlias(void* exact,void* alias) {
        if(!alias) return false;
        std::unique_lock lock(mutex_,std::try_to_lock); if(!lock) return false;
        auto at=identities_.find(exact); if(at==identities_.end() || identities_.count(alias)) return false;
        identities_.emplace(alias,at->second); return true;
    }
    void unbindAlias(void* alias) {
        std::unique_lock lock(mutex_);
        auto at=identities_.find(alias);
        if(at!=identities_.end() && at->second->object!=alias) identities_.erase(at);
    }
    std::shared_ptr<CreatedContext> find(FfxNrContextTicketV1 ticket) const {
        std::unique_lock lock(mutex_,std::try_to_lock); if(!lock) return {};
        for(const auto& entry:identities_) {
            const auto id=entry.second->identity;
            if(id.owner_id==ticket.owner_id && id.incarnation==ticket.incarnation && id.registration_generation==ticket.registration_generation) return entry.second;
        }
        return {};
    }
    bool remove(void* exact) {
        std::unique_lock lock(mutex_);
        auto at=identities_.find(exact); if(at==identities_.end()) return false;
        auto context=at->second;
        // Enrolled owners remain pinned by their checked lifecycle until consumed.
        if(context->enrolled.load() || context->quarantined.load()) return false;
        for(auto it=identities_.begin();it!=identities_.end();) {
            if(it->second==context) it=identities_.erase(it); else ++it;
        }
        return true;
    }
};
inline CreatedRegistry& createdRegistry() { static auto* registry=new CreatedRegistry; return *registry; }
inline void registerCreated(void* exact,void* alias,uint64_t provider) noexcept {
    try { createdRegistry().create(exact,alias,provider); } catch(...) { /* no checked enrollment is available */ }
}
}
