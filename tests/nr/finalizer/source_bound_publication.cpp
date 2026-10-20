#define NR_SOURCE_BOUND_CONSUMER_TESTING 1
#define NR_SOURCE_TRANSACTION_TESTING 1
#include <nr/lifecycle/Fsr3SourceBoundConsumerPublication.h>
#include <nr/lifecycle/Fsr3SourceBoundPreflight.h>
#define main existing_source_bound_main
#include "source_bound_consumer.cpp"
#undef main
#include <iostream>
#include <inputs/NativeControlledFgBoundary.h>
namespace Neurotic::Lifecycle
{
struct SourceBoundPublicationTestAccess
{
    static auto Apply(NativeSourceBoundConsumerAction& action,FinalRealFrameFinalizer& f,NativeFinalConsumerEvents& e)
    {return action.Apply(f,e);}
};
}
struct SyntheticSession
{
    std::shared_ptr<SourceFixture> f;
    unsigned applyCalls=0;bool omitAuthorization=false;
    bool RetainSourceBoundConsumer(const L::SourceBoundClaimHandle&,std::shared_ptr<L::NativeSourceBoundConsumerTail> tail)
    {return tail&&tail->Transaction()==f->transaction;}
    auto ApplySourceBoundConsumer(const L::SourceBoundClaimHandle& claim,L::NativeSourceBoundConsumerAction& action)
    {
        ++applyCalls;
        auto identity=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::IdentityRegistry,900);
        identity.scope=f->source.recipe.header.scope;
        L::NativeFinalConsumerEvents events{[&]{return f->Event();},{},identity};
        auto result=L::SourceBoundPublicationTestAccess::Apply(action,*f->finalizer,events);
        if(result.logical)Access::Logical(*f->finalizer,claim,f->Event());
        if(omitAuthorization){result.authorization.reset();result.reason=L::FinalizationReason::None;}
        return result;
    }
};
struct SyntheticResource
{
    std::shared_ptr<SourceFixture> f;
    bool allow=true,submitted=false,reusable=false,closed=false;unsigned acquireCalls=0;
    struct Guard {SourceFixture& f;auto Proof()const{return f.Proof();}};
    const auto& Endpoint()const{return *f->store.Resolve(f->boundary.output).value;}
    const auto& ConsumerRecording()const{return f->consumerRecording;}
    auto QueueIdentity()const{return &f->binding.description.queue.KnownPart()->value;}
    auto DispatchCall()const{return Key("sdk.call");}
    auto AcquireSourceBound(ID3D12Resource*,ID3D12GraphicsCommandList*,const DlssNr::GpuSafety::Ticket&,const L::SelectedFsr3DispatchAdmission&)
    {++acquireCalls;return allow?std::make_unique<Guard>(Guard{*f}):nullptr;}
    std::optional<L::SourceBoundSubmissionOutcome> ObserveSubmission()const
    {if(!submitted)return {};return L::SourceBoundSubmissionOutcome{DispatchCall(),ConsumerRecording(),Key("sdk.submit"),*QueueIdentity(),L::Fact::Yes,L::Fact::Yes,L::Fact::Yes};}
    void CloseAdmission(){closed=true;L::CloseAdmission(*f->lease);}
    auto AcquireRetirement(){return closed&&reusable?std::make_unique<Guard>(Guard{*f}):nullptr;}
};
struct DiagnosticResource:SyntheticResource
{
    auto AcquireSourceBound(ID3D12Resource* r,ID3D12GraphicsCommandList* l,const DlssNr::GpuSafety::Ticket& t,const L::SelectedFsr3DispatchAdmission& s,uint32_t* diagnostic)
    {if(diagnostic&&!*diagnostic)*diagnostic=allow?0:107;return SyntheticResource::AcquireSourceBound(r,l,t,s);}
};
struct SyntheticSdk:L::SelectedFsr3DispatchAdmission
{bool Current(ID3D12Resource*,ID3D12GraphicsCommandList*,ID3D12CommandQueue*,const DlssNr::GpuSafety::Ticket&)const noexcept override{return true;}};
int main()try
{
    unsigned checks=0;auto check=[&](bool pass,const char* text){++checks;if(!pass)throw std::runtime_error(text);};
    const auto absent=L::ObserveFsr3SourceBoundPreflight(nullptr);
    check(absent.status==L::Fsr3SourceBoundPreflightStatus::MissingModule,"missing module cannot qualify");
    const auto system=L::ObserveFsr3SourceBoundPreflight(GetModuleHandleW(L"kernel32.dll"));
    check(system.status==L::Fsr3SourceBoundPreflightStatus::MissingQualifiedExport,"unrelated loaded module cannot qualify");
    for(unsigned scenario=0;scenario<3;++scenario)
    {
        auto f=std::make_shared<SourceFixture>();const auto claim=f->Reserve();check(bool(claim.claim),"source-bound claim reserved");
        check(f->Bind(*claim.claim).accepted,"actual finalizer candidate bound");
        auto session=std::make_shared<SyntheticSession>();session->f=f;
        auto resource=std::make_shared<SyntheticResource>();resource->f=f;
        auto source=L::SourceTransactionTestAccess::Observe(f->transaction);
        using Publication=L::Fsr3SourceBoundConsumerPublication<NrContextTests::Store,SyntheticResource,SyntheticSession>;
        auto publication=Publication::Create(session,*claim.claim,std::shared_ptr<const NrContextTests::Store>(f,&f->store),source,resource,f->boundary);
        check(bool(publication),"publication binds exact real finalizer transaction");
        SyntheticSdk sdk;
        if(scenario==2)resource->allow=false;
        check(publication->LastOfferFailure()==0,"new diagnostic is clear");
        const bool offered=publication->Offer(nullptr,nullptr,{},sdk);
        check(offered==(scenario!=2),"Resource refusal prevents finalizer authorization");
        if(!offered){
            check(publication->LastOfferFailure()==100,"legacy Resource refusal has distinct fallback diagnostic");
            check(session->applyCalls==0&&resource->acquireCalls==1,"Resource refusal never enters finalizer");
            check(!publication->Offer(nullptr,nullptr,{},sdk)&&publication->LastOfferFailure()==100,"first refusal survives attempted retry");
            check(resource->acquireCalls==1,"diagnostic and retry do not reacquire");
            publication->Advance();check(!publication->Retired(),"refused offer retains unresolved claim");continue;}
        check(publication->LastOfferFailure()==0,"accepted Offer diagnostic remains clear");
        check(!publication->Offer(nullptr,nullptr,{},sdk)&&publication->LastOfferFailure()==201,"duplicate accepted Offer reports state without retry");
        check(publication->DispatchCurrent(),"finalizer authorization rechecked before opaque entry");
        publication->DispatchReturned(scenario?L::SourceBoundDispatchEffect::InterruptedUnknown:L::SourceBoundDispatchEffect::ReturnedAccepted,scenario?std::nullopt:std::optional<std::int32_t>{0});
        check(!publication->DispatchCurrent(),"actual callback return closes dispatch admission");
        publication->Advance();check(!publication->Retired(),"no SDK submission cannot imply retirement");
        resource->submitted=true;publication->Advance();check(!publication->Retired(),"submitted work cannot imply provider release");
        f->facts.dependency.completed=L::Fact::Yes;f->facts.dependency.nonReplayable=L::Fact::Yes;
        f->facts.dependency.reusable=L::Fact::Yes;f->facts.consumerTicketsRetired=L::Fact::Yes;f->facts.ownerCanRecycle=L::Fact::Yes;
        for(std::size_t i=0;i<f->facts.holds.Size();++i){auto* hold=f->facts.holds.Get(i);hold->released=L::Fact::Yes;hold->releaseEvidence=Key("release",i+1);}
        resource->reusable=true;publication->Advance();
        check(publication->Retired(),"exact terminal Resource and provider facts retire existing claim");
        publication->Advance();check(publication->Retired(),"terminal observation idempotent");
        check(!f->Reserve().accepted,"retired source transaction cannot reserve twice");
        check(!f->Claim().accepted,"source-bound closure cannot mint canonical Strict identity");
    }
    for(unsigned scenario=0;scenario<3;++scenario)
    {
        auto f=std::make_shared<SourceFixture>();const auto claim=f->Reserve();check(bool(claim.claim),"diagnostic claim reserved");
        check(f->Bind(*claim.claim).accepted,"diagnostic candidate bound");
        auto session=std::make_shared<SyntheticSession>();session->f=f;
        auto resource=std::make_shared<DiagnosticResource>();resource->f=f;
        auto source=L::SourceTransactionTestAccess::Observe(f->transaction);
        using Publication=L::Fsr3SourceBoundConsumerPublication<NrContextTests::Store,DiagnosticResource,SyntheticSession>;
        auto publication=Publication::Create(session,*claim.claim,std::shared_ptr<const NrContextTests::Store>(f,&f->store),source,resource,f->boundary);
        check(bool(publication),"diagnostic publication created");SyntheticSdk sdk;
        if(scenario==0)resource->allow=false;
        if(scenario==1)f->consumerRecording={}; // Existing finalizer's actual InvalidRights refusal.
        if(scenario==2)session->omitAuthorization=true; // Preserve partial authorization effects when a response loses its token.
        check(!publication->Offer(nullptr,nullptr,{},sdk),"Offer decision unchanged on each refusal");
        const uint32_t expected=scenario==0?107:scenario==1?1000+static_cast<uint32_t>(L::FinalizationReason::InvalidRights):1000;
        check(publication->LastOfferFailure()==expected,"exact Resource or finalizer reason exposed");
        const auto calls=session->applyCalls;check(calls==(scenario==0?0u:1u),"exact existing finalizer call count");
        check(!publication->Offer(nullptr,nullptr,{},sdk)&&publication->LastOfferFailure()==expected,"failed Offer remains one shot with first reason");
        check(session->applyCalls==calls&&resource->acquireCalls==1,"read-only diagnostic cannot repeat partial effects");
        check(!publication->DispatchCurrent()&&!publication->Retired(),"failure grants neither dispatch nor terminal state");
    }
    // Exercise the production builder against the existing finalizer, including
    // contracts that the actual pre-HUD/pre-tonemap route cannot satisfy.
    for(unsigned scenario=0;scenario<6;++scenario)
    {
        auto f=std::make_shared<SourceFixture>();
        auto consumer=*f->store.Resolve(f->contract.consumer).value;
        consumer.hudIncluded=scenario==1;consumer.toneMapped=scenario==2;
        if(scenario==5)consumer.extent={1921,1080};
        f->contract.consumer=f->store.Publish(consumer,C::OwnerDomain::FrameGeneration);
        f->source.contract=f->store.Publish(f->contract,C::OwnerDomain::FrameGeneration);
        auto delivery=*f->store.Resolve(*f->source.recipe.nativeDelivery).value;
        delivery.finalConsumer=f->contract.consumer;
        f->source.recipe.nativeDelivery=f->store.Publish(delivery,C::OwnerDomain::StreamCoordinator);
        f->stage.delivery=*f->source.recipe.nativeDelivery;f->PublishStage();
        const auto claim=f->Reserve();check(bool(claim.claim),"production boundary claim reserved");
        check(f->Bind(*claim.claim).accepted,"production boundary candidate bound");
        const auto& content=*f->store.Resolve(*f->stage.returnedOutput).value;
        auto boundary=L::BuildControlledFgBoundary(f->boundary.owner,f->source.recipe,f->source.generations,
            content,f->stage,f->transaction,consumer);
        check(!boundary.boundary.hudIncluded.KnownPart()->value&&!boundary.boundary.toneMapped.KnownPart()->value,
            "production builder preserves actual route observations");
        if(scenario==3)boundary.boundary.hudIncluded={};
        if(scenario==4)++boundary.candidate.revision;
        auto result=Access::Authorize(*f->finalizer,*claim.claim,boundary,f->Proof(),f->store,f->Event());
        if(scenario==0){
            check(result.accepted&&result.authorization.has_value(),"production controlled boundary authorizes real finalizer");
            check(!f->Claim().accepted,"production boundary cannot promote canonical Strict");
        }else check(!result.accepted&&result.reason==L::FinalizationReason::InvalidBoundary,
            "unsupported or unknown actual boundary remains refused");
    }
    std::cout<<"Source-bound publication: "<<checks<<" checks, 0 failures; synthetic owners, no GPU/provider/game execution\n";return 0;
}
catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}
