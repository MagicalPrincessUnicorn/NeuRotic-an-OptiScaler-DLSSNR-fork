#include "finalizer_fixture.h"
#include <array>
#include <thread>
using namespace FinalizerTests;
struct ReentrantReader
{
    Fixture& fixture;mutable bool entered=false,busy=false;
    template<class T> X::MetadataView<T> Resolve(const C::MetadataRef<T>& ref)const
    {
        if(!entered)
        {
            entered=true;
            busy=fixture.finalizer->TryClaimPrimary(fixture.request,*this,fixture.Event()).reason==L::FinalizationReason::Busy;
        }
        return fixture.store.Resolve(ref);
    }
};
int main(int argc,char** argv)
{
    return Guard([&]{Report r("finalizer-concurrency",argc==2?std::string_view(argv[1]):std::string_view{});
        auto reentrant=std::make_unique<Fixture>();ReentrantReader reader{*reentrant};
        auto primary=reentrant->finalizer->TryClaimPrimary(reentrant->request,reader,reentrant->Event());
        r.Check("T27C.reentrant-refused",reader.busy,true,"TryClaimPrimary");
        r.Check("T27C.outer-unaffected",primary.accepted,true,"TryClaimPrimary");
        auto f=std::make_unique<Fixture>();auto claim=f->Claim();
        if(!claim.claim)throw std::runtime_error("concurrency fixture primary unavailable");
        f->Produce(*claim.claim);f->Prepare(*claim.claim);
        std::array<L::FinalizerEvent,8> events;for(auto& event:events)event=f->Event();
        std::array<L::FinalizationResult,8> results;std::array<std::thread,8> workers;std::atomic<bool> start{false};
        // Immutable test owner facts are shared; no GPU operation or lifetime proof
        // is inferred from this CPU race. Production owners synchronize mutation.
        for(std::size_t i=0;i<workers.size();++i)workers[i]=std::thread([&,i]{
            while(!start.load(std::memory_order_acquire))std::this_thread::yield();
            results[i]=f->finalizer->CommitSeal(*claim.claim,f->request.generations,f->Proof(),f->store,events[i]);});
        start.store(true,std::memory_order_release);for(auto& worker:workers)worker.join();
        std::size_t accepted=0;bool unique=true,refusalsBusy=true;
        for(const auto& result:results)
        {
            if(result.accepted){++accepted;unique=unique&&result.packet&&result.packet->seal.value==1&&result.packet->baseRealFrame.value==10;}
            else refusalsBusy=refusalsBusy&&result.reason==L::FinalizationReason::Busy;
        }
        r.Check("T27C.accepted-commit",accepted>=1,true,"CommitSeal");
        r.Check("T27C.one-literal-seal",unique,true,"CommitSeal");
        r.Check("T27C.nonblocking-refusal",refusalsBusy,true,"CommitSeal");
        return r.Finish();});
}
