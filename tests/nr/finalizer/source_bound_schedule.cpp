#define NR_SOURCE_TRANSACTION_TESTING
#define NR_SPECTRE_SOURCE_TESTING
#include <iostream>
#if __has_include(<nr/lifecycle/NativeSourceTransactionObservation.h>)
#include <nr/lifecycle/NativeSourceTransactionObservation.h>
#include <nr/lifecycle/Fsr3FinalConsumerAdapter.h>
namespace DlssNr { class StreamlineSourceScopeTestAccess {
public:
 static auto Scope(){auto v=std::unique_ptr<StreamlineSourceScope>(new StreamlineSourceScope);
   v->observation_=std::shared_ptr<StreamlineSourceScope::Observation>(new StreamlineSourceScope::Observation(1,2));return v;}
 static auto Observation(StreamlineSourceScope& v){return v.observation_;}
 static void Complete(StreamlineSourceScope& v,bool ok){v.Complete(ok?0:1,ok);}
}; }
namespace L=Neurotic::Lifecycle;
namespace C=Neurotic::Contracts;
namespace Neurotic::Lifecycle {
struct SourceTransactionTestAccess {
    static auto Observe(C::InterceptedSourceTransactionV1 v){return std::shared_ptr<NativeSourceTransactionObservation>(new NativeSourceTransactionObservation(std::move(v),{}));}
    static auto Enclosed(C::InterceptedSourceTransactionV1 v,std::shared_ptr<const DlssNr::StreamlineSourceScope::Observation> outer)
    {return std::shared_ptr<NativeSourceTransactionObservation>(new NativeSourceTransactionObservation(std::move(v),std::move(outer)));}
    static void Output(NativeSourceTransactionObservation& v){v.SelectedOutput({},5,8);}
    static auto Scope(std::shared_ptr<NativeSourceTransactionObservation> v){return std::unique_ptr<NativeSourceTransactionScope>(new NativeSourceTransactionScope(std::move(v)));}
    static void Return(NativeSourceTransactionObservation& v,bool ok){v.Returned(ok);}
}; }
struct NoGpu {static DlssNr::GpuSafety::Ticket Record(ID3D12GraphicsCommandList*){return {};}};
int main(){
    int checks=0,failed=0;auto check=[&](bool pass,const char* name){++checks;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto first=L::SourceTransactionTestAccess::Observe({}), second=L::SourceTransactionTestAccess::Observe({});
    // Only metadata transport is under test: none of these observations grants rights.
    using Adapter=L::BasicFsr3FinalConsumerAdapter<NoGpu>; Adapter a;int ctx=0,other=0;
    a.ObserveContext(&ctx);
    check(a.ObserveSource(&ctx,10,first),"CPU-11 explicit pending schedule");
    check(a.ObserveSource(&ctx,11,second),"CPU-12 separate pending slot");
    auto early=a.ObserveSourceCallback(11);
    check(early.reason==L::SourceAssociationReason::Pending&&early.source==second,"CPU-12 reverse callbacks keep explicit source");
    L::SourceTransactionTestAccess::Return(*second,true);
    check(a.ObserveSourceCallback(11).reason==L::SourceAssociationReason::AlreadyObserved,"CPU-13 late completion cannot backdate callback");
    check(!a.ObserveSource(&ctx,10,second),"CPU-12 live callback-key collision refused");
    check(a.ObserveSourceCallback(10).reason==L::SourceAssociationReason::Ambiguous,"CPU-12 collision poisons first association");
    check(a.ObserveSourceCallback(77).reason==L::SourceAssociationReason::Missing,"CPU-13 missing join never selects latest");
    check(!a.ObserveSource(&other,12,first),"CPU-08 wrong context refused");
    a.ObserveContext(&other);
    check(!a.ObserveSource(&other,10,second),"CPU-08 replacement context needs actual old-callback quiescence, not a local epoch");
    check(a.ObserveSourceCallback(10).reason==L::SourceAssociationReason::Missing,"CPU-08 recreated context cannot inherit old source");
    {
      auto scope=L::SourceTransactionTestAccess::Scope(first);
      check(L::NativeSourceTransactionScope::Current()==first,"CPU-11 actual caller scope carries retained record");
      {auto nested=L::SourceTransactionTestAccess::Scope({});check(!L::NativeSourceTransactionScope::Current(),"CPU-14 unsupported nested invocation masks outer");}
      check(L::NativeSourceTransactionScope::Current()==first,"CPU-14 outer restored after nested callback");
    }
    check(!L::NativeSourceTransactionScope::Current(),"CPU-14 no stale TLS after return");
    // A cold observation cannot be mistaken for a successful output write.
    auto success=L::SourceTransactionTestAccess::Observe({});
    L::SourceTransactionTestAccess::Return(*success,true);
    check(success->Status()==L::SourceAssociationReason::OutputUnavailable,"CPU-11 API success is not an output publication");
    std::array<std::unique_ptr<L::NativeSourceTransactionScope>,9> nested;
    for(auto& scope:nested)scope=L::SourceTransactionTestAccess::Scope(first);
    check(!L::NativeSourceTransactionScope::Current(),"CPU-14 excessive nesting masks source");
    for(auto it=nested.rbegin();it!=nested.rend();++it)it->reset();
    Adapter unobserved;unobserved.ObserveContext(&ctx);unobserved.ObserveContext(&other);
    check(!unobserved.ObserveSource(&other,1,second),"CPU-08 unobserved old-context callbacks cannot acquire a new-context source");
    Adapter bounded;bounded.ObserveContext(&ctx);
    bool all=true;for(unsigned i=1;i<=Adapter::Capacity;++i)all=bounded.ObserveSource(&ctx,i,first)&&all;
    check(all&&!bounded.ObserveSource(&ctx,99,second),"CPU-12 capacity refuses without evicting live pending association");
    check(bounded.ObserveSourceCallback(1).source==first,"CPU-12 capacity does not overwrite oldest live entry");
    auto outer=DlssNr::StreamlineSourceScopeTestAccess::Scope();
    auto enclosed=L::SourceTransactionTestAccess::Enclosed({},DlssNr::StreamlineSourceScopeTestAccess::Observation(*outer));
    L::SourceTransactionTestAccess::Output(*enclosed);
    L::SourceTransactionTestAccess::Return(*enclosed,true);
    check(enclosed->Status()==L::SourceAssociationReason::EnclosingPending,"CPU-15 native success preserves pending outer result");
    DlssNr::StreamlineSourceScopeTestAccess::Complete(*outer,false);
    check(enclosed->Status()==L::SourceAssociationReason::EnclosingFailed&&enclosed->SelectedOutput().has_value(),"CPU-15 outer failure retains inner write metadata without success");
    auto abandonedOuter=DlssNr::StreamlineSourceScopeTestAccess::Scope();
    auto abandoned=L::SourceTransactionTestAccess::Enclosed({},DlssNr::StreamlineSourceScopeTestAccess::Observation(*abandonedOuter));
    L::SourceTransactionTestAccess::Return(*abandoned,true);abandonedOuter.reset();
    check(abandoned->Status()==L::SourceAssociationReason::EnclosingFailed,"CPU-15 abandoned outer stays unavailable");
    auto successOuter=DlssNr::StreamlineSourceScopeTestAccess::Scope();
    auto output=L::SourceTransactionTestAccess::Enclosed({},DlssNr::StreamlineSourceScopeTestAccess::Observation(*successOuter));
    L::SourceTransactionTestAccess::Output(*output);L::SourceTransactionTestAccess::Return(*output,true);
    DlssNr::StreamlineSourceScopeTestAccess::Complete(*successOuter,true);
    check(output->Status()==L::SourceAssociationReason::MissingConsumerRights,"CPU-13 full inner/outer success is still not consumer authority");
    successOuter.reset();outer.reset();
    L::SourceTransactionTestAccess::Return(*first,false);
    check(first->Status()==L::SourceAssociationReason::Failed,"CPU-15 failed actual SR remains failed");
    L::SourceTransactionTestAccess::Return(*first,true);
    check(first->Status()==L::SourceAssociationReason::Failed,"CPU-17 effects failure cannot be rewritten as success");
    std::cout<<"Source scheduling: "<<checks<<" checks, "<<failed<<" failures; CPU only\n";return failed?1:0;
}
#else
int main(){std::cerr<<"FAIL source transaction scheduling implementation missing\n";return 1;}
#endif
