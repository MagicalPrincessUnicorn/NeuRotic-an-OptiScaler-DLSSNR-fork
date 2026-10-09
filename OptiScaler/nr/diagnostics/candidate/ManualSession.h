#pragma once
#include "Sink.h"
#include "Reducer.h"
#include <memory>
#include <mutex>
#include <utility>

namespace DlssNr::CandidateObserver {
class ManualSession {
    friend class ManualRouter;
    friend class ManualLease;
    std::atomic<uint32_t> readers_{0};
    std::unique_ptr<Sink> sink_=std::make_unique<Sink>();
    std::unique_ptr<Reducer> reducer_=std::make_unique<Reducer>();
    uint64_t elapsed_=0;
  public:
    const uint64_t identity;
    explicit ManualSession(uint64_t id):identity(id) {
        sink_->SetDeadline(Sink::Clock()+2000);
        if(id) (void)sink_->Start();
    }
    bool Enabled() const noexcept {return sink_->Enabled();}
    void Close() noexcept {sink_->Close(Reason::Requested);}
    void Advance(uint64_t elapsed) noexcept {
        elapsed_=elapsed;
        if(sink_->Enabled())sink_->AdvanceWindow(elapsed);
        Event events[64]{};
        while(auto count=sink_->Drain(events,64)) {
            for(uint32_t i=0;i<count;i++)if(!reducer_->Apply(events[i]))sink_->Close(Reason::CounterExhausted);
        }
        reducer_->AdvanceTime(elapsed);
    }
    Snapshot Read() const noexcept {
        auto value=reducer_->Snapshot();value.elapsedMs=elapsed_;value.coverage=sink_->Stats();return value;
    }
};
class ManualLease {
    friend class ManualRouter;
    ManualSession* session_=nullptr;
    explicit ManualLease(ManualSession& s) noexcept:session_(&s) {s.readers_.fetch_add(1,std::memory_order_acquire);}
  public:
    ManualLease()=default;
    ManualLease(const ManualLease&)=delete;
    ManualLease& operator=(const ManualLease&)=delete;
    ManualLease(ManualLease&& other) noexcept:session_(std::exchange(other.session_,nullptr)){}
    ManualLease& operator=(ManualLease&& other) noexcept {
        if(this!=&other) {Reset();session_=std::exchange(other.session_,nullptr);}return *this;
    }
    ~ManualLease(){Reset();}
    void Reset() noexcept {if(auto* s=std::exchange(session_,nullptr))s->readers_.fetch_sub(1,std::memory_order_release);}
    explicit operator bool() const noexcept {return session_!=nullptr;}
    bool SessionMatches(uint64_t id) const noexcept {return session_ && session_->identity==id;}
    EmitResult Emit(const Event& value) noexcept {return session_?session_->sink_->TryEmit(value):EmitResult::Disabled;}
    void Nested() noexcept {if(session_)session_->sink_->NoteLoss(Reason::Nested);}
};
class ManualRouter {
    std::mutex mutex_;
    ManualSession* active_=nullptr;
    std::atomic<bool> contended_{false};
  public:
    bool Open(ManualSession& session) noexcept {
        std::lock_guard lock(mutex_);
        if(active_ || !session.Enabled())return false;
        contended_.store(false);active_=&session;return true;
    }
    ManualLease Pin() noexcept {
        std::unique_lock lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()) {contended_.store(true,std::memory_order_relaxed);return {};}
        return active_ && active_->Enabled()?ManualLease(*active_):ManualLease{};
    }
    bool Retire(ManualSession& session) noexcept {
        std::lock_guard lock(mutex_);
        if(active_!=&session || session.Enabled() || session.readers_.load(std::memory_order_acquire))return false;
        if(contended_.exchange(false))session.sink_->NoteInexactLoss(Reason::Contended);
        active_=nullptr;return true;
    }
};
}
