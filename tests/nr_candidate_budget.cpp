#include "nr/diagnostics/candidate/Sink.h"
#include <cassert>
#include <memory>
#include <iostream>
#include <array>
#include <vector>
using namespace DlssNr::CandidateObserver;
static Event creation(uintptr_t resource=1) {
    Event e{};e.kind=Kind::Committed;e.source=1;e.deviceBits=20;e.resourceBits=resource;return e;
}
static void refused(Sink& sink, const Event& e, BudgetCause cause) {
    assert(sink.TryEmit(e)==EmitResult::BudgetRefused);
    const auto stats=sink.Stats();uint64_t sum=0;
    for(uint32_t i=0;i<BudgetCauseCount;i++) {sum+=stats.budgetRefused[i];assert(stats.budgetRefusalExact[i]);}
    assert(stats.budgetRefused[static_cast<uint32_t>(cause)]==1 && sum==1);
    assert(stats.dropped==1 && stats.lossMask==(1u<<static_cast<uint32_t>(Reason::Budget)) && !stats.complete);
}
static std::vector<uintptr_t> resources(uint32_t buckets,uint32_t perBucket,uint32_t desired) {
    std::array<unsigned,256> counts{};std::vector<uintptr_t> result;
    for(uintptr_t r=16;result.size()<desired;r+=16) {
        const uint64_t h=20^((r>>4)*0x9e3779b97f4a7c15ULL);
        const auto b=static_cast<uint32_t>((h^(h>>32))%buckets);
        if(counts[b]<perBucket) {++counts[b];result.push_back(r);}
    }
    return result;
}
int main() {
    Event batch[64]{};
    {
        auto s=std::make_unique<Sink>();assert(s->Start());
        for(unsigned i=0;i<8;i++)assert(s->TryEmit(creation())==EmitResult::Recorded);
        refused(*s,creation(),BudgetCause::ResourceWindow);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());
        // Five distinct addresses sharing one four-way bucket while 1020 slots remain unused.
        for(uintptr_t r=1;r<=4;r++)assert(s->TryEmit(creation(r))==EmitResult::Recorded);
        refused(*s,creation(5),BudgetCause::WindowQuotaSlots);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());
        for(auto r:resources(256,1,64)) {
            for(unsigned i=0;i<8;i++)assert(s->TryEmit(creation(r))==EmitResult::Recorded);
            assert(s->Drain(batch,64)==8);
        }
        refused(*s,creation(),BudgetCause::WindowData);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());Event e{};e.kind=Kind::Coverage;
        for(unsigned i=0;i<32;i++)assert(s->TryEmit(e)==EmitResult::Recorded);
        refused(*s,e,BudgetCause::SessionControl);assert(!s->Enabled());
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());Event e{};e.kind=Kind::Coverage;
        for(unsigned i=0;i<32;i++)assert(s->TryEmit(e)==EmitResult::Recorded);
        assert(s->Drain(batch,64)==32);
        for(auto r:resources(256,1,64)) {
            for(unsigned i=0;i<8;i++)assert(s->TryEmit(creation(r))==EmitResult::Recorded);
            assert(s->Drain(batch,64)==8);
        }
        refused(*s,creation(),BudgetCause::WindowData);
        assert(!s->Enabled()); // preserve cross-family session exhaustion closure
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());
        for(unsigned window=0;window<8188;window++) {
            for(unsigned i=0;i<8;i++)assert(s->TryEmit(creation())==EmitResult::Recorded);
            assert(s->Drain(batch,64)==8);s->AdvanceWindow(16ull*(window+1));
        }
        refused(*s,creation(),BudgetCause::SessionData);assert(!s->Enabled());
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());auto e=creation();e.frameKnown=true;e.frameContract=1;
        for(unsigned i=0;i<8;i++)assert(s->TryEmit(e)==EmitResult::Recorded);
        s->AdvanceWindow(16);refused(*s,e,BudgetCause::ResourceFrame);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());auto e=creation();e.frameKnown=true;e.frameContract=1;
        for(unsigned i=0;i<16;i++) {e.frameScope=i;assert(s->TryEmit(e)==EmitResult::Recorded);s->AdvanceWindow(16ull*(i+1));}
        e.frameScope=16;refused(*s,e,BudgetCause::FrameStreams);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());auto e=creation();e.frameKnown=true;e.frameContract=1;
        for(unsigned i=0;i<5;i++) {e.frameOrdinal=i;assert(s->TryEmit(e)==EmitResult::Recorded);s->AdvanceWindow(16ull*(i+1));}
        e.frameOrdinal=0;refused(*s,e,BudgetCause::FrameOrder);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());auto e=creation();e.frameKnown=true;e.frameContract=1;
        for(uintptr_t r=1;r<=4;r++) {e.resourceBits=r;assert(s->TryEmit(e)==EmitResult::Recorded);s->AdvanceWindow(16*r);}
        e.resourceBits=5;refused(*s,e,BudgetCause::FrameQuotaSlots);
    }
    {
        auto s=std::make_unique<Sink>();assert(s->Start());uint64_t time=0;auto e=creation();e.frameKnown=true;e.frameContract=1;
        for(auto r:resources(16,4,64)) {
            e.resourceBits=r;
            for(unsigned i=0;i<8;i++)assert(s->TryEmit(e)==EmitResult::Recorded);
            assert(s->Drain(batch,64)==8);s->AdvanceWindow(time+=16);
        }
        refused(*s,e,BudgetCause::FrameData);
    }
    std::cout<<"PASS: exact budget causes, colliding addresses, per-resource, frame and session limits; loss remains explicit\n";
}
