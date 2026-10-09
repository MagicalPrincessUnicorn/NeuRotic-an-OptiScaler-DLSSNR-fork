#pragma once
#include "NativeInvocationOwner.h"
#include "SelectedFsr3ResourceOwner.h"
#include <dlssnr/NativeSelectedSrLifetimeLedger.h>
#include "../../../external/FidelityFX-CheckedClosure/CheckedAlgorithm.h"

namespace Neurotic::Lifecycle
{
// The private bridge consumes existing invocation state and SDK-owned objects.
// There is deliberately no constructor accepting caller-supplied C03 metadata.
class SelectedFsr3NativeLeaseOwner final:public NativeLeaseOwner,
    public std::enable_shared_from_this<SelectedFsr3NativeLeaseOwner>
{
    friend class NativeProcessBootstrap;
    friend class DlssNr::NativeDx12Source;
    std::shared_ptr<const void> root_;
    std::shared_ptr<const void> metadata_;
    NativeResourceRegistry& resources_;
    std::shared_ptr<DlssNr::NativeSelectedSrLifetimeLedger> sr_;
    DlssNr::NativeSrRetirementReceipt srReceipt_;
    std::shared_ptr<DlssNr::NativeProviderRetirementOwner> priorOwner_;
    std::shared_ptr<DlssNr::ProviderInvocationUse> priorUse_;
    std::shared_ptr<DlssNr::NativeFeatureRecordingUse> priorRecording_;
    DlssNr::NativePriorWriterClosure priorClosure_;
    std::shared_ptr<ffx::nr::OwnedOutputLease> output_;
    std::shared_ptr<ffx::nr::AlgorithmLease> algorithm_;
    ffx::nr::OwnedOutputSnapshot originalOutput_;
    ffx::nr::AlgorithmSnapshot originalAlgorithm_;
    FfxNrDispatchTicketV1 dispatch_;
    DlssNr::GpuSafety::Ticket producer_,consumer_;
    // The selected SDK may recycle its writer COM list for consumption. Keep
    // that exact list alive until its existing completion owner records SDK
    // exclusion; COM reference count alone cannot establish non-replayability.
    Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList> retainedConsumerList_;
    LeaseBinding binding_;
    C::ResourceView endpoint_;
    ExternalHold algorithmHold_;
    C::RecordKey releaseEvidence_;
    bool releasePublished_=false;
    C::RecordKey dispatchCall_,consumerRecording_;
    std::optional<SourceBoundSubmissionOutcome> submission_;
    C::ObjectIncarnation queueIdentity_;
    SelectedFsr3NativeLeaseOwner(NativeInvocationOwner& invocation,
        std::shared_ptr<DlssNr::NativeSelectedSrLifetimeLedger> sr,DlssNr::NativeSrRetirementReceipt srReceipt,
        DlssNr::NativePriorWriterClosure prior,std::shared_ptr<ffx::nr::OwnedOutputLease> output,
        std::shared_ptr<ffx::nr::AlgorithmLease> algorithm,const FfxNrDispatchTicketV1& dispatch,
        DlssNr::GpuSafety::Ticket consumer)
        :root_(invocation.retainedSourceState_),metadata_(invocation.store_),resources_(*invocation.resources_),
         sr_(std::move(sr)),srReceipt_(std::move(srReceipt)),priorOwner_(invocation.providerOwner_),
         priorUse_(invocation.providerUse_),priorRecording_(invocation.recordingUse_),priorClosure_(std::move(prior)),
         output_(std::move(output)),algorithm_(std::move(algorithm)),originalOutput_(output_->inspect()),
         originalAlgorithm_(algorithm_->inspect()),dispatch_(dispatch),producer_(invocation.ticket_),consumer_(std::move(consumer)),
         retainedConsumerList_(reinterpret_cast<ID3D12GraphicsCommandList*>(dispatch.command_list)){}
    static bool SameContext(const FfxNrContextTicketV1& a,const FfxNrContextTicketV1& b)noexcept
    {return a.owner_id==b.owner_id&&a.incarnation==b.incarnation&&a.registration_generation==b.registration_generation;}
    bool PriorCurrent()const
    {
        return sr_->CoversOutput(srReceipt_,originalOutput_.resource,producer_,endpoint_.identity)&&
            priorOwner_->ValidatePriorWriter(priorClosure_,priorUse_,priorRecording_,producer_,originalOutput_.resource);
    }
    bool AllocationCurrent(const ffx::nr::OwnedOutputSnapshot& output,const ffx::nr::AlgorithmSnapshot& algorithm)const noexcept
    {
        return algorithm_->Owns(output_)&&output.resource==originalOutput_.resource&&
            output.allocationGeneration==originalOutput_.allocationGeneration&&SameContext(output.context,originalOutput_.context)&&
            algorithm.input==output.resource&&algorithm.allocationGeneration==output.allocationGeneration&&
            algorithm.algorithmGeneration==originalAlgorithm_.algorithmGeneration&&SameContext(algorithm.swapchain,output.context)&&
            output.ordinaryEscapeExcluded&&output.recyclingExcluded&&algorithm.registrationCreated&&algorithm.registrationComplete;
    }
    bool ActionCurrent(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
        const DlssNr::GpuSafety::Ticket& ticket)const noexcept
    {
        const auto output=output_->inspect();const auto algorithm=algorithm_->inspect();
        return AllocationCurrent(output,algorithm)&&!output.failed&&!algorithm.failed&&!algorithm.released&&
            output.callbackActive&&output.admissionOpen&&output.actionActive&&resource==output.resource&&
            list==output.consumerList&&output.consumerRecording==dispatch_.recording_incarnation&&
            reinterpret_cast<uintptr_t>(list)==dispatch_.command_list&&reinterpret_cast<uintptr_t>(queue)==dispatch_.command_queue&&
            ticket==consumer_&&DlssNr::GpuSafety::MatchesRecording(ticket,list,queue);
    }
    std::optional<SourceBoundSubmissionOutcome> Submission()
    {
        if(submission_)return submission_;
        FfxNrSubmitResultV1 observed{};
        if(!output_->inspectSubmission(observed)||observed.size!=sizeof(observed)||observed.version!=1||
           observed.dispatch.dispatch_id!=dispatch_.dispatch_id||
           observed.dispatch.recording_incarnation!=dispatch_.recording_incarnation||
           observed.dispatch.command_list!=dispatch_.command_list||observed.dispatch.command_queue!=dispatch_.command_queue||
           observed.dispatch.present_color!=dispatch_.present_color||!SameContext(observed.dispatch.context,dispatch_.context))return {};
        const auto publication=resources_.PublishNativeLease(originalOutput_.resource,binding_.description.commandScope);
        if(!publication)return {};
        SourceBoundSubmissionOutcome result;
        result.call=dispatchCall_;result.recording=consumerRecording_;result.submission=publication->header.record;
        result.queue=queueIdentity_;
        result.executeInvoked=observed.execute_invoked==1?Fact::Yes:observed.execute_invoked==0?Fact::No:Fact::Unknown;
        result.signalSucceeded=observed.execute_invoked==1?(SUCCEEDED(static_cast<HRESULT>(observed.signal_result))?Fact::Yes:Fact::No):Fact::Unknown;
        // The exact producer was sealed, completed, uniquely submitted, and an
        // actual queue-owner wait succeeded before this consumer admission.
        result.dependencyWaitsSucceeded=Fact::Yes;
        submission_=result;return submission_;
    }
  public:
    class SdkAction final:public SelectedFsr3DispatchAdmission
    {
        friend class SelectedFsr3NativeLeaseOwner;
        std::shared_ptr<SelectedFsr3NativeLeaseOwner> owner_;
        explicit SdkAction(std::shared_ptr<SelectedFsr3NativeLeaseOwner> owner):owner_(std::move(owner)){}
      public:
        ~SdkAction(){owner_->output_->endAction();}
        bool Current(ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue,
            const DlssNr::GpuSafety::Ticket& ticket)const noexcept override
        {return owner_->ActionCurrent(resource,list,queue,ticket);}
    };
    struct Selection
    {
        std::shared_ptr<SelectedFsr3NativeLeaseOwner> owner;
        std::shared_ptr<SelectedFsr3ResourceOwner> resource;
        std::unique_ptr<SdkAction> action;
    };
    std::unique_ptr<SdkAction> AcquireSdkAction(const FfxNrDispatchTicketV1& ticket)
    {
        if(ticket.dispatch_id!=dispatch_.dispatch_id||ticket.recording_incarnation!=dispatch_.recording_incarnation||
           ticket.command_list!=dispatch_.command_list||ticket.command_queue!=dispatch_.command_queue||
           ticket.present_color!=dispatch_.present_color||!SameContext(ticket.context,dispatch_.context)||
           !output_->beginConsumerAction(ticket))return {};
        return std::unique_ptr<SdkAction>(new SdkAction(shared_from_this()));
    }
  private:
    std::optional<DlssNr::GpuSafety::TerminalRecordingEvidence> RetireOwnedConsumerRecording()
    {
        if(auto terminal=DlssNr::GpuSafety::InspectTerminalRecording(consumer_))return terminal;
        const auto output=output_->inspect();const auto algorithm=algorithm_->inspect();
        const auto retired=output_->inspectRetirement();
        if(!AllocationCurrent(output,algorithm)||!PriorCurrent()||output.failed||algorithm.failed||
           !retired.admissionClosed||!retired.actionsQuiescent||!retired.sdkComplete||retired.failed||
           !algorithm.admissionClosed||!algorithm.destroyAttempted||!algorithm.released||
           output.callbackActive||output.actionActive||output.consumerList!=retainedConsumerList_.Get()||
           output.consumerRecording!=dispatch_.recording_incarnation)return {};
        FfxNrSubmitResultV1 submitted{};
        if(!output_->inspectSubmission(submitted)||submitted.size!=sizeof(submitted)||submitted.version!=1||
           submitted.dispatch.dispatch_id!=dispatch_.dispatch_id||submitted.dispatch.recording_incarnation!=dispatch_.recording_incarnation||
           submitted.dispatch.command_list!=dispatch_.command_list||submitted.dispatch.command_queue!=dispatch_.command_queue||
           submitted.dispatch.present_color!=dispatch_.present_color||!SameContext(submitted.dispatch.context,dispatch_.context)||
           submitted.execute_invoked!=1)return {};
        auto* queue=reinterpret_cast<ID3D12CommandQueue*>(dispatch_.command_queue);
        if(!DlssNr::GpuSafety::MatchesRecording(consumer_,retainedConsumerList_.Get(),queue))return {};
        // This is the authenticated creating SDK's permanent exclusion of its
        // owned list after frozen-prefix drainage/consume, not a claimed Reset.
        // GPU completion remains independently checked by GpuSafety.
        if(!DlssNr::GpuSafety::SealOwnedRecording(retainedConsumerList_.Get()))return {};
        return DlssNr::GpuSafety::InspectTerminalRecording(consumer_);
    }
    // Dormant source-owner construction: no current product caller has a
    // qualified pre-enrollment binding these exact SDK objects to the C04
    // contract. Keep this private until that independent qualification exists.
    static std::optional<Selection> Create(NativeInvocationOwner& invocation,
        std::shared_ptr<DlssNr::NativeSelectedSrLifetimeLedger> sr,std::shared_ptr<ffx::nr::OwnedOutputLease> output,
        std::shared_ptr<ffx::nr::AlgorithmLease> algorithm,const FfxNrDispatchTicketV1& dispatch,
        DlssNr::GpuSafety::Ticket consumer,std::uint32_t& refusal)
    {
        // Identify the first failed existing predicate without repeating an
        // owner action or weakening any admission condition. See the packaged
        // CONSUMER_ADMISSION_CODES.md; zero is retained on success.
        const auto refuse=[&](std::uint32_t code)->std::optional<Selection>{if(!refusal)refusal=code;return {};};
        if(!sr||!output||!algorithm||!consumer||!invocation.closed_||invocation.activeActions_||invocation.returnTailActive_||
           !invocation.retainedSourceState_||!invocation.snapshot_||!invocation.snapshot_->SourceBoundRequest()||
           !invocation.store_||!invocation.resources_||!invocation.providerOwner_||!invocation.publishedOutput_||
           invocation.snapshot_->product.recipe.placement!=C::Placement::NativeAfter)return refuse(1);
        const auto stage=invocation.CommittedStage();
        if(!stage||stage->outcome!=C::NativeStageOutcome::HostReturnRecorded||
           stage->returnedOutput!=invocation.publishedOutput_)return refuse(2);
        const auto* content=Context::ResolveMetadata(*invocation.publishedOutput_,*invocation.store_);
        const auto* endpoint=content?Context::ResolveNativeOutput(*content,*invocation.store_):nullptr;
        const auto* contract=Context::ResolveMetadata(invocation.snapshot_->SourceBoundRequest()->contract,*invocation.store_);
        const auto* finalConsumer=contract?Context::ResolveMetadata(contract->consumer,*invocation.store_):nullptr;
        if(!endpoint||!finalConsumer||finalConsumer->Check()!=C::Error::None)return refuse(3);
        const auto original=output->inspect();const auto fg=algorithm->inspect();
        if(!algorithm->Owns(output)||!fg.registrationCreated||!fg.registrationComplete||fg.failed||fg.released||
           original.failed||!original.writerSubmitted||!original.writerRetired||!original.ordinaryEscapeExcluded||!original.recyclingExcluded||
           original.resource!=invocation.outputTarget_||fg.input!=original.resource||
           fg.allocationGeneration!=original.allocationGeneration||!SameContext(fg.swapchain,original.context)||
           !SameContext(dispatch.context,original.context)||dispatch.present_color!=reinterpret_cast<uintptr_t>(original.resource))return refuse(4);
        const auto srReceipt=sr->Retirement();
        if(!srReceipt||!sr->CoversOutput(*srReceipt,original.resource,invocation.ticket_,endpoint->identity))return refuse(5);
        auto prior=invocation.providerOwner_->ClosePriorWriter(invocation.providerUse_,invocation.recordingUse_,
            invocation.ticket_,original.resource);if(!prior)return refuse(6);
        auto owner=std::shared_ptr<SelectedFsr3NativeLeaseOwner>(new SelectedFsr3NativeLeaseOwner(invocation,
            std::move(sr),*srReceipt,std::move(*prior),std::move(output),std::move(algorithm),dispatch,std::move(consumer)));
        owner->endpoint_=*endpoint;auto sdk=owner->AcquireSdkAction(dispatch);if(!sdk)return refuse(7);
        auto* queue=reinterpret_cast<ID3D12CommandQueue*>(dispatch.command_queue);
        auto* list=reinterpret_cast<ID3D12GraphicsCommandList*>(dispatch.command_list);
        if(!sdk->Current(original.resource,list,queue,owner->consumer_))return refuse(8);
        // Pin the SDK action before Resource lock; refusal releases it after the
        // lock. The actual queue owner establishes cross-queue order if needed.
        auto lock=invocation.resources_->LockNativeAction();
        auto current=invocation.resources_->CurrentRegisteredView(original.resource);
        if(!current||*current!=*endpoint)return refuse(9);
        const auto queueIdentity=invocation.resources_->ObserveQueue(queue);
        const auto producerQueue=invocation.resources_->ObserveQueue(original.queue);
        if(!queueIdentity.IsKnown()||!producerQueue.IsKnown())return refuse(10);
        auto produced=DlssNr::GpuSafety::InspectRecording(invocation.ticket_,queue,true);
        if(!produced.valid||!produced.registryHealthy||!produced.uniqueSubmission||!produced.completed||
           !produced.nonReplayable||!produced.orderedForConsumer)return refuse(11);
        const NativeInvocationOwner::Slot* slot=nullptr;
        for(std::size_t i=0;i<invocation.slotCount_;++i)if(invocation.slots_[i].native==original.resource&&
            invocation.slots_[i].purpose.View()=="Native.ComposeTarget")
        {if(slot)return refuse(13);slot=&invocation.slots_[i];}
        if(!slot)return refuse(12);
        const auto issue=[&](){return invocation.resources_->PublishNativeLease(original.resource,invocation.scope_);};
        const auto lease=issue(),dependency=issue(),recording=issue(),registration=issue(),call=issue(),submission=issue();
        if(!lease||!dependency||!recording||!registration||!call||!submission)return refuse(14);
        auto& binding=owner->binding_;auto& description=binding.description;
        description.header=lease->header;description.context=invocation.snapshot_->product.recipe.context;
        description.candidate=slot->candidate;description.resource=endpoint->identity;
        description.use=C::UsageKind::RetainedProviderConsumption;description.commandScope=invocation.scope_;
        description.device=endpoint->device;description.queue=queueIdentity;description.generations=invocation.snapshot_->generations;
        const C::EvidenceRef evidence{lease->header.record};
        description.revoked=C::OptionalFact<bool>::FromKnown(false,evidence);
        description.revocationEpoch=C::OptionalFact<C::RecordKey>::FromKnown(registration->header.record,evidence);
        description.expirationBoundary=C::OptionalFact<C::RecordKey>::FromKnown(call->header.record,evidence);
        description.submissionRecheckRequired=C::OptionalFact<bool>::FromKnown(true,evidence);
        C::ContractRef<C::ContractId::C03> dependencyRef;dependencyRef.recordType.Assign(C::DependencyProof::WireName);
        dependencyRef.record=dependency->header.record;dependencyRef.revision=dependency->header.revision;
        description.producerDependency=C::OptionalFact<C::ContractRef<C::ContractId::C03>>::FromKnown(dependencyRef,evidence);
        binding.consumer=call->header.record;binding.purpose.Assign("ExternalProviderConsume");
        binding.plan=invocation.snapshot_->product.recipe.committedPlan.record;
        binding.profile=invocation.snapshot_->product.profileCertificate.header.record;
        binding.route=C::GenerationToken{invocation.snapshot_->generation.Describe()};binding.callbackScoped=true;
        if(!ValidBinding(binding))return refuse(15);
        owner->algorithmHold_={registration->header.record,binding.consumer,endpoint->identity,HoldKind::Provider,
            finalConsumer->provider,C::GenerationToken{finalConsumer->handoff.Describe()},Fact::No,{}};
        C::DependencyProof proof;proof.header=dependency->header;proof.producerDevice=endpoint->device;
        proof.producerQueue=producerQueue;proof.consumerQueue=queueIdentity;proof.generations=description.generations;
        const C::EvidenceRef observed{proof.header.record};
        proof.recording=C::OptionalFact<C::RecordKey>::FromKnown(invocation.seeds_.recording,observed);
        proof.submission=C::OptionalFact<C::RecordKey>::FromKnown(submission->header.record,observed);
        proof.ownerDependency=C::OptionalFact<C::RecordKey>::FromKnown(proof.header.record,observed);
        proof.orderedForConsumer=C::OptionalFact<bool>::FromKnown(true,observed);
        proof.gpuCompleted=C::OptionalFact<bool>::FromKnown(true,observed);
        proof.recordingNonReplayable=C::OptionalFact<bool>::FromKnown(true,observed);
        proof.providerReleased=C::OptionalFact<bool>::FromKnown(false,observed);
        proof.storageReusable=C::OptionalFact<bool>::FromKnown(false,observed);
        auto resource=std::shared_ptr<SelectedFsr3ResourceOwner>(new SelectedFsr3ResourceOwner(owner->root_,
            invocation.owners_->Resources(),owner,original.resource,list,queue,owner->producer_,owner->consumer_,
            *endpoint,proof,recording->header.record,!produced.sameQueue));
        resource->dispatchCall_=call->header.record;
        owner->dispatchCall_=call->header.record;owner->consumerRecording_=recording->header.record;
        owner->queueIdentity_=queueIdentity.KnownPart()->value;
        resource->observeSubmission_=[weak=std::weak_ptr<SelectedFsr3NativeLeaseOwner>(owner)]()
            ->std::optional<SourceBoundSubmissionOutcome>{auto held=weak.lock();return held?held->Submission():std::nullopt;};
        return Selection{std::move(owner),std::move(resource),std::move(sdk)};
    }
  public:
    std::optional<LeaseBinding> Describe(ID3D12Resource* resource)override
    {return resource==originalOutput_.resource?std::optional<LeaseBinding>(binding_):std::nullopt;}
    OwnerFacts Refresh(const LeaseBinding& binding,ID3D12Resource* resource,ID3D12GraphicsCommandList* list,ID3D12CommandQueue* queue)override
    {
        OwnerFacts facts;facts.current=binding_;
        const auto output=output_->inspect();const auto algorithm=algorithm_->inspect();
        const auto current=resources_.CurrentRegisteredView(resource);
        const bool allocation=resource==originalOutput_.resource&&AllocationCurrent(output,algorithm)&&current&&*current==endpoint_;
        const bool prior=allocation&&PriorCurrent();
        const bool live=prior&&SameFinalFrameBinding(binding,binding_)&&ActionCurrent(resource,list,queue,consumer_);
        facts.permission=live?Fact::Yes:Fact::No;facts.callbackActive=live?Fact::Yes:Fact::No;
        facts.registryHealthy=allocation&&!output.failed?Fact::Yes:Fact::No;
        facts.nativeBindingMatches=allocation?Fact::Yes:Fact::No;facts.apiCompatible=allocation?Fact::Yes:Fact::No;
        facts.resourcesRegistered=allocation?Fact::Yes:Fact::No;
        facts.descriptorsProtected=allocation?Fact::Yes:Fact::No;
        facts.externalRegistrationsComplete=prior&&!algorithm.failed?Fact::Yes:Fact::Unknown;
        auto hold=algorithmHold_;
        if(algorithm.failed)hold.released=Fact::Unknown;
        else if(algorithm.released&&algorithm.destroyAttempted&&algorithm.admissionClosed)
        {
            if(!releasePublished_)
            {
                const auto publication=resources_.PublishNativeLease(resource,binding_.description.commandScope);
                if(publication){releaseEvidence_=publication->header.record;releasePublished_=true;}
            }
            hold.released=releasePublished_?Fact::Yes:Fact::Unknown;
            if(releasePublished_)hold.releaseEvidence=releaseEvidence_;
        }
        facts.holds.Push(hold);
        const auto retired=output_->inspectRetirement();
        facts.ownerCanRecycle=prior&&retired.admissionClosed&&retired.actionsQuiescent&&retired.sdkComplete&&
            !retired.failed&&hold.released==Fact::Yes?Fact::Yes:Fact::No;
        return facts;
    }
};
}
