#pragma once
#include "NativeEvaluationResult.h"
#include "NativeTemporalBindingMapper.h"
#include "NativeRouteAdmission.h"
#include <nr/lifecycle/ConsumptionLeaseAdapter.h>
#include <nr/lifecycle/NativeHistoryState.h>
#include <dlssnr/NativeOutputPassToken.h>

struct ID3D12Resource;
namespace Neurotic::Protocol
{
// This is the existing Resource owner's synchronous, call-scoped facade. The
// owner must hold its registration/realization lock through refresh AND the
// admitted native operation. Neither a decoded receipt nor a pointer lookup is
// a valid implementation. Prepared internal resources require their own C03.
class NativeOperationOwner
{
  public:
    virtual ~NativeOperationOwner()=default;
    virtual NativeLeaseProof Refresh(NativeCheckPoint,const NativeResourceUse&)noexcept=0;
    // Called only after the actual operation/barrier was recorded. The allocation
    // owner binds new content/PreparedView metadata to the reserved C12 realization
    // and current recording; it does not change the semantic plan. Failure retains
    // all referenced storage and must not be reported as an unattempted operation.
    virtual bool RecordProduced(NativeCheckPoint,const NativeResourceSet&)noexcept{return false;}
    virtual std::optional<C::MetadataRef<C::ResourceView>> RecordOutput(
        const DlssNr::NativeOutputPassToken&)noexcept{return {};}
    virtual bool RetainsReturnTransaction()const noexcept{return false;}
    virtual bool ProviderEntry()noexcept{return false;}
    // Called only when activation succeeded but the forwarder has not called
    // the provider. Earlier renderer recording remains owned separately.
    virtual void ProviderEntryCancelled()noexcept{}
    virtual void ProviderReturned(bool)noexcept{}
};
inline bool ValidateNativeLease(const NativeResourceUse& use,const NativeLeaseProof& proof,NativeCheckPoint point)
{
    if(!use.resource||proof.exactNative!=use.resource||!proof.lease||!proof.facts||
       proof.lease->Binding().purpose!=use.purpose)return false;
    const auto& binding=proof.lease->Binding();const auto usage=binding.description.use;
    if(!binding.dependencyRequired||binding.cpuReadback||
       (use.write?usage!=C::UsageKind::WriteExclusive:
        (usage!=C::UsageKind::OrderedGpuRead&&usage!=C::UsageKind::RetainedProviderConsumption)))return false;
    if(point==NativeCheckPoint::BeforeModel)
    {
        if(!use.write&&usage!=C::UsageKind::RetainedProviderConsumption)return false;
        // Before entry, exact reserved registrations cover all reads and writes.
        // Activation happens only at the provider seam. Existing active-provider
        // owner facts remain supported for independently enrolled integrations.
        bool retained=false;
        if(const auto& reserved=proof.facts->providerReservation;reserved&&
           reserved->registration.Check()==C::Error::None&&reserved->feature.Check()==C::Error::None&&
           Context::SameResourceStructure(reserved->target,binding.description.resource)&&
           (use.write||(binding.sameRecording&&binding.sameRecording->providerRegistration==reserved->registration)))retained=true;
        for(const auto& hold:proof.facts->holds)
            if(hold.kind==Lifecycle::HoldKind::Provider&&hold.consumer==binding.consumer&&
               hold.released==Lifecycle::Fact::No&&Lifecycle::ValidHold(hold,binding.description.resource))retained=true;
        if(!retained)return false;
    }
    return (point==NativeCheckPoint::BeforeSubmission?Lifecycle::ValidateForSubmit(*proof.lease,*proof.facts):
        Lifecycle::ValidateForRecording(*proof.lease,*proof.facts)).allowed;
}
class NativeExecutionObserver
{
    NativeOperationOwner* owner_=nullptr;
    NativeExecutionFacts facts_;
    Lifecycle::NativeHistoryState* history_=nullptr;
    C::EvaluationId evaluation_;
    std::optional<C::NativeSampleIdentityV1> sample_;
    C::OptionalFact<C::GenerationToken> currentHistory_;
    bool awaitingOwnerDelivery_=false;
    NativeInvocationAdmission* admission_=nullptr;
    std::unique_ptr<NativeOwnerActionGuard> action_;
    NativeCheckPoint actionPoint_=NativeCheckPoint::BeforeRecord;
  public:
    explicit NativeExecutionObserver(NativeOperationOwner* owner=nullptr,Lifecycle::NativeHistoryState* history=nullptr,
        const C::EvaluationId& evaluation={},const C::OptionalFact<C::GenerationToken>& currentHistory={}):
        owner_(owner),history_(history),evaluation_(evaluation),currentHistory_(currentHistory){}
    NativeExecutionObserver(NativeInvocationAdmission& admission,NativeOperationOwner& owner,
        Lifecycle::NativeHistoryState* history=nullptr):owner_(&owner),history_(history),admission_(&admission)
    {
        sample_=admission.Sample();
        if(const auto* product=admission.Product())evaluation_=product->recipe.evaluation;
        else Fail("Native.AdmissionUnavailable");
        if(!sample_)Fail("Native.SampleUnavailable");
    }
    bool BoundTo(const NativeInvocationAdmission& admission)const noexcept{return admission_==&admission;}
    NativeExecutionObserver(const NativeExecutionObserver&)=delete;
    NativeExecutionObserver& operator=(const NativeExecutionObserver&)=delete;
    ~NativeExecutionObserver()
    {if(history_&&facts_.modelAttempted&&!awaitingOwnerDelivery_)history_->Abandon(evaluation_);}
    bool RequiresReset()const noexcept{return history_&&history_->RequiresReset();}
    bool CheckEncode(const NativeEncodeControls& actual)noexcept
    {
        const auto* product=admission_?admission_->Product():nullptr;
        const auto expected=product?ReadNativeEncodeBindings(product->bindingPlan):std::nullopt;
        if(!facts_.failure.Empty()||!expected||*expected!=actual)
        {Fail("Native.EncodeContractMismatch");return false;}
        return true;
    }
    bool Check(NativeCheckPoint point,const NativeResourceSet& resources)noexcept
    {
        if(!facts_.failure.Empty())return false;
        if(!owner_||resources.Size()==0){Fail("Native.OwnerUnavailable");return false;}
        if(admission_)
        {
            action_.reset();
            try{action_=admission_->Action(point);}catch(...){Fail("Native.OwnerGuardFailure");return false;}
            if(!action_){Fail("Native.ActionUnavailable");return false;}
            actionPoint_=point;
        }
        for(const auto& use:resources)
        {
            const auto proof=action_?action_->Refresh(point,use):owner_->Refresh(point,use);
            if(action_)
            {
                // The admission snapshot fixes source, structure, route and
                // recording. A renderer write can issue a newer physical
                // content revision only at its actual recorded boundary.
                const auto* issued=proof.lease?&proof.lease->Binding():nullptr;
                bool enrolled=false;
                if(issued)for(const auto& baseline:action_->Current().leases)
                    if(baseline.purpose==use.purpose&&baseline.consumer==issued->consumer&&
                       baseline.plan==issued->plan&&baseline.profile==issued->profile&&
                       baseline.route==issued->route&&baseline.description.context==issued->description.context&&
                       baseline.description.candidate==issued->description.candidate&&
                       baseline.description.commandScope==issued->description.commandScope&&
                       baseline.sameRecording&&issued->sameRecording&&
                       baseline.sameRecording->recording==issued->sameRecording->recording&&
                       baseline.sameRecording->reservation==issued->sameRecording->reservation&&
                       Context::SameResourceStructure(baseline.description.resource,issued->description.resource))
                        enrolled=true;
                if(!enrolled||!proof.facts||!Lifecycle::CheckBinding(*issued,*proof.facts).allowed)
                {Fail("Native.ExactOperationRejected");return false;}
            }
            if(!ValidateNativeLease(use,proof,point)){Fail("Native.RightsRejected");return false;}
        }
        return true;
    }
    void Recorded()noexcept{facts_.commandsRecorded=true;}
    void ObserveOutputPass(const DlssNr::NativeDispatchOutcome& outcome)noexcept
    {
        // Notification is one-shot at the real pass boundary. Even a conflicting
        // notification cannot erase possible effects already observed.
        if(outcome.Effect()>=DlssNr::NativeDispatchEffect::DispatchPossible)Recorded();
        if(!outcome.CommandList()||!outcome.Target()||!outcome.Width()||!outcome.Height())
        {Fail("Native.OutputPassInvalid");return;}
        if(facts_.outputPass)
        {Fail("Native.OutputPassRepeated");return;}
        facts_.outputPass=outcome;
    }
    bool Produced(NativeCheckPoint point,const NativeResourceSet& resources)noexcept
    {
        Recorded();
        if(!owner_||resources.Size()==0||!owner_->RecordProduced(point,resources))
        {Fail("Native.RealizationPublicationFailed");return false;}
        return true;
    }
    bool OutputProduced(const DlssNr::NativeOutputPassToken& pass)noexcept
    {
        Recorded();
        if(!owner_||!admission_||!action_||actionPoint_!=NativeCheckPoint::BeforeComposition||
           !facts_.failure.Empty()||!facts_.modelSucceeded)
        {Fail("Native.OutputPublicationUnavailable");return false;}
        const auto output=owner_->RecordOutput(pass);
        if(!output){Fail("Native.OutputPublicationFailed");return false;}
        facts_.output=C::OptionalFact<C::MetadataRef<C::ResourceView>>::FromKnown(*output,C::EvidenceRef{output->record});
        facts_.composition=C::CompositionStatus::Accepted;return true;
    }
  private:
    bool RecordModelAttempt(bool resetRequested)noexcept
    {
        if(facts_.modelAttempted||!facts_.failure.Empty()||
           (history_&&!history_->Attempt(evaluation_,resetRequested,currentHistory_,sample_)))
        {Fail("Native.HistoryAdmissionRejected");return false;}
        facts_.commandsRecorded=true;facts_.modelAttempted=true;facts_.actualResetRequested=resetRequested;return true;
    }
  public:
    // CPU evidence observer only. An admitted production observer must cross
    // StartAuthorizedModel at the actual renderer seam.
    bool ModelAttempt(bool resetRequested=false)noexcept
    {return !admission_&&RecordModelAttempt(resetRequested);}
    // Only this method is used at the actual candidate model seam. The guard
    // remains held across the immediate model call and its result publication.
    bool StartAuthorizedModel(bool resetRequested=false)noexcept
    {
        if(!admission_||!action_||actionPoint_!=NativeCheckPoint::BeforeModel||facts_.modelAttempted||
            !facts_.failure.Empty()||!admission_->StartModel(*action_))
        {Fail("Native.ModelStartRevoked");return false;}
        if(history_&&!history_->Attempt(evaluation_,resetRequested,currentHistory_,sample_))
        {Fail("Native.HistoryAdmissionRejected");return false;}
        if(!admission_->ModelCalled())
        {
            if(history_)history_->Abandon(evaluation_);
            Fail("Native.ModelCallRejected");return false;
        }
        facts_.commandsRecorded=true;facts_.modelAttempted=true;facts_.actualResetRequested=resetRequested;
        return true;
    }
    void AwaitFinalConsumption()noexcept
    {if(facts_.modelAttempted&&facts_.modelSucceeded&&facts_.failure.Empty())awaitingOwnerDelivery_=true;}
    // Called by the forwarder immediately before the provider function call.
    bool ProviderEntry(bool reset)noexcept
    {
        if(!admission_||!action_||actionPoint_!=NativeCheckPoint::BeforeModel||facts_.modelAttempted||
            !facts_.failure.Empty())
        {Fail("Native.ModelStartRevoked");return false;}
        if(!owner_||!owner_->ProviderEntry()){Fail("ProviderLifetimeContractUnavailable");return false;}
        if(!StartAuthorizedModel(reset))
        {owner_->ProviderEntryCancelled();return false;}
        return true;
    }
    void ProviderReturned(bool success)noexcept{if(owner_)owner_->ProviderReturned(success);}
    void AwaitHostReturn()noexcept
    {if(owner_&&owner_->RetainsReturnTransaction()&&facts_.modelAttempted&&facts_.modelSucceeded&&facts_.failure.Empty())awaitingOwnerDelivery_=true;}
    void ModelResult(bool success,const C::OptionalFact<C::GenerationToken>& postAttemptHistory={})noexcept
    {
        if(admission_)admission_->ModelReturned();
        if(history_&&!history_->ModelReturned(evaluation_,postAttemptHistory,success)){Fail("Native.HistoryReturnRejected");return;}
        facts_.modelSucceeded=success;if(!success)Fail("Native.ModelFailed");
    }
    void Composed()noexcept{facts_.commandsRecorded=true;facts_.composition=C::CompositionStatus::Recorded;}
    bool CompositionRecorded()const noexcept
    {return facts_.failure.Empty()&&facts_.modelSucceeded&&(facts_.composition==C::CompositionStatus::Recorded||facts_.composition==C::CompositionStatus::Accepted);}
    void Fail(std::string_view reason)noexcept{if(facts_.failure.Empty())facts_.failure.Assign(reason);}
    void OutputRestoreFailed()noexcept
    {facts_.outputRestoreFailed=true;Fail("Native.OutputRestoreFailed");}
    void CommandStateRestoreFailed()noexcept{NativeMarkCommandStateRestoreFailed(facts_);}
    const NativeExecutionFacts& Facts()const noexcept{return facts_;}
};
inline NativeResourceSet NativeResources(std::initializer_list<NativeResourceUse> uses)
{
    NativeResourceSet result;for(const auto& use:uses)if(!result.Push(use))return {};return result;
}
}
