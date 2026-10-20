#define NR_NATIVE_FINALIZER_TESTING 1
#include "finalizer_fixture.h"
#include <thread>
#include <barrier>
#if __has_include(<nr/lifecycle/NativeUnsealedRetirement.h>)
#include <nr/lifecycle/NativeUnsealedRetirement.h>
#define HAS_UNSEALED_RECEIPT 1
#endif
using namespace FinalizerTests;
#ifdef HAS_UNSEALED_RECEIPT
namespace Neurotic::Lifecycle
{
// Synthetic capability producer exists only in this CPU fixture translation
// unit. No fixture result is provider/recording terminality evidence.
struct NativeUnsealedTestAccess
{
    static NativeUnsealedRetirement Receipt(PrimaryNrClaimHandle claim,const C::NrExecutionRecipe& recipe,
        const C::NativeSampleIdentityV1& sample,const C::RecordKey& recording)
    {return NativeUnsealedRetirement(claim,recipe,sample,recording);}
};
}
C::NativeSampleIdentityV1 Sample(Fixture& f,std::uint64_t n=1)
{
    auto frame=*f.store.Resolve(f.request.recipe.frame).value;
    C::NativeSampleIdentityV1 s;s.session=f.session;s.stream=frame.renderStreamId.KnownPart()->value;
    s.view=frame.viewId.KnownPart()->value;s.feature=Id<C::ObjectIncarnation>("feature",1);
    s.ingress=Id<C::ObjectIncarnation>("ingress",1);s.producerOrdinal=n;s.callback=Key("callback",n);
    frame.nativeSample=f.store.Publish(s,C::OwnerDomain::Provider);
    frame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",9+n));
    frame.evaluationId=Known(Id<C::EvaluationId>("evaluation",n));f.Frame(frame);
    return s;
}
void Retirement(Report& r)
{
    auto f=std::make_unique<Fixture>(1);auto sample=Sample(*f);auto claim=f->Claim();
    if(!claim.claim)throw std::runtime_error("sample claim unavailable");
    const auto frame=*f->store.Resolve(f->request.recipe.frame).value;
    auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,500);
    auto receipt=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,sample,Key("recording"));
    auto boundaryProof=f->Proof();
    const auto retire=[&](const auto& proof,const auto& owner,const auto& through,const auto& excluded){
        return f->finalizer->RetireInterrupted(*claim.claim,proof,owner,through,excluded,f->store,f->Event(),&boundaryProof);};
    r.Check("F09.must-interrupt",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->Produce(*claim.claim);f->Prepare(*claim.claim);
    r.Check("F09.interrupt-prepared",f->finalizer->InterruptUnsealed(*claim.claim,f->Event()).accepted,true,"InterruptUnsealed");
    r.Check("F09.no-duplicate-candidate",f->Produce(*claim.claim).accepted,false,"SubmitEvaluationResult");
    r.Check("F09.no-duplicate-boundary",f->Prepare(*claim.claim).accepted,false,"PrepareSeal");
    r.Check("F09.no-prepared-commit",f->Commit(*claim.claim).accepted,false,"CommitSeal");
    r.Check("F09.prepared-hold-not-covered",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    L::CloseAdmission(*f->lease);f->facts.ownerCanRecycle=L::Fact::Yes;
    f->facts.consumerTicketsRetired=L::Fact::Yes;f->facts.dependency.reusable=L::Fact::Yes;
    r.Check("F09.downstream-held",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->facts.holds.Get(0)->released=L::Fact::Yes;f->facts.holds.Get(0)->releaseEvidence=Key("downstream.release");
    auto saved=f->facts.holds;f->facts.holds={};
    r.Check("F09.no-lost-registration",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->facts.holds=saved;f->facts.holds.Get(0)->provider=Id<C::ProviderIncarnation>("other.provider",1);
    r.Check("F09.wrong-release-provider",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->facts.holds=saved;f->facts.dependency.nonReplayable=L::Fact::No;
    r.Check("F09.downstream-replay",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->facts.dependency.nonReplayable=L::Fact::Yes;f->facts.dependency.reusable=L::Fact::Unknown;
    r.Check("F09.downstream-unknown-completion",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    f->facts.dependency.reusable=L::Fact::Yes;
    auto changed=f->request.recipe;++changed.header.revision;
    auto bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,changed,sample,Key("recording"));
    r.Check("F09.exact-recipe",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    auto wrong=sample;++wrong.feature.value;
    bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,wrong,Key("recording"));
    r.Check("F09.feature-recreation",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    wrong=sample;++wrong.ingress.value;
    bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,wrong,Key("recording"));
    r.Check("F09.ingress-recreation",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    wrong=sample;++wrong.callback.value;
    bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,wrong,Key("recording"));
    r.Check("F09.exact-callback",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    wrong=sample;++wrong.producerOrdinal;
    bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,wrong,Key("recording"));
    r.Check("F09.exact-sample",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    bad=L::NativeUnsealedTestAccess::Receipt(*claim.claim,f->request.recipe,sample,C::RecordKey{});
    r.Check("F09.missing-recording",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    auto foreign=std::make_unique<Fixture>();Sample(*foreign);auto fc=foreign->Claim();
    bad=L::NativeUnsealedTestAccess::Receipt(*fc.claim,f->request.recipe,sample,Key("recording"));
    r.Check("F09.wrong-finalizer",retire(bad,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    auto owner=registry;owner.owner=C::OwnerDomain::Strategy;
    r.Check("F09.wrong-exclusion-owner",retire(receipt,owner,frame,Known(true)).accepted,false,"RetireInterrupted");
    r.Check("F09.unknown-exclusion",retire(receipt,registry,frame,C::OptionalFact<bool>{}).accepted,false,"RetireInterrupted");
    r.Check("F09.false-exclusion",retire(receipt,registry,frame,Known(false)).accepted,false,"RetireInterrupted");
    auto otherFrame=frame;otherFrame.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));
    r.Check("F09.wrong-exclusion-frame",retire(receipt,registry,otherFrame,Known(true)).accepted,false,"RetireInterrupted");
    r.Check("F09.retired-no-seal",[&]{auto v=retire(receipt,registry,frame,Known(true));return v.accepted&&!v.packet;}(),true,"RetireInterrupted");
    r.Check("F09.duplicate-receipt",retire(receipt,registry,frame,Known(true)).accepted,false,"RetireInterrupted");
    r.Check("F09.persistent-exclusion",f->Claim().accepted,false,"TryClaimPrimary");
    unsigned retired=0;auto oldClaim=*claim.claim;
    for(std::uint64_t i=2;i<=202;++i)
    {
        sample=Sample(*f,i);auto current=f->Claim();if(!current.claim)break;
        if(f->finalizer->InterruptUnsealed(oldClaim,f->Event()).accepted)throw std::runtime_error("stale slot reopened");
        if(!f->finalizer->InterruptUnsealed(*current.claim,f->Event()).accepted)break;
        auto proof=L::NativeUnsealedTestAccess::Receipt(*current.claim,f->request.recipe,sample,Key("recording",i));
        const auto through=*f->store.Resolve(f->request.recipe.frame).value;
        auto result=f->finalizer->RetireInterrupted(*current.claim,proof,registry,through,Known(true),f->store,f->Event());
        if(!result.accepted||result.packet)break;++retired;oldClaim=*current.claim;
    }
    r.Check("F09.sequential-201",retired==201,true,"RetireInterrupted");
    f->finalizer->StopAdmissions();auto drain=f->finalizer->DrainStatus();
    r.Check("F09.drained",drain&&drain->safelyDrained&&drain->unsealed==0,true,"DrainStatus");
    auto sealed=std::make_unique<Fixture>();auto sc=sealed->Claim();sealed->Produce(*sc.claim);sealed->Prepare(*sc.claim);sealed->Commit(*sc.claim);
    r.Check("F09.sealed-not-interrupted",sealed->finalizer->InterruptUnsealed(*sc.claim,sealed->Event()).accepted,false,"InterruptUnsealed");
}
void PrefixAndRace(Report& r)
{
    auto f=std::make_unique<Fixture>(2);auto s1=Sample(*f,1);auto a=f->Claim();
    auto changedBinding=f->binding;changedBinding.sameRecording=L::SameRecordingUse{};
    r.Check("F09.binding-recording-exact",L::SameFinalFrameBinding(f->binding,changedBinding),false,"SameFinalFrameBinding");
    const auto request1=f->request;const auto frame1=*f->store.Resolve(request1.recipe.frame).value;
    auto s2=Sample(*f,2);auto b=f->Claim();const auto frame2=*f->store.Resolve(f->request.recipe.frame).value;
    auto registry=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,500);
    auto p1=L::NativeUnsealedTestAccess::Receipt(*a.claim,request1.recipe,s1,Key("recording",1));
    auto p2=L::NativeUnsealedTestAccess::Receipt(*b.claim,f->request.recipe,s2,Key("recording",2));
    f->finalizer->InterruptUnsealed(*a.claim,f->Event());f->finalizer->InterruptUnsealed(*b.claim,f->Event());
    r.Check("F09.prefix-missing-exclusion",f->finalizer->RetireInterrupted(*a.claim,p1,registry,frame1,Known(false),f->store,f->Event()).accepted,false,"RetireInterrupted");
    r.Check("F09.prefix-keeps-unacknowledged",f->finalizer->RetireInterrupted(*b.claim,p2,registry,frame2,Known(true),f->store,f->Event()).accepted,false,"RetireInterrupted");
    r.Check("F09.prefix-own-ack",f->finalizer->RetireInterrupted(*a.claim,p1,registry,frame1,Known(true),f->store,f->Event()).accepted,true,"RetireInterrupted");
    r.Check("F09.prefix-next-ack",f->finalizer->RetireInterrupted(*b.claim,p2,registry,frame2,Known(true),f->store,f->Event()).accepted,true,"RetireInterrupted");
    unsigned coherent=0;
    for(unsigned i=0;i<64;++i)
    {
        auto race=std::make_unique<Fixture>();auto c=race->Claim();race->Produce(*c.claim);race->Prepare(*c.claim);
        const auto event1=race->Event(),event2=race->Event();std::barrier start(3);
        L::FinalizationResult interrupted,sealed;
        std::thread first([&]{start.arrive_and_wait();interrupted=race->finalizer->InterruptUnsealed(*c.claim,event1);});
        std::thread second([&]{start.arrive_and_wait();sealed=race->finalizer->CommitSeal(*c.claim,race->request.generations,race->Proof(),race->store,event2);});
        start.arrive_and_wait();first.join();second.join();
        if(interrupted.accepted&&sealed.accepted)throw std::runtime_error("interrupted reservation sealed");
        if(!interrupted.accepted&&!sealed.accepted)throw std::runtime_error("race had no linearized outcome");
        if(interrupted.accepted&&race->Commit(*c.claim).accepted)throw std::runtime_error("late seal after interruption");
        if(sealed.accepted&&race->finalizer->InterruptUnsealed(*c.claim,race->Event()).accepted)throw std::runtime_error("sealed interrupted");
        ++coherent;
    }
    r.Check("F09.interrupt-seal-races-64",coherent==64,true,"InterruptUnsealed/CommitSeal");
    auto bounded=std::make_unique<Fixture>(1);auto firstSample=Sample(*bounded);auto first=bounded->Claim();
    auto firstFrame=*bounded->store.Resolve(bounded->request.recipe.frame).value;
    auto firstReceipt=L::NativeUnsealedTestAccess::Receipt(*first.claim,bounded->request.recipe,firstSample,Key("recording",1));
    bounded->finalizer->InterruptUnsealed(*first.claim,bounded->Event());
    r.Check("F09.first-family-retired",bounded->finalizer->RetireInterrupted(*first.claim,firstReceipt,registry,firstFrame,Known(true),bounded->store,bounded->Event()).accepted,true,"RetireInterrupted");
    firstFrame.viewId=Known(Id<C::ViewId>("other.view",2));bounded->Frame(firstFrame);
    auto nextSample=Sample(*bounded,2);auto next=bounded->Claim();
    auto nextFrame=*bounded->store.Resolve(bounded->request.recipe.frame).value;
    auto nextReceipt=L::NativeUnsealedTestAccess::Receipt(*next.claim,bounded->request.recipe,nextSample,Key("recording",2));
    bounded->finalizer->InterruptUnsealed(*next.claim,bounded->Event());
    r.Check("F09.exclusion-capacity-refuses",bounded->finalizer->RetireInterrupted(*next.claim,nextReceipt,registry,nextFrame,Known(true),bounded->store,bounded->Event()).reason==L::FinalizationReason::Capacity,true,"RetireInterrupted");
    bounded->finalizer->StopAdmissions();const auto held=bounded->finalizer->DrainStatus();
    r.Check("F09.unacknowledged-claim-not-drained",held&&!held->safelyDrained&&held->unsealed==1,true,"DrainStatus");
}
#endif
template<class Finalizer> void Interruption(Report& r,Fixture& f,Finalizer& finalizer)
{
    auto claim=f.Claim();if(!claim.claim)throw std::runtime_error("claim unavailable");
    if constexpr(requires { finalizer.InterruptUnsealed(*claim.claim,f.Event()); })
    {
        r.Check("F09.interrupt",finalizer.InterruptUnsealed(*claim.claim,f.Event()).accepted,true,"InterruptUnsealed");
        r.Check("F09.no-late-candidate",f.Produce(*claim.claim).accepted,false,"SubmitEvaluationResult");
        r.Check("F09.no-late-prepare",f.Prepare(*claim.claim).accepted,false,"PrepareSeal");
        r.Check("F09.no-late-seal",f.Commit(*claim.claim).accepted,false,"CommitSeal");
        r.Check("F09.no-new-primary",f.Claim().newPrimaryClaim,false,"TryClaimPrimary");
        auto duplicate=finalizer.InterruptUnsealed(*claim.claim,f.Event());
        r.Check("F09.interrupt-idempotent",duplicate.accepted&&!duplicate.packet,true,"InterruptUnsealed");
        finalizer.StopAdmissions();auto drain=finalizer.DrainStatus();
        r.Check("F09.interruption-not-retirement",drain&&!drain->safelyDrained,true,"DrainStatus");
    }
    else r.Check("F09.interrupt-api",false,true,"InterruptUnsealed");
}
int main(int argc,char** argv)
{
    return Guard([&]{Report r("finalizer-unsealed",argc==2?std::string_view(argv[1]):std::string_view{});
        auto f=std::make_unique<Fixture>();Interruption(r,*f,*f->finalizer);
#ifdef HAS_UNSEALED_RECEIPT
        Retirement(r);
        PrefixAndRace(r);
#endif
        return r.Finish();});
}
