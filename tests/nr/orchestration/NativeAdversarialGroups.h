#pragma once
#include "InitialInvocationFixture.h"
#include "NativeAdversarialConfig.h"
#include "NativeAdversarialSample.h"
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
#include <nr/diagnostics/ContractObservation.h>
#include <type_traits>
namespace NativeAdversarialGroups
{
using namespace InitialNativeTests;
struct NoRawRights final:P::NativeOperationOwner
{P::NativeLeaseProof Refresh(P::NativeCheckPoint,const P::NativeResourceUse&)noexcept override{return {};}};
inline P::NativeResourceSet Uses()
{return P::NativeResources({{Sym("ColorModelInput"),reinterpret_cast<ID3D12Resource*>(std::uintptr_t{16}),false}});}

inline void Selection(Report& r)
{
    for(bool before:{false,true})
    {
        NativeAdversarialConfig::TestConfig source;source.DlssNrRunBeforeSr=before;source.DlssNrRoute=0;
        const auto frozen=TryNrConfigSnapshot(source,false);
        const bool valid=frozen.has_value();unsigned candidate=0,original=0;bool selected=false;
        const auto seam=[&](bool callbackBefore){
            if(frozen&&P::SelectNativePath(frozen->DlssNrNativeProtocol.value_or_default(),
                frozen->DlssNrRoute.value_or_default(),frozen->DlssNrRunBeforeSr.value_or_default(),callbackBefore)==P::NativePath::Candidate)
            {++candidate;selected=callbackBefore;}
        };
        seam(true);source.DlssNrRunBeforeSr=!before;source.DlssNrRoute=2;++original;seam(false);
        const auto next=TryNrConfigSnapshot(source,false);
        r.Check(before?"selection_frozen_before":"selection_frozen_after",valid&&candidate==1&&original==1&&selected==before&&
            next&&next->DlssNrRunBeforeSr.value_or_default()!=frozen->DlssNrRunBeforeSr.value_or_default()&&
            next->DlssNrRoute.value_or_default()==2,true,
            "N05 actual synchronized snapshot owns pre/post selection across an exterior original-call shim and later settings edit");
    }
}

inline void ClosedTickets(Report& r)
{
    NativeTests::Fixture sampleFixture(false,true);sampleFixture.AsNativeSample(1);
    auto sample=*S::ResolveNativeSample(sampleFixture.frame,sampleFixture.store);
    L::NativeExecutionLedger ledger;auto first=ledger.Reserve(sample);
    const bool used=first&&ledger.BeginClaim(*first)&&ledger.Authorize(*first)&&ledger.StartModel(*first)&&ledger.MarkModelCalled(*first);
    if(first)ledger.MarkModelReturned(*first);
    const bool retired=first&&ledger.Retire(*first);
    ++sample.producerOrdinal;++sample.callback.value;auto fresh=ledger.Reserve(sample);
    const bool reuse=first&&fresh&&first->slot==fresh->slot&&first->revision!=fresh->revision;
    const bool stale=first&&!ledger.Contains(*first)&&!ledger.BeginClaim(*first)&&!ledger.Authorize(*first)&&
        !ledger.StartModel(*first)&&!ledger.MarkModelCalled(*first)&&!ledger.Retire(*first);
    r.Check("late_ticket_after_reuse_refused",used&&retired&&reuse&&stale,true,
        "L13 retain old capability across actual slot reuse; every authority method rejects the old revision");
    const bool newCall=fresh&&ledger.BeginClaim(*fresh)&&ledger.Authorize(*fresh)&&ledger.StartModel(*fresh)&&ledger.MarkModelCalled(*fresh);
    if(fresh)ledger.MarkModelReturned(*fresh);
    const bool freshRetired=fresh&&ledger.Retire(*fresh);--sample.producerOrdinal;--sample.callback.value;
    r.Check("new_ticket_and_closed_prefix",newCall&&freshRetired&&!ledger.Reserve(sample),true,
        "L13 exact new revision calls once and closure retains old producer-prefix exclusion");
}

inline void SampleBindings(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    auto a=std::make_shared<SyntheticInvocation>(f.root.get(),10);auto admitted=Access::Admit(f.root,a);
    auto b=std::make_shared<SyntheticInvocation>(f.root.get(),11);auto other=Access::Admit(f.root,b);
    const auto* aProduct=admitted?admitted->Product():nullptr;
    const auto* bProduct=other?other->Product():nullptr;
    const auto aSample=admitted?admitted->Sample():std::nullopt;
    const auto bSample=other?other->Sample():std::nullopt;
    const auto map=aProduct?P::BuildNativeBindingMap(*aProduct,*a->store):std::nullopt;
    r.Check("same_sample_exact_recipe_resources",admitted&&other&&map&&map->resources.Size()==4&&
        aProduct&&bProduct&&aSample&&bSample&&!aSample->SameSample(*bSample),true,
        "N11 both samples independently qualify exact recipe/resource bindings in the production mapper");
    bool wrongRecipe=false,wrongUse=false,staleRef=false;
    if(aProduct&&bProduct&&aSample&&bSample)
    {
        const auto different=a->store->Publish(*bSample,C::OwnerDomain::Provider);
        auto p=*aProduct;p.recipe.nativeSample=different;
        wrongRecipe=!P::BuildNativeBindingMap(p,*a->store);
        auto qualified=std::make_unique<SampleFixture>();
        const bool positive=P::PrepareNativeProfile(qualified->request,qualified->store).projection.has_value();
        qualified->request.representations.Get(0)->nativeSample=qualified->store.Publish(*bSample,C::OwnerDomain::Provider);
        wrongUse=positive&&!P::PrepareNativeProfile(qualified->request,qualified->store).projection;
        p=*aProduct;++p.recipe.nativeSample->revision;staleRef=!P::BuildNativeBindingMap(p,*a->store);
    }
    r.Check("cross_sample_recipe_refused",wrongRecipe&&wrongUse&&staleRef,true,
        "N11 a different sample or stale publication cannot reuse a qualified recipe even with equal resource descriptions");
    r.Check("raw_values_convey_no_authority",!std::is_default_constructible_v<P::NativeInvocationAdmission>&&
        !std::is_copy_constructible_v<P::NativeInvocationAdmission>&&
        !std::is_constructible_v<P::NativeInvocationAdmission,P::RecipeProduct>&&
        !std::is_constructible_v<P::NativeInvocationAdmission,C::PlanCommit>,true,
        "N12 structural public API rejects authority construction from descriptive recipes and commits");
}

inline P::NativeProtocolResult Invoke(SyntheticInvocation& owner,P::NativeInvocationAdmission& admission,
    L::NativeHistoryState& history,unsigned& calls,bool reset,bool failModel,bool failPublication=false)
{
    NoRawRights raw;const auto header=NrContextTests::Header(C::ContractId::C07,C::OwnerDomain::Strategy,881);
    auto executor=[&](const P::NativeBindingMap&,P::NativeExecutionObserver& observer){
            if(!observer.Check(P::NativeCheckPoint::BeforeModel,Uses())||!observer.StartAuthorizedModel(reset))return observer.Facts();
            ++calls;observer.ModelResult(!failModel);
            auto facts=observer.Facts();facts.actualPurposes.Push(Sym("ColorModelInput"));
            owner.store->failPublication=failPublication;return facts;
        };
    return P::ExecuteNativeProtocol(admission,header,raw,*owner.store,executor,&history,true);
}

inline void FailureHistory(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    L::NativeHistoryState history(Key("failure.history.owner"),Key("failure.history"));unsigned calls=0;
    auto first=std::make_shared<SyntheticInvocation>(f.root.get(),10);auto a=Access::Admit(f.root,first);
    const bool ready=a&&first->EstablishSyntheticLease();P::NativeProtocolResult failed;
    if(ready)failed=Invoke(*first,*a,history,calls,false,true);
    auto same=std::make_shared<SyntheticInvocation>(f.root.get(),10);same->evaluationOverride=999;
    auto duplicate=Access::Admit(f.root,same);
    r.Check("failed_attempt_same_sample_not_retry",ready&&calls==1&&failed.facts.modelAttempted&&
        !failed.legacyAllowed&&!duplicate&&same->preparations==0&&history.Pending(),true,
        "O09 one actual failed model seam consumes the sample and retains reset debt across evaluation relabeling");
    auto next=std::make_shared<SyntheticInvocation>(f.root.get(),11);auto n=Access::Admit(f.root,next);
    const bool nextReady=n&&next->EstablishSyntheticLease();P::NativeProtocolResult noReset;
    if(nextReady)noReset=Invoke(*next,*n,history,calls,false,false);
    r.Check("failed_history_requires_next_reset",nextReady&&calls==1&&!noReset.facts.modelAttempted&&history.Pending(),true,
        "O09 next source sample is admitted but cannot enter its model without reset permission");
    auto resetOwner=std::make_shared<SyntheticInvocation>(f.root.get(),12);auto resetAdmission=Access::Admit(f.root,resetOwner);
    const bool resetReady=resetAdmission&&resetOwner->EstablishSyntheticLease();P::NativeProtocolResult reset;
    if(resetReady)reset=Invoke(*resetOwner,*resetAdmission,history,calls,true,false);
    r.Check("new_sample_reset_requested_not_ack",resetReady&&calls==2&&reset.facts.modelAttempted&&
        reset.facts.actualResetRequested==true&&history.Pending()&&!reset.legacyAllowed,true,
        "O09 genuine next sample can request reset; an opaque success supplies no affected-owner reset acknowledgment");
}

inline void OptionalDiagnostics(Report& r)
{
    namespace D=Neurotic::Diagnostics::M0;
    for(bool noisy:{false,true})
    {
        Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
        auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),10);owner->terminal=false;
        std::weak_ptr<SyntheticInvocation> weak=owner;auto a=Access::Admit(f.root,owner);
        L::NativeHistoryState history(Key("diag.history.owner"),Key("diag.history"));unsigned calls=0;
        D::Observer<2> diagnostic(D::Mode::Shadow);bool faultObserved=!noisy;
        if(noisy)
        {
            const auto thrown=diagnostic.ObserveLazy("Optional",[]()->D::SourceSnapshot{throw std::runtime_error("sink builder");});
            diagnostic.TryPublish("Optional",nullptr);
            D::ExportSink sink; sink.function=[](void*,const D::EventClosure&)noexcept{return D::ExportDisposition::SerializationFailed;};
            const bool drained=diagnostic.DrainOne(sink);
            faultObserved=thrown==D::PublishResult::ValidationRejected&&drained;
            Access::DiagnosticNoise(*f.root);
        }
        const bool ready=a&&owner->EstablishSyntheticLease();P::NativeProtocolResult result;
        if(ready)result=Invoke(*owner,*a,history,calls,false,false);
        a.reset();owner.reset();f.coordinator.Revoke(O::RevocationCause::Disabled);
        const auto held=f.root->AdvanceClosure();
        bool released=false;if(auto retained=weak.lock()){retained->terminal=true;retained.reset();released=f.root->AdvanceClosure().closed;}
        r.Check(noisy?"optional_diagnostics_failure_independent":"optional_diagnostics_control",ready&&faultObserved&&calls==1&&
            result.facts.modelAttempted&&history.Pending()&&!held.closed&&released&&weak.expired(),true,
            "O10 actual observer throw/drop and journal noise preserve model truth and retained owner tails through explicit exterior terminality");
    }
}

