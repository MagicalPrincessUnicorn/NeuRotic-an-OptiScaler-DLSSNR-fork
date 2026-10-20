#define main capability_core_scenarios
#include "nr_capability.cpp"
#undef main
#include "../OptiScaler/nr/diagnostics/capability/CapabilityInternal.h"
#include <barrier>
#include <thread>
#include <limits>
#include <cstdlib>
static int failAllocation=-1;
void* operator new(size_t size) {
    if(failAllocation==0) { failAllocation=-1; throw std::bad_alloc(); }
    if(failAllocation>0) --failAllocation;
    if(auto p=std::malloc(size?size:1)) return p;
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept { std::free(p); }
void operator delete(void* p,size_t) noexcept { std::free(p); }
namespace DlssNr::Capability {
struct TestAccess {
    static auto State(Store& s) { return s.state; }
    static auto Cell(const WriterPort& w) { return w.cell; }
};
}
int main()
{
    Store store("session"); AdapterFactory factory(store); auto port=factory.RegisterWriter(ProducerId::Config,"nr").port;
    auto cell=TestAccess::Cell(*port); auto state=TestAccess::State(store);
    for(int allocation=0;allocation<4;++allocation) {
        failAllocation=allocation;
        { auto capture=store.Capture({}); failAllocation=-1; }
        assert(state->snapshots.load()==0); // Includes shared_ptr control-block failure.
    }
    auto seq=ReserveSample(*port); auto image=Image(*port,seq.sequence);
    std::barrier sync(2); PublishResult published=PublishResult::Unavailable;
    std::thread worker([&] { sync.arrive_and_wait(); published=TryPublish(*port,seq.sequence,image); sync.arrive_and_wait(); });
    { std::lock_guard lock(state->mutex); sync.arrive_and_wait(); sync.arrive_and_wait(); }
    worker.join(); assert(published==PublishResult::Contended);
    { auto snap=store.Capture({}); assert(snap.state==CaptureState::Gap); }
    assert(TryPublish(*port,seq.sequence,image)==PublishResult::Published);
    auto stagedSeq=ReserveSample(*port); auto stagedImage=Image(*port,stagedSeq.sequence);
    StageResult staged=StageResult::Closed;
    std::thread stager([&] { sync.arrive_and_wait(); staged=TryStageEnvelope(*port,stagedSeq.sequence,stagedImage); sync.arrive_and_wait(); });
    { std::lock_guard lock(cell->pendingMutex); sync.arrive_and_wait(); sync.arrive_and_wait(); }
    stager.join(); assert(staged==StageResult::Contended);
    { auto snap=store.Capture({}); assert(Query(*snap.snapshot,Request()).resolution==Resolution::Unknown); }
    assert(TryStageEnvelope(*port,stagedSeq.sequence,stagedImage)==StageResult::Staged);
    // Multiple drainers cannot commit the same reserved sample twice.
    std::vector<PublishResult> first,second;
    std::thread drainA([&] { sync.arrive_and_wait(); first=factory.DrainStagedObservations({}); });
    sync.arrive_and_wait(); second=factory.DrainStagedObservations({}); drainA.join();
    unsigned commits=0; for(auto r:first) commits+=r==PublishResult::Published;
    for(auto r:second) commits+=r==PublishResult::Published; assert(commits==1);    // Sequence overflow never reuses zero.
    cell->offered.store(UINT64_MAX); auto exhausted=ReserveSample(*port); assert(exhausted.state==SequenceState::Exhausted);
    assert(TryPublish(*port,1,image)==PublishResult::Closed);
    auto newPort=factory.RegisterWriter(ProducerId::Config,"nr").port; assert(newPort->Id()!=port->Id());
    auto newSeq=ReserveSample(*newPort); auto invalid=Image(*newPort,newSeq.sequence);
    invalid.factCount=33; assert(TryPublish(*newPort,newSeq.sequence,invalid)==PublishResult::InvalidBatch);
    invalid=Image(*newPort,newSeq.sequence); invalid.used=65537;
    assert(TryPublish(*newPort,newSeq.sequence,invalid)==PublishResult::CapacityExceeded);
    invalid=Image(*newPort,newSeq.sequence); invalid.facts[0]=65532;
    assert(TryPublish(*newPort,newSeq.sequence,invalid)==PublishResult::InvalidBatch);
    invalid=Image(*newPort,newSeq.sequence);
    auto& fact=*reinterpret_cast<Fact*>(invalid.arena.data()+invalid.facts[0]);
    const char alias[]="foreign-id"; std::memcpy(invalid.arena.data()+fact.id.offset,alias,sizeof(alias)-1); fact.id.size=sizeof(alias)-1;
    assert(TryPublish(*newPort,newSeq.sequence,invalid)==PublishResult::InvalidBatch);
    std::cout << "capability races: PASS\n";
}

