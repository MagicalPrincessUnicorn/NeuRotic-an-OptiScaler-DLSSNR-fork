#pragma once
#include "NativeInitialMaterialization.h"
#include "NativeReturnTransaction.h"
#include <nr/protocol/NativeTemporalProtocolAdapter.h>
#include <array>
#include <atomic>
#include <functional>

namespace Neurotic::Lifecycle
{
// Private process ingress. Its only constructor is called by the authentic
// NativeProcessBootstrap callback after renderer preparation has transferred
// the exact recording action. A sealed value never constructs this owner.
class NativeInvocationOwner final:public Protocol::NativeInvocationOwnerPort,
                                  public Protocol::NativeOperationOwner
{
    friend class NativeProcessBootstrap;
    friend class SelectedFsr3NativeLeaseOwner;
#ifdef NR_NATIVE_UNSEALED_OWNER_TESTING
    friend class NativeUnsealedOwnerTestAccess;
#endif
    using P=Protocol::NativeInvocationSnapshot;
    struct Slot
    {
        C::Symbol purpose;
        ID3D12Resource* native=nullptr;
        C::ContractRef<C::ContractId::C01> candidate;
        bool invocationInput=false;
        bool modelUse=false;
        C::RecordKey providerRegistration;
    };
    struct Seeds
    {
        C::RecordKey consumer,recording,reservation;
        C::HandoffContractGeneration handoff;
        std::array<C::RecordKey,4> providerRegistrations{};
        C::RecordKey history;
    };
    std::shared_ptr<NativeInitialMaterialization::Store> store_;
    std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> borrow_;
    std::shared_ptr<DlssNr::NativeNgxCallCapture> call_;
    NativeResourceRegistry* resources_=nullptr;
    NativeOwnerSet* owners_=nullptr;
    C::NativeSampleIdentityV1 sample_;
    C::ScopeRef scope_;
    Seeds seeds_;
    mutable std::mutex callbackMutex_;
    std::function<bool()> pinCurrent_;
    std::atomic<bool> closed_{false};
    DlssNr::GpuSafety::Ticket ticket_;
    std::shared_ptr<const P> snapshot_;
    C::Symbol preparationReason_=Protocol::Symbol("Native.Admission.SourceUnavailable");
    std::array<Slot,8> slots_{};
    std::size_t slotCount_=0;
    std::shared_ptr<NativeHistoryState> history_;
    std::shared_ptr<DlssNr::NativeProviderRetirementOwner> providerOwner_;
    std::shared_ptr<DlssNr::ProviderInvocationUse> providerUse_;
    std::shared_ptr<DlssNr::NativeFeatureRecordingUse> recordingUse_;
    std::atomic<unsigned> activeActions_{0};
    std::atomic<bool> returnTailActive_{false};
    std::optional<NativeResourceRegistry::OpaqueWriteReservation> outputWrite_;
    std::optional<OwnerMetadataArena::Reservation<C::ResourceView>> outputView_;
    std::optional<OwnerMetadataArena::Reservation<C::NativeOutputContentV1>> outputContent_;
    std::optional<C::MetadataRef<C::NativeOutputContentV1>> publishedOutput_;
    std::optional<OwnerMetadataArena::Reservation<C::ResourceView>> srOutputView_;
    std::optional<OwnerMetadataArena::Reservation<C::NativeOutputContentV1>> srOutputContent_;
    std::optional<C::MetadataRef<C::NativeOutputContentV1>> returnedSrOutput_,consumedSrInput_;
    ID3D12Resource* outputTarget_=nullptr;
    std::uint64_t recordedOrdinal_=0;
    C::Symbol outputDomain_;
    std::optional<C::MetadataRef<C::InterceptedSourceTransactionV1>> sourceBoundTransaction_;
    std::optional<C::MetadataRef<C::SourceBoundConsumerContractV1>> sourceBoundContract_;
    // Bootstrap selects this before preparation using publications from the
    // existing identity/qualification owners. No callback key is converted.
    bool SelectSourceBound(C::MetadataRef<C::InterceptedSourceTransactionV1> transaction,
        C::MetadataRef<C::SourceBoundConsumerContractV1> contract)
    {
        if(snapshot_||sourceBoundTransaction_||sourceBoundContract_)return false;
        sourceBoundTransaction_=transaction;sourceBoundContract_=contract;return true;
    }
    std::unique_ptr<NativeReturnTransaction> return_;
    // Retain the existing owner aggregate for asynchronous tails, without
    // retaining a CPU callback admission pin after the outer API returns.
    std::shared_ptr<const void> retainedSourceState_;
    std::unique_ptr<DlssNr::NativeRendererWriteToken> internalOutputWrite_;
    bool ReserveReturn(const C::RecordHeader& resultHeader,const C::ResourceView& target)
    {
        if(!snapshot_||!history_||return_||!snapshot_->product.recipe.nativeDelivery||
           !snapshot_->product.recipe.nativeSample)return false;
        const auto& recipe=snapshot_->product.recipe;
        C::NativeStageDeliveryV1 stage;
        stage.plan=recipe.committedPlan;stage.route=snapshot_->generation;
        stage.delivery=*recipe.nativeDelivery;stage.recipe=Protocol::Reference<C::ContractId::C06>(recipe);
        stage.executionResult={Protocol::Symbol(C::EvaluationResult::WireName),resultHeader.record,resultHeader.revision};
        stage.sample=*recipe.nativeSample;stage.evaluation=recipe.evaluation;
        stage.admissionEpoch=snapshot_->reservation;stage.placement=recipe.placement;
        const auto bindings=store_->Publish(snapshot_->product.bindingPlan,C::OwnerDomain::RenderingProtocol);
        stage.inputBindings={bindings.owner,Protocol::Symbol(Protocol::BindingPlan::WireName),{1,0},bindings.record,bindings.revision};
        stage.boundary=C::NativeReturnBoundary::NgxEvaluateReturn;stage.caller=sample_.ingress;
        stage.lastRecording=seeds_.recording;
        if(recipe.placement==C::Placement::NativeBefore)
        {
            srOutputView_=store_->metadata->ForOwner(C::OwnerDomain::Resource).Reserve<C::ResourceView>();
            srOutputContent_=store_->metadata->ForOwner(C::OwnerDomain::Resource).Reserve<C::NativeOutputContentV1>();
            if(!srOutputView_||!srOutputContent_)return false;
        }
        return_=NativeReturnTransaction::Reserve(*store_->metadata,stage,sample_,target,history_,store_);
        return bool(return_);
    }
    // Called only by the selected SR boundary after the existing Resource
    // owner committed the actual caller Output. Preallocated before NR entry.
    bool PublishSrReturn(const C::ResourceView& view,const OpaqueSrEvaluationReceipt& receipt)noexcept
    {
        if(!snapshot_||snapshot_->product.recipe.placement!=C::Placement::NativeBefore||
           !publishedOutput_||returnedSrOutput_||!srOutputView_||!srOutputContent_||
           receipt.Classification()!=OpaqueSrEvaluationReceipt::Class::RecordedOpaqueWrite||
           receipt.Original().inputs[0].native!=outputTarget_||!Context::CompleteContent(view))return false;
        const auto* native=Context::ResolveMetadata(*publishedOutput_,*store_);
        const auto* input=native?Context::ResolveNativeOutput(*native,*store_):nullptr;
        if(!input||receipt.Original().inputs[0].identity!=input->identity||
           native->recording!=seeds_.recording||!srOutputView_->Commit(view))return false;
        C::NativeOutputContentV1 output;output.view=srOutputView_->Reference();output.recording=seeds_.recording;
        output.producerOrdinal=++recordedOrdinal_;output.semanticDomain=outputDomain_;
        if(!srOutputContent_->Commit(output))return false;
        returnedSrOutput_=srOutputContent_->Reference();consumedSrInput_=publishedOutput_;return true;
    }
    void ObserveReturnExecution(const Protocol::NativeProtocolResult& result)noexcept
    {
        if(!return_)return;
        // Caller output is observed later under the Resource lock through the
        // immediate return commit. C07 alone records only Native production.
        providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState&){
            if(!result.evaluation||!publishedOutput_)return return_->Reject("Native.ReturnExecutionUnavailable");
            if(!return_->ObserveNative(*result.evaluation,*publishedOutput_,*store_))return false;
            return true;
        });
    }
    bool ReserveOutput(ID3D12Resource* target)
    {
        auto borrow=LiveBorrow();
        if(!borrow||outputWrite_||publishedOutput_||target!=outputTarget_)return false;
        if(!providerOwner_||!providerOwner_->RegisterOutputTarget(recordingUse_,borrow->RecordingAction(),
            call_->CommandList(),target))return false;
        if(store_->request.placement==C::Placement::NativeBefore)
        {
            internalOutputWrite_=borrow->IssueWriteToken(target);
            if(!internalOutputWrite_)return false;
        }
        outputWrite_=internalOutputWrite_?
            resources_->ReserveRendererOutput(target,call_->CommandList(),borrow->RecordingAction(),*internalOutputWrite_):
            resources_->ReserveOpaqueWrite(target,call_->CommandList(),borrow->RecordingAction());
        return outputWrite_.has_value();
    }
    NativeInvocationOwner(std::shared_ptr<NativeInitialMaterialization::Store> store,
        std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> borrow,
        std::shared_ptr<DlssNr::NativeNgxCallCapture> call,NativeResourceRegistry& resources,
        NativeOwnerSet& owners,C::NativeSampleIdentityV1 sample,C::ScopeRef scope,Seeds seeds,
        std::function<bool()> pinCurrent):store_(std::move(store)),borrow_(std::move(borrow)),
        call_(std::move(call)),resources_(&resources),owners_(&owners),sample_(sample),scope_(scope),
        seeds_(seeds),pinCurrent_(std::move(pinCurrent))
    {if(borrow_)ticket_=borrow_->RecordingAction().RecordingTicket();}
    std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> LiveBorrow()const
    {
        std::lock_guard lock(callbackMutex_);
        if(closed_.load()||!pinCurrent_||!pinCurrent_())return {};
        return borrow_;
    }
    bool LiveRecording()const
    {
        auto borrow=LiveBorrow();
        return borrow&&borrow->RecordingAction().Current()&&call_&&
            borrow->RecordingAction().RecordingTicket()==ticket_&&
            DlssNr::GpuSafety::MatchesLocalRecording(ticket_,call_->CommandList(),
                call_->Resource(store_->request.placement==C::Placement::NativeBefore?"Color":"Output"));
    }
    void CloseCallback(bool completingReturn=false)noexcept
    {
        std::lock_guard lock(callbackMutex_);
        if(closed_)return;
        if(return_&&!completingReturn&&providerOwner_)
            providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState&){
                return_->Reject("Native.ReturnAbandoned");return_->FinishRejected();return false;});
        pinCurrent_={};borrow_.reset();
        if(providerOwner_&&providerUse_)providerOwner_->Cancel(providerUse_);
        closed_=true;
    }
    const Slot* Find(const Protocol::NativeResourceUse& use)const
    {
        const Slot* match=nullptr;
        for(std::size_t i=0;i<slotCount_;++i)
            if(slots_[i].purpose==use.purpose&&slots_[i].native==use.resource)
            {if(match)return nullptr;match=&slots_[i];}
        return match;
    }
    bool Add(std::string_view purpose,ID3D12Resource* resource,
        const C::ContractRef<C::ContractId::C01>& candidate,bool input,bool model,
        C::RecordKey provider={})
    {
        if(slotCount_==slots_.size()||!resource||candidate.record.Check()!=C::Error::None)return false;
        C::Symbol key;if(!key.Assign(purpose))return false;
        for(std::size_t i=0;i<slotCount_;++i)
            if(slots_[i].purpose==key&&slots_[i].native==resource)return false;
        slots_[slotCount_++]={key,resource,candidate,input,model,provider};return true;
    }
    bool AttachFeatureHistory()
    {
        if(!providerOwner_||!owners_)return false;
        history_=providerOwner_->History(recordingUse_,owners_->HistoryJournal().Binding().subject,seeds_.history);
        return bool(history_);
    }
    std::optional<LeaseBinding> Issue(const Slot& slot,bool write,Protocol::NativeCheckPoint point,
        const P& snapshot)
    {
        const auto publication=resources_->PublishNativeLease(slot.native,scope_);
        if(!publication)return {};
        const bool model=point==Protocol::NativeCheckPoint::BeforeModel;
        LeaseBinding binding;auto& d=binding.description;
        d.header=publication->header;d.context=snapshot.product.recipe.context;
        d.candidate=slot.candidate;d.resource=publication->view.identity;
        d.use=write?C::UsageKind::WriteExclusive:
            model?C::UsageKind::RetainedProviderConsumption:C::UsageKind::OrderedGpuRead;
        d.commandScope=scope_;d.device=publication->view.device;d.generations=snapshot.generations;
        const C::EvidenceRef evidence{d.header.record};
        d.revoked=C::OptionalFact<bool>::FromKnown(false,evidence);
        d.revocationEpoch=C::OptionalFact<C::RecordKey>::FromKnown(seeds_.reservation,evidence);
        d.expirationBoundary=C::OptionalFact<C::RecordKey>::FromKnown(sample_.callback,evidence);
        d.submissionRecheckRequired=C::OptionalFact<bool>::FromKnown(true,evidence);
        binding.consumer=seeds_.consumer;binding.purpose=slot.purpose;
        binding.plan=snapshot.product.recipe.committedPlan.record;
        binding.profile=snapshot.product.profileCertificate.header.record;
        binding.route=C::GenerationToken{snapshot.generation.Describe()};
        binding.callbackScoped=true;
        binding.contentBasis=write?ContentBasis::WriteDestination:
            slot.invocationInput?ContentBasis::InvocationPublication:ContentBasis::PhysicalRevision;
        SameRecordingUse use{seeds_.recording,seeds_.reservation,1,2};
        if(model&&!write)use.providerRegistration=slot.providerRegistration;
        binding.sameRecording=use;
        if(!ValidBinding(binding))return {};
        return binding;
    }
    bool InvocationCurrent(const Slot& slot,const P& snapshot)const
    {
        if(!slot.invocationInput)return true;
        const auto* context=Context::ResolveMetadata(snapshot.product.inputContext,*store_);
        const auto* frame=context?Context::ResolveMetadata(context->frame,*store_):nullptr;
        if(!frame)return false;
        for(const auto& use:snapshot.product.inputRepresentations)
            if(use.sourceCandidate&&*use.sourceCandidate==slot.candidate&&
               Protocol::CurrentNativePublication(use,*context,*frame,*store_,snapshot.product.recipe.placement))return true;
        return false;
    }
    OwnerFacts Facts(const Slot& slot,const LeaseBinding& binding,Protocol::NativeCheckPoint point,const P& snapshot)
    {
        OwnerFacts facts;facts.current=binding;
        const auto recording=DlssNr::GpuSafety::InspectRecording(ticket_);
        const bool live=LiveRecording()&&recording.valid&&recording.registryHealthy&&
            !recording.submitted&&!recording.nonReplayable&&
            DlssNr::GpuSafety::MatchesLocalRecording(ticket_,call_->CommandList(),slot.native)&&
            InvocationCurrent(slot,snapshot);
        const bool exclusive=providerOwner_&&providerOwner_->RecordingCurrent(recordingUse_,sample_,ticket_,
            slot.invocationInput?nullptr:slot.native);
        const auto yes=Fact::Yes,no=Fact::No;
        facts.permission=live?yes:no;facts.callbackActive=live?yes:no;
        facts.registryHealthy=recording.registryHealthy?yes:no;
        facts.nativeBindingMatches=live?yes:no;facts.apiCompatible=live?yes:no;
        facts.invocationPublicationCurrent=slot.invocationInput&&live?yes:Fact::Unknown;
        facts.recordingTracked=live?yes:no;facts.resourcesRegistered=live?yes:no;
        facts.descriptorsProtected=live?yes:no;facts.externalRegistrationsComplete=exclusive?yes:no;
        facts.dependency.ordered=yes;facts.dependency.committed=no;facts.dependency.uniqueSubmission=no;
        facts.dependency.completed=no;facts.dependency.nonReplayable=no;facts.dependency.reusable=no;
        facts.dependency.sameDevice=live?yes:no;facts.dependency.queueKnown=Fact::Unknown;
        facts.dependency.supportedType=live?yes:no;
        facts.sameRecording=SameRecordingFacts{*binding.sameRecording,live?yes:no,live?yes:no,exclusive?yes:no,live&&exclusive?yes:no};
        if(point==Protocol::NativeCheckPoint::BeforeModel&&slot.modelUse)
        {
            if(providerOwner_&&providerOwner_->Reserved(providerUse_,slot.providerRegistration,binding.description.resource))
                facts.providerReservation=OwnerFacts::ProviderReservation{
                    slot.providerRegistration,binding.description.resource,providerOwner_->Feature()};
        }
        if(slot.modelUse&&providerOwner_&&
           providerOwner_->GpuHeld(providerUse_,slot.providerRegistration,binding.description.resource))
        {
            // Provider GPU retention transferred to this exact recording. The
            // CPU borrow may already be closed; ordered continuation still needs
            // C03 to acknowledge this hold instead of pretending it is absent.
            ExternalHold hold;hold.registration=slot.providerRegistration;hold.consumer=seeds_.consumer;
            hold.target=binding.description.resource;hold.kind=HoldKind::Provider;
            hold.provider=providerOwner_->Feature();hold.handoff=C::GenerationToken{seeds_.handoff.Describe()};
            hold.released=no;facts.holds.Push(hold);facts.sameRecording->compatibleHolds.Push(hold.registration);
        }
        return facts;
    }
    class Action final:public Protocol::NativeOwnerActionGuard
    {
        NativeInvocationOwner& owner_;
        std::shared_ptr<const P> snapshot_;
        std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> borrow_;
        std::unique_lock<std::recursive_mutex> resourceLock_;
        std::array<std::optional<LiveConsumptionLease>,8> leases_;
        std::array<OwnerFacts,8> facts_;
        std::size_t used_=0;
      public:
        Action(NativeInvocationOwner& owner,std::shared_ptr<const P> snapshot,
            std::shared_ptr<DlssNr::NativeRendererInvocationBorrow> borrow):owner_(owner),
            snapshot_(std::move(snapshot)),borrow_(std::move(borrow)),
            resourceLock_(owner.resources_->LockNativeAction()){++owner_.activeActions_;}
        ~Action()
        {
            for(auto& lease:leases_)if(lease)CloseAdmission(*lease);
            resourceLock_.unlock();borrow_.reset();snapshot_.reset();--owner_.activeActions_;
        }
        const P& Current()const noexcept override{return *snapshot_;}
        Protocol::NativeLeaseProof Refresh(Protocol::NativeCheckPoint point,
            const Protocol::NativeResourceUse& use)noexcept override
        try
        {
            if(used_==leases_.size()||!owner_.LiveRecording())return {};
            const auto* slot=owner_.Find(use);if(!slot||!owner_.InvocationCurrent(*slot,*snapshot_))return {};
            auto binding=owner_.Issue(*slot,use.write,point,*snapshot_);if(!binding)return {};
            auto facts=owner_.Facts(*slot,*binding,point,*snapshot_);
            auto lease=BeginConsumption(*binding,facts);
            if(!lease||!ValidateForRecording(*lease,facts).allowed)return {};
            if(point==Protocol::NativeCheckPoint::BeforeComposition&&use.write&&
               use.purpose.View()=="Native.ComposeTarget"&&
               !owner_.ReserveOutput(use.resource))return {};
            const auto index=used_++;leases_[index].emplace(std::move(*lease));facts_[index]=std::move(facts);
            return {use.resource,&*leases_[index],&facts_[index]};
        }
        catch(...){return {};}
    };
  public:
    std::optional<C::NativeSampleIdentityV1> SourceSample(const Orchestration::MaterializationRecord& record)const override
    {
        if(!LiveRecording()||sample_.Check()!=C::Error::None||
           sample_.session!=record.initialSample.session||sample_.stream!=record.initialSample.stream||
           sample_.view!=record.initialSample.view||sample_.feature!=record.initialSample.feature||
           sample_.ingress!=record.initialSample.ingress)return {};
        return sample_;
    }
    std::shared_ptr<const P> Prepare(const Orchestration::MaterializationRecord& record)override
    {
        preparationReason_=Protocol::Symbol("Native.Admission.OwnerPreparation");
        if(snapshot_||!LiveRecording()||record.delivery.placement!=store_->request.placement)return {};
        auto borrow=LiveBorrow();if(!borrow||!borrow->Current()||!call_||!store_||!owners_)return {};
        if(!record.metadata)return {};
        if(record.metadata.get()!=store_.get())
            store_->committedRoute=std::static_pointer_cast<const NativeInitialMaterialization::Store>(record.metadata);
        if(store_->committedRoute&&
           !NativeInitialMaterialization::SameSettings(*store_,*store_->committedRoute))return {};
        Protocol::RecipeRequest request;
        static_cast<Protocol::NativeProfilePreparationRequest&>(request)=store_->request;
        request.recipeHeader=owners_->ProtocolJournal().Header(C::ContractId::C06,scope_);
        request.externalPlan=Protocol::Reference<C::ContractId::C05>(record.plan);
        request.nativePlan=store_->Publish(record.plan,C::OwnerDomain::StreamCoordinator);
        auto built=Protocol::BuildRecipe(request,*store_);
        if(!built.product){preparationReason_=built.reason;return {};}
        preparationReason_=Protocol::Symbol("Native.Admission.BindingMap");
        const auto map=Protocol::BuildNativeBindingMap(built.product->Value(),*store_);
        const bool before=record.delivery.placement==C::Placement::NativeBefore;
        const auto* source=borrow->Inspect();if(!map||!source||(before&&!source->preSrScratch))return {};
        outputTarget_=before?source->preSrScratch:call_->Resource("Output");
        outputView_=store_->metadata->ForOwner(C::OwnerDomain::Resource).Reserve<C::ResourceView>();
        outputContent_=store_->metadata->ForOwner(C::OwnerDomain::Resource).Reserve<C::NativeOutputContentV1>();
        if(!outputTarget_||!outputView_||!outputContent_)return {};
        const auto* frame=Context::ResolveMetadata(built.product->Value().recipe.frame,*store_);
        const auto* context=Context::ResolveMetadata(store_->request.context,*store_);
        if(!frame||!frame->nativeSample||!context||!Context::Established(record.plan.routeGeneration))return {};
        const auto* colorDescription=Context::ResolveOptional(context->color,*store_);
        if(!colorDescription||!Context::Established(colorDescription->domain)||
           !outputDomain_.Assign(C::EnumName(colorDescription->domain.KnownPart()->value)))return {};
        auto snapshot=std::make_shared<P>();snapshot->product=built.product->Value();
        snapshot->anchor=record.anchor;snapshot->generation=record.plan.routeGeneration.KnownPart()->value;
        snapshot->frame=*frame;snapshot->consumer=seeds_.consumer;snapshot->recording=seeds_.recording;
        snapshot->reservation=seeds_.reservation;snapshot->generations=context->generations;
        snapshot->handoff=seeds_.handoff;
        if(store_->selectedConsumer_)
        {
            // Qualify this actual recipe/sample, after BuildRecipe has issued
            // RENDER.NativeBindings. Structural preparation is not that proof.
            if(!store_->selectedWriterCurrent_||!store_->selectedWriterCurrent_(call_->CommandList(),call_->Resource("Output"))||
               !store_->source->TransactionSubject()||!record.delivery.finalConsumer||
               record.delivery.kind!=C::NativeDeliveryKind::FinalConsumerRequired)return {};
            const auto* consumer=Context::ResolveMetadata(*record.delivery.finalConsumer,*store_);
            if(!consumer||*consumer!=*store_->selectedConsumer_||!frame->nativeSample)return {};
            C::QualificationCertificate fg;
            fg.header=owners_->FrameGenerationJournal().Header(C::ContractId::C04,scope_);
            fg.context=snapshot->product.recipe.context;fg.profile=snapshot->product.recipe.profile;
            fg.purpose=Protocol::Symbol(C::SourceBoundConsumerContractV1::WireName);
            fg.eligibility=C::Eligibility::Eligible;
            fg.nativeFreshness=Context::NativeFreshness(*frame->nativeSample,sample_);
            C::StructuralSignature signature;signature.schema=Protocol::Symbol("FG.ControlledOwnedWriter.v1");
            signature.version=1;signature.generations=context->generations;
            C::BoundedList<C::RecordKey,32> dependencies;dependencies.Push(*store_->selectedEnrollment_);
            dependencies.Push(snapshot->product.profileCertificate.header.record);
            signature.structuralDependencies.count=static_cast<std::uint32_t>(dependencies.Size());
            signature.structuralDependencies.backing=store_->Publish(dependencies,C::OwnerDomain::FrameGeneration);
            fg.signature=store_->Publish(signature,C::OwnerDomain::FrameGeneration);
            C::SourceBoundConsumerContractV1 contract;contract.profile=fg.profile;contract.consumer=*record.delivery.finalConsumer;
            contract.profileQualification=store_->Publish(snapshot->product.profileCertificate,C::OwnerDomain::Strategy);
            contract.consumerQualification=store_->Publish(fg,C::OwnerDomain::FrameGeneration);
            if(!SelectSourceBound(*store_->source->TransactionSubject(),store_->Publish(contract,C::OwnerDomain::FrameGeneration)))return {};
        }
        const auto candidate=[&](Protocol::Purpose purpose)->std::optional<C::ContractRef<C::ContractId::C01>>{
            for(const auto& resource:map->resources)if(resource.purpose==purpose)return resource.sourceCandidate;
            return {};};
        const auto color=candidate(Protocol::Purpose::ColorModelInput);
        const auto depth=candidate(Protocol::Purpose::DepthGuide);
        const auto motion=candidate(Protocol::Purpose::MotionVectorGuide);
        if(!color||!depth||!motion)return {};
        slotCount_=0;
        preparationReason_=Protocol::Symbol("Native.Admission.ResourceSlots");
        if(!Add("Native.ComposeTarget",call_->Resource(before?"Color":"Output"),*color,true,false)||
           !Add("DLSSNR.Depth",call_->Resource("Depth"),*depth,true,true,seeds_.providerRegistrations[1])||
           !Add("DLSSNR.MVec",call_->Resource("MotionVectors"),*motion,true,true,seeds_.providerRegistrations[2])||
           (before&&!Add("Native.PreSrScratch",source->preSrScratch,*color,false,false))||
           (before&&!Add("Native.ComposeTarget",source->preSrScratch,*color,false,false))||
           !Add("DLSSNR.Color",source->modelInput,*color,false,true,seeds_.providerRegistrations[0])||
           !Add("DLSSNR.Output",source->modelOutput,*color,false,true,seeds_.providerRegistrations[3])||
           !Add("Native.OriginalCopy",source->originalCopy,*color,false,false))return {};
        for(std::size_t i=0;i<slotCount_;++i)
        {
            preparationReason_=Protocol::Symbol("Native.Admission.ResourceLease");
            const bool write=!slots_[i].invocationInput;
            auto binding=Issue(slots_[i],write,Protocol::NativeCheckPoint::BeforeRecord,*snapshot);
            if(!binding||!snapshot->leases.Push(*binding))return {};
        }
        preparationReason_=Protocol::Symbol("Native.Admission.SnapshotClosure");
        if(!SealSnapshot(*snapshot,store_))return {};
        if(sourceBoundTransaction_||sourceBoundContract_)
        {
            if(!sourceBoundTransaction_||!sourceBoundContract_)return {};
            SourceBoundPrimaryRequest selected;selected.transaction=*sourceBoundTransaction_;selected.contract=*sourceBoundContract_;
            selected.recipe=snapshot->product.recipe;selected.plan=record.plan;selected.generations=snapshot->generations;
            selected.evaluationAssociation=store_->evaluationAssociation_;
            if(!SealSourceBoundSnapshot(*snapshot,store_,selected))return {};
        }
        providerOwner_=source->providerUses;
        preparationReason_=Protocol::Symbol("Native.Admission.ProviderLifetime");
        if(!providerOwner_||!providerOwner_->Contract().Supported())return {};
        std::vector<ID3D12Resource*> retained{source->modelInput,source->modelOutput,source->originalCopy};
        if(before)retained.push_back(source->preSrScratch);
        recordingUse_=providerOwner_->ReserveRecording(sample_,borrow->RecordingAction(),call_->CommandList(),retained);
        preparationReason_=Protocol::Symbol("Native.Admission.ProviderRecording");
        if(!recordingUse_)return {};
        preparationReason_=Protocol::Symbol("Native.Admission.FeatureHistory");
        if(!AttachFeatureHistory())return {};
        preparationReason_=Protocol::Symbol("Native.Admission.ProviderRegistration");
        std::vector<DlssNr::ProviderRegistrationBinding> registrations;
        for(size_t i=0;i<slotCount_;++i)if(slots_[i].modelUse)
        {
            auto publication=resources_->PublishNativeLease(slots_[i].native,scope_);
            if(!publication)return {};
            registrations.push_back({sample_,slots_[i].providerRegistration,publication->view.identity,
                providerOwner_->Feature(),ticket_,slots_[i].native});
        }
        std::sort(registrations.begin(),registrations.end(),[](const auto& a,const auto& b){return a.registration.value<b.registration.value;});
        providerUse_=providerOwner_->Reserve(registrations,borrow->RecordingAction(),call_->CommandList());
        if(!providerUse_)return {};
        snapshot_=snapshot;preparationReason_=Protocol::Symbol("Native.Admission.MatchOrAcquire");return snapshot_;
    }
    const C::Symbol& PreparationReason()const noexcept{return preparationReason_;}
    std::unique_ptr<Protocol::NativeOwnerActionGuard> Acquire(const P& snapshot,
        Protocol::NativeCheckPoint)override
    {
        auto borrow=LiveBorrow();
        if(!snapshot_||!borrow||!borrow->Current()||!Protocol::SameNativeInvocation(*snapshot_,snapshot))return {};
        return std::make_unique<Action>(*this,snapshot_,std::move(borrow));
    }
    Orchestration::Retirement CancelPreparation()noexcept override
    {return Retire(P{},{});}
  private:
    bool TerminalTails()noexcept
    try
    {
        if(!closed_||activeActions_||returnTailActive_||
           !DlssNr::GpuSafety::InspectTerminalRecording(ticket_))return false;
        if(history_&&(!providerOwner_||!providerOwner_->WithHistory(recordingUse_,
            [](NativeHistoryState& history)noexcept{return history.InvocationClosed();})))return false;
        if(providerOwner_&&providerUse_)
        {
            providerOwner_->Cancel(providerUse_);providerOwner_->Poll();
            if(!providerOwner_->Complete(providerUse_))return false;
        }
        return true;
    }
    catch(...){return false;}
  public:
    Orchestration::Retirement Retire(const P&,const std::optional<PrimaryNrClaimHandle>& claim)noexcept override
    try
    {
        if(claim||!TerminalTails())return Orchestration::Retirement::Outstanding;
        if(providerOwner_&&providerUse_&&!providerOwner_->CloseInvocation(providerUse_))
            return Orchestration::Retirement::Outstanding;
        if(providerOwner_&&recordingUse_&&!providerOwner_->CloseRecording(recordingUse_))
            return Orchestration::Retirement::Outstanding;
        return Orchestration::Retirement::Retired;
    }
    catch(...){return Orchestration::Retirement::Outstanding;}
    std::optional<NativeUnsealedRetirement> UnsealedTerminal(const P& snapshot,
        const PrimaryNrClaimHandle& claim)noexcept override
    try
    {
        if(!snapshot_||!Protocol::SameNativeInvocation(*snapshot_,snapshot)||
           snapshot_->Delivery()!=C::NativeDeliveryKind::FinalConsumerRequired||
           !snapshot_->Sample()||*snapshot_->Sample()!=sample_||!TerminalTails())return {};
        return NativeUnsealedRetirement(claim,snapshot_->product.recipe,sample_,seeds_.recording);
    }
    catch(...){return {};}
    bool ProviderEntry()noexcept override
    try
    {
        auto borrow=LiveBorrow();
        return borrow&&LiveRecording()&&providerOwner_&&providerUse_&&
            providerOwner_->Enter(providerUse_,borrow->RecordingAction(),DlssNr::ProviderLifetimeContract::NgxFeature18(DlssNr::Feature18HostContractIdentity));
    }
    catch(...){return false;}
    void ProviderEntryCancelled()noexcept override
    {
        if(providerOwner_)providerOwner_->CancelActivatedBeforeCall(providerUse_);
    }
    void ProviderReturned(bool success)noexcept override
    {if(providerOwner_)providerOwner_->Returned(providerUse_,success);}
    Protocol::NativeLeaseProof Refresh(Protocol::NativeCheckPoint,const Protocol::NativeResourceUse&)noexcept override
    {return {};}
    bool RecordProduced(Protocol::NativeCheckPoint,const Protocol::NativeResourceSet& produced)noexcept override
    {
        auto borrow=LiveBorrow();if(!borrow||!LiveRecording())return false;
        for(const auto& use:produced)if(use.write)
        {
            const auto* slot=Find(use);if(!slot||slot->invocationInput)return false;
            auto token=borrow->IssueWriteToken(use.resource);
            if(!token||!resources_->RecordRendererWritten(use.resource,call_->CommandList(),*token))return false;
            ++recordedOrdinal_;
        }
        return true;
    }
    std::optional<C::MetadataRef<C::ResourceView>> RecordOutput(
        const DlssNr::NativeOutputPassToken& pass)noexcept override
    try
    {
        auto borrow=LiveBorrow();
        if(!borrow||!snapshot_||!activeActions_||!LiveRecording()||!outputWrite_||
           !outputView_||!outputContent_||publishedOutput_||
           !pass.Matches(*borrow,call_->CommandList(),outputTarget_))return {};
        const auto* current=resources_->Observe(outputTarget_);
        if(!current||!Context::Established(current->raster.active))return {};
        const auto rect=current->raster.active.KnownPart()->value;
        if(rect.x||rect.y||rect.width!=pass.Outcome().Width()||rect.height!=pass.Outcome().Height()||
           pass.Outcome().Subresource()!=0)return {};
        if(!resources_->CommitRendererOutput(*outputWrite_,*borrow,pass,internalOutputWrite_.get()))return {};
        current=resources_->Observe(outputTarget_);
        if(!current||!Context::CompleteContent(*current)||!outputView_->Commit(*current))return {};
        C::NativeOutputContentV1 output;output.view=outputView_->Reference();
        output.recording=seeds_.recording;output.producerOrdinal=++recordedOrdinal_;
        // The selected C06 profile owns the semantic interpretation.
        output.semanticDomain=outputDomain_;
        if(!outputContent_->Commit(output))return {};
        publishedOutput_=outputContent_->Reference();return output.view;
    }
    catch(...){return {};}
    std::optional<C::NativeStageDeliveryV1> CommittedStage()const noexcept override
    try
    {
        if(!closed_||!providerOwner_)return {};
        std::optional<C::NativeStageDeliveryV1> stage;
        providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState&){
            const auto* published=return_?return_->CommittedStage():nullptr;
            if(published)stage=*published;return bool(published);});
        return stage;
    }
    catch(...){return {};}
    bool FinalConsumed(const P& snapshot,const PrimaryNrClaimHandle& claim,
        const FinalizerConsumptionAcknowledgment& acknowledgment)noexcept override
    try
    {
        std::lock_guard lock(callbackMutex_);
        if(!closed_||activeActions_||!snapshot_||!owners_||!providerOwner_||
           !Protocol::SameNativeInvocation(*snapshot_,snapshot)||snapshot.Delivery()!=C::NativeDeliveryKind::FinalConsumerRequired||
           !acknowledgment.Matches(*owners_->Finalizer(),claim,snapshot.product.recipe.evaluation))return false;
        return providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState& history)noexcept
            {return history.Consumed(snapshot.product.recipe.evaluation);});
    }
    catch(...){return false;}
    bool FinalInterrupted(const P& snapshot,const PrimaryNrClaimHandle& claim,
        const FinalizerInterruptionAcknowledgment& acknowledgment)noexcept override
    try
    {
        std::lock_guard lock(callbackMutex_);
        if(!closed_||activeActions_||!snapshot_||!owners_||!providerOwner_||
           !Protocol::SameNativeInvocation(*snapshot_,snapshot)||snapshot.Delivery()!=C::NativeDeliveryKind::FinalConsumerRequired||
           !acknowledgment.Matches(*owners_->Finalizer(),claim,snapshot.product.recipe.evaluation))return false;
        return providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState& history)noexcept
            {history.Abandon(snapshot.product.recipe.evaluation);return history.InvocationClosed();});
    }
    catch(...){return false;}
    NativeHistoryState* History()const noexcept{return history_.get();}
    bool SourceBoundLogicalClosed(const P& snapshot,const SourceBoundClaimHandle& claim,
        const SourceBoundLogicalAcknowledgment& acknowledgment)noexcept override
    try
    {
        std::lock_guard lock(callbackMutex_);
        if(!closed_||activeActions_||!snapshot_||!owners_||!providerOwner_||
           !Protocol::SameNativeInvocation(*snapshot_,snapshot)||!snapshot.SourceBoundRequest()||
           !acknowledgment.Matches(*owners_->Finalizer(),claim,snapshot.product.recipe.evaluation))return false;
        return providerOwner_->WithHistory(recordingUse_,[&](NativeHistoryState& history)noexcept {
            if(acknowledgment.Consumed())return history.Consumed(snapshot.product.recipe.evaluation);
            history.Abandon(snapshot.product.recipe.evaluation);return history.InvocationClosed();});
    }
    catch(...){return false;}
    bool RetainsReturnTransaction()const noexcept override{return return_&&return_->HistoryAwaitSafe();}
    const std::shared_ptr<NativeInitialMaterialization::Store>& Store()const noexcept{return store_;}
};
}
