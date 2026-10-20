#include "../OptiScaler/nr/diagnostics/candidate/Observer.h"
#include <windows.h>
#include <cassert>
#include <iostream>
#include <thread>
using namespace DlssNr::CandidateObserver;
std::atomic<bool> emitLocked=false,releaseEmit=false;
std::thread producer;
namespace DlssNr::CandidateObserver {
void EmitLockedForTest(const Event& event) noexcept {
    if(event.resourceBits==999) {emitLocked.store(true);while(!releaseEmit.load())Sleep(1);}
}
void BeforeManualRetireForTest() noexcept {
    if(emitLocked.load() && producer.joinable()) {releaseEmit.store(true);producer.join();}
}
}
int main() {
    SetEnvironmentVariableA("NEUROTIC_CANDIDATE_OBSERVER",nullptr);
    StartCold(false);assert(BeginManual(1));
    OwnerFact unsupported{};unsupported.available=true;
    assert(TryObserveOwnerFact(unsupported)==ImportResult::UnsupportedContract);
    Snapshot result{};
    {
        CreationScope held;assert(held.ShouldEmit());
        Event event{};event.kind=Kind::Committed;event.source=1;event.resourceBits=3;
        event.descriptorKnown=true;event.descriptor.dimension=3;event.descriptor.width=1280;
        event.descriptor.height=720;event.descriptor.format=40;
        assert(held.Emit(event)==EmitResult::Recorded);
        AdvanceManual(1999);assert(ManualBusy());
        AdvanceManual(2000);assert(!EndManual(result));assert(!BeginManual(2));
        assert(held.Emit(event)==EmitResult::Closing);
    }
    assert(EndManual(result));assert(!ManualBusy());assert(result.count==1);
    assert(!result.candidates[0].mayAdmit && result.coverage.familyEnqueued[1]==1);
    assert(BeginManual(2));AdvanceManual(2000);assert(EndManual(result));assert(result.count==0);
    assert(BeginManual(3));
    producer=std::thread([] {
        CreationScope scope;Event event{};event.kind=Kind::Committed;event.source=1;event.resourceBits=999;
        assert(scope.Emit(event)==EmitResult::Recorded);
    });
    while(!emitLocked.load())Sleep(1);
    assert(EndManual(result));assert(result.coverage.familyEnqueued[1]==1);
    assert(result.count==1); // final drain must include a producer that finished during retirement
    assert(BeginManual(4));AdvanceManual(2000);assert(EndManual(result));assert(result.count==0);
    RequestClose(Reason::SourceDetach);assert(!BeginManual(5));
    std::cout<<"actual observer manual rearm, callback pinning and detach checks passed\n";
}
