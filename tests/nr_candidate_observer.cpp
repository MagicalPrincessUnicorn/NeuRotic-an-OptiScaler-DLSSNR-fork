#include "nr/diagnostics/candidate/Sink.h"
#include "nr/diagnostics/candidate/Reducer.h"
#include <cassert>
#include <iostream>
#include <thread>
#include <vector>
#include <memory>

using namespace DlssNr::CandidateObserver;
static Event creation(uintptr_t resource = 10) {
    Event e{}; e.kind=Kind::Committed; e.deviceBits=20; e.resourceBits=resource;
    e.descriptor.dimension=3; e.descriptor.width=1920; e.descriptor.height=1080;
    e.descriptor.depth=1; e.descriptor.mips=1; e.descriptor.format=40;
    e.descriptor.flags=2; e.descriptor.sampleCount=1; e.descriptorKnown=true; return e;
}
int main() {
    assert(Classify(false,0,true,true,true)==Kind::Alternate);
    assert(Classify(true,0x80004005u,true,true,true)==Kind::Failed);
    assert(Classify(true,1,false,false,true)==Kind::CapabilityOnly);
    assert(Classify(true,0,true,false,true)==Kind::Anomalous);
    assert(Classify(true,1,true,true,true)==Kind::ResultUnclassified);
    assert(Classify(true,0,true,true,false)==Kind::InterfaceUnclassified);
    assert(Classify(true,0,true,true,true)==Kind::Committed);
    auto sinkStorage=std::make_unique<Sink>(); auto& sink=*sinkStorage;
    assert(sink.TryEmit(creation())==EmitResult::Disabled);
    assert(sink.Start());
    for(int i=0;i<8;i++) assert(sink.TryEmit(creation())==EmitResult::Recorded);
    assert(sink.TryEmit(creation())==EmitResult::BudgetRefused);
    sink.AdvanceWindow(0); // worker polling cannot manufacture another collection window
    assert(sink.TryEmit(creation())==EmitResult::BudgetRefused);
    Event batch[64]{}; auto n=sink.Drain(batch,64); assert(n==8);
    sink.AdvanceWindow(16);
    assert(sink.TryEmit(creation())==EmitResult::Recorded);
    n=sink.Drain(batch,64); assert(n==1 && batch[0].epoch>1 && batch[0].sequence==9);
    assert(sink.Stats().lossMask!=0 && !sink.Stats().complete);
    auto reducerStorage=std::make_unique<Reducer>(); auto& reducer=*reducerStorage;
    assert(reducer.Apply(batch[0]));
    auto next=batch[0]; next.sequence++; next.elapsedMs=2001;
    assert(reducer.Apply(next));
    assert(reducer.Count()==2);
    auto view=reducer.Snapshot(); assert(view.count==2);
    assert(view.candidates[0].token!=view.candidates[1].token);
    assert(view.candidates[1].retention==Retention::Superseded);
    assert(!view.candidates[0].mayAdmit);
    assert(view.candidates[0].depthSupported);
    Event tick{};tick.kind=Kind::Coverage;tick.sequence=next.sequence+1;
    tick.epoch=next.epoch;tick.timeKnown=true;tick.elapsedMs=12001;
    assert(reducer.Apply(tick));assert(reducer.Count()==0);
    next.sequence=tick.sequence+1;next.elapsedMs=12002;
    assert(reducer.Apply(next));assert(reducer.Predecessor(next.sequence)==0);
    assert(reducer.TombstoneCount()==2);
    sink.Close(Reason::Requested);
    assert(sink.TryEmit(creation())==EmitResult::Closing);
    assert(sink.Drain(batch,64)==0);
    auto exhaustedStorage=std::make_unique<Sink>(); auto& exhausted=*exhaustedStorage;
    assert(exhausted.Start(UINT64_MAX-1,1));
    assert(exhausted.TryEmit(creation())==EmitResult::Recorded);
    assert(exhausted.TryEmit(creation(11))==EmitResult::CounterExhausted);
    exhausted.Close(Reason::Requested);assert(exhausted.Stats().reason==Reason::CounterExhausted && !exhausted.Stats().complete);
    auto stopped=std::make_unique<Sink>();stopped->Close(Reason::Requested);assert(!stopped->Start());
    auto quotas=std::make_unique<Sink>();assert(quotas->Start());
    for(uintptr_t resource=1;resource<=4;resource++)assert(quotas->TryEmit(creation(resource))==EmitResult::Recorded);
    assert(quotas->TryEmit(creation(5))==EmitResult::BudgetRefused); // same four-way bucket
    auto frames=std::make_unique<Sink>();assert(frames->Start());
    auto owner=creation();owner.frameKnown=true;owner.frameContract=1;owner.frameOrdinal=0;
    for(unsigned i=0;i<8;i++)assert(frames->TryEmit(owner)==EmitResult::Recorded);
    frames->AdvanceWindow(16);assert(frames->TryEmit(owner)==EmitResult::BudgetRefused);
    for(unsigned i=1;i<=4;i++){owner.frameOrdinal=i;frames->AdvanceWindow(16*(i+1));assert(frames->TryEmit(owner)==EmitResult::Recorded);}
    owner.frameOrdinal=0;frames->AdvanceWindow(112);assert(frames->TryEmit(owner)==EmitResult::BudgetRefused);
    auto controls=std::make_unique<Sink>();assert(controls->Start());Event control{};control.kind=Kind::Coverage;
    for(unsigned i=0;i<32;i++)assert(controls->TryEmit(control)==EmitResult::Recorded);
    assert(controls->TryEmit(control)==EmitResult::BudgetRefused && !controls->Enabled());
    auto queue=std::make_unique<Sink>();assert(queue->Start());
    uint64_t t=0;
    for(unsigned i=0;i<126;i++) {
        for(uintptr_t resource=1;resource<=4;resource++)for(unsigned repeat=0;repeat<8;repeat++)assert(queue->TryEmit(creation(resource))==EmitResult::Recorded);
        queue->AdvanceWindow(t+=16);
    }
    assert(queue->Stats().highWater==4032);assert(queue->TryEmit(creation(1))==EmitResult::QueueFull);
    assert(queue->TryEmit(control)==EmitResult::Recorded);queue->Close(Reason::Requested);
    unsigned drained=0;while(auto amount=queue->Drain(batch,64))drained+=amount;
    assert(drained==4033 && queue->EmptyAfterClose());
    auto session=std::make_unique<Sink>();assert(session->Start());t=0;
    for(unsigned i=0;i<2047;i++) {
        for(uintptr_t resource=1;resource<=4;resource++)for(unsigned repeat=0;repeat<8;repeat++)assert(session->TryEmit(creation(resource))==EmitResult::Recorded);
        assert(session->Drain(batch,64)==32);session->AdvanceWindow(t+=16);
    }
    assert(session->Stats().enqueued==65504);
    assert(session->TryEmit(creation())==EmitResult::BudgetRefused && !session->Enabled());
    auto concurrent=std::make_unique<Sink>();assert(concurrent->Start());
    std::atomic<unsigned> ready{0},done{0};std::atomic<bool> go{false};std::vector<std::thread> producers;
    for(unsigned i=0;i<4;i++)producers.emplace_back([&,i]{ready.fetch_add(1);while(!go.load())std::this_thread::yield();
        for(unsigned repeat=0;repeat<10000;repeat++)(void)concurrent->TryEmit(creation(i+1));done.fetch_add(1);});
    while(ready.load()!=4)std::this_thread::yield();go.store(true);uint64_t previous=0,total=0;
    while(done.load()!=4) {
        auto amount=concurrent->Drain(batch,64);for(unsigned i=0;i<amount;i++){assert(batch[i].sequence>previous);previous=batch[i].sequence;total++;}
        std::this_thread::yield();
    }
    for(auto& thread:producers)thread.join();concurrent->Close(Reason::Requested);
    while(auto amount=concurrent->Drain(batch,64))for(unsigned i=0;i<amount;i++){assert(batch[i].sequence>previous);previous=batch[i].sequence;total++;}
    assert(total==concurrent->Stats().enqueued && !concurrent->Stats().complete);
    assert(concurrent->Stats().dropped<=40000-total);
    for(unsigned repeat=0;repeat<100;repeat++) {
        auto racing=std::make_unique<Sink>();std::atomic<bool> startNow{false};
        std::thread initializer([&]{while(!startNow.load())std::this_thread::yield();(void)racing->Start();});
        std::thread closer([&]{while(!startNow.load())std::this_thread::yield();racing->Close(Reason::Requested);});
        startNow.store(true);initializer.join();closer.join();assert(!racing->Enabled() && !racing->Start());
    }
    std::cout << "candidate contract/budget/reuse/exhaustion checks passed\n";
}
