#pragma once
#include "Contract.h"
#include "ManualSession.h"

namespace DlssNr::CandidateObserver {
bool Enabled() noexcept;
EmitResult TryEmit(const Event&) noexcept;
ReadResult TryRead(Snapshot&) noexcept;
ImportResult TryObserveOwnerFact(const OwnerFact&) noexcept;
void RequestClose(Reason) noexcept;
void ObserveCoverage(uint32_t familyStates) noexcept;
void StartCold(bool protectionDenied) noexcept;
// Worker-only lifecycle; producer leases never retain foreign resource objects.
bool BeginManual(uint64_t operation) noexcept;
void AdvanceManual(uint64_t elapsed) noexcept;
bool EndManual(Snapshot&) noexcept;
bool ManualBusy() noexcept;
ManualLease AcquireManualLease() noexcept;
void NoteNested() noexcept;
#ifdef NR_CANDIDATE_TEST_GATE
// Only the CPU capture fixture defines this; omitted from the shipped product.
bool CaptureTestGateReady() noexcept;
void ReleaseCaptureTestGate() noexcept;
#endif

inline thread_local uint32_t creationDepth=0;
class CreationScope {
    bool active_=false,outer_=false;
    ManualLease manual_;
public:
    CreationScope() noexcept {
        if(!Enabled()) return;
        if(creationDepth==UINT32_MAX) {RequestClose(Reason::CounterExhausted);return;}
        active_=true;outer_=creationDepth++==0;
        manual_=AcquireManualLease();
        if(!outer_) {if(manual_)manual_.Nested();else NoteNested();}
    }
    ~CreationScope(){if(active_) --creationDepth;}
    bool ShouldEmit() const noexcept {return active_ && outer_ && Enabled();}
    EmitResult Emit(const Event& event) noexcept {
        if(!active_ || !outer_)return EmitResult::Disabled;
        return manual_?manual_.Emit(event):TryEmit(event);
    }
};
}