inline void PublicationFailure(Report& r)
{
    Fixture f;f.owner->deliveryKind=C::NativeDeliveryKind::HostReturn;f.Activate();
    auto owner=std::make_shared<SyntheticInvocation>(f.root.get(),10);owner->terminal=false;
    std::weak_ptr<SyntheticInvocation> weak=owner;auto a=Access::Admit(f.root,owner);
    L::NativeHistoryState history(Key("publication.history.owner"),Key("publication.history"));unsigned calls=0;
    const bool ready=a&&owner->EstablishSyntheticLease();P::NativeProtocolResult result;
    if(ready)result=Invoke(*owner,*a,history,calls,false,false,true);
    r.Check("post_call_publication_refusal_retained",ready&&calls==1&&result.facts.modelAttempted&&result.facts.commandsRecorded&&
        !result.evaluation&&!result.legacyAllowed&&result.reason.View()=="Native.OwnerFailure"&&history.Pending(),true,
        "O12 actual required result-list publication throws after the model seam; attempted truth and history debt survive");
    a.reset();owner.reset();f.coordinator.Revoke(O::RevocationCause::Disabled);
    const auto held=f.root->AdvanceClosure();const bool retained=!weak.expired();
    bool closed=false;if(auto tail=weak.lock()){tail->terminal=true;tail.reset();closed=f.root->AdvanceClosure().closed;}
    r.Check("publication_failure_owner_until_terminal",retained&&!held.closed&&!held.finalizerStopped&&closed&&weak.expired()&&history.Pending(),true,
        "O12 pre-existing operation slot retains failed metadata owner through exterior terminality; diagnostic result is unnecessary for retirement");
}
}
