#pragma once
#include "OwnerMetadataArena.h"
#include "NativeHistoryState.h"
#include <nr/context/NativeStageQualification.h>
#include <dlssnr/NativeHostReturnObservation.h>
#include <cstring>
#define NR_NATIVE_DEFERRED_RETURN_FAILURE_V1 1
#define NR_NATIVE_HOST_RETURN_OBSERVATION_V1 1
#define NR_NATIVE_HOST_RETURN_RESOURCE_OBSERVATION_V1 1

namespace Neurotic::Lifecycle
{
class NativeProcessBootstrap;
class NativeInvocationOwner;
// Retained by the real invocation owner before any Native effects. The private
// observation ports are reached only from actual outer/provider/resource calls;
// decoded values and metadata readers never enroll an operation authority.
// All access is serialized by that owner, including Prepare through Commit.
class NativeReturnTransaction final
{
    friend class NativeProcessBootstrap;
    friend class NativeInvocationOwner;
#ifdef NR_NATIVE_RETURN_TRANSACTION_TESTING
    friend class NativeReturnTransactionTestAccess;
#endif
    using Causes=C::BoundedList<C::OwnerValueReference,16>;
    enum class State { Reserved, Prepared, Committed, Rejected };
    // Retain the arena/model root until the invocation retires, not merely until
    // the CPU callback returns. Releases run outside the success return tail.
    std::shared_ptr<const void> retainedRoot_;
    std::shared_ptr<NativeHistoryState> history_;
    OwnerMetadataArena::Reservation<C::NativeStageDeliveryV1> stageSlot_;
    OwnerMetadataArena::Reservation<Causes> causesSlot_;
    C::NativeStageDeliveryV1 stage_;
    C::NativeSampleIdentityV1 sample_;
    C::ResourceView target_,currentTarget_;
    std::optional<C::EvaluationResult> native_;
    std::optional<C::MetadataRef<C::NativeOutputContentV1>> srInput_;
    Causes causes_;
    State state_=State::Reserved;
    C::NativeDeliveryKind kind_=C::NativeDeliveryKind::Invalid;
    bool srObserved_=false,srSucceeded_=false,restored_=false,outputObserved_=false,failureFinished_=false;
    bool failurePublished_=false;
    NativeReturnTransaction(std::shared_ptr<const void> root,std::shared_ptr<NativeHistoryState> history,
        OwnerMetadataArena::Reservation<C::NativeStageDeliveryV1> stageSlot,
        OwnerMetadataArena::Reservation<Causes> causesSlot,const C::NativeStageDeliveryV1& stage,
        const C::NativeSampleIdentityV1& sample,const C::ResourceView& target)noexcept:
        retainedRoot_(std::move(root)),history_(std::move(history)),stageSlot_(std::move(stageSlot)),
        causesSlot_(std::move(causesSlot)),stage_(stage),sample_(sample),target_(target){}
    static C::OwnerValueReference Value(const C::MetadataRef<C::NativeStageDeliveryV1>& ref)noexcept
    {C::Symbol name;name.Assign(C::NativeStageDeliveryV1::WireName);return {ref.owner,name,{1,0},ref.record,ref.revision};}
    static C::OwnerValueReference Value(const C::MetadataRef<C::NativeOutputContentV1>& ref)noexcept
    {C::Symbol name;name.Assign(C::NativeOutputContentV1::WireName);return {ref.owner,name,{1,0},ref.record,ref.revision};}
    static bool SameTarget(const C::ResourceView& a,const C::ResourceView& b)
    {
        return Context::CompleteResourceStructure(a)&&Context::CompleteResourceStructure(b)&&
            Context::SameResourceStructure(a.identity,b.identity)&&Context::SameFact(a.device,b.device)&&
            Context::SameFact(a.adapter,b.adapter)&&Context::SameFact(a.mip,b.mip)&&
            Context::SameFact(a.arrayLayer,b.arrayLayer)&&Context::SameFact(a.plane,b.plane)&&
            Context::SemanticEqual(a.descriptor,b.descriptor)&&Context::SemanticEqual(a.raster,b.raster);
    }
    // Seed comes from the exact admitted snapshot, including its reserved C07
    // reference and recording. The target is a structural expectation: its old
    // content revision is deliberately not part of the captured expectation.
    static std::unique_ptr<NativeReturnTransaction> Reserve(OwnerMetadataArena& arena,
        C::NativeStageDeliveryV1 seed,const C::NativeSampleIdentityV1& sample,const C::ResourceView& target,
        std::shared_ptr<NativeHistoryState> history,std::shared_ptr<const void> retainedRoot)noexcept
    {
        try
        {
            if(!history||!retainedRoot||sample.Check()!=C::Error::None||seed.caller!=sample.ingress||
                !Context::CompleteResourceStructure(target))return {};
            auto publisher=arena.ForOwner(C::OwnerDomain::Provider);
            auto stageSlot=publisher.Reserve<C::NativeStageDeliveryV1>();if(!stageSlot)return {};
            auto causesSlot=publisher.Reserve<Causes>();if(!causesSlot)return {};
            seed.publication=Value(stageSlot->Reference());seed.nativeOutput.reset();seed.returnedOutput.reset();
            seed.outcome=C::NativeStageOutcome::DeliveryRejected;seed.reason.Assign("Native.ReturnReserved");
            seed.causes={};seed.lastRecordedOrdinal=0;seed.hostResult=0;
            if(!Context::ValidValues(seed))return {};
            return std::unique_ptr<NativeReturnTransaction>(new NativeReturnTransaction(std::move(retainedRoot),
                std::move(history),std::move(*stageSlot),std::move(*causesSlot),seed,sample,target));
        }
        catch(...){return {};}
    }
    bool Reject(std::string_view reason)noexcept
    {
        if(state_==State::Committed||state_==State::Rejected)return false;
        state_=State::Rejected;stage_.outcome=C::NativeStageOutcome::DeliveryRejected;
        stage_.reason.Assign(reason);stage_.causes={};causesSlot_.Cancel();
        if(history_)history_->Abandon(stage_.evaluation);
        return false;
    }
    bool FinishRejected()noexcept
    {
        if(state_!=State::Rejected||failureFinished_)return false;
        failureFinished_=true;
        // Failure has its own preallocated body. It cannot erase C07 Produced
        // or manufacture successful delivery when required publication fails.
        // Defer publication until the outer owner has captured a later actual
        // SR result; the first failure and history demand were already latched.
        failurePublished_=stageSlot_.Commit(stage_);return failurePublished_;
    }
    template<class Reader>bool ObserveNative(const C::EvaluationResult& result,
        const C::MetadataRef<C::NativeOutputContentV1>& output,const Reader& reader)noexcept
    {
        if(state_!=State::Reserved)return false;
        try
        {
            if(native_)return Reject("Native.ReturnNativeRepeated");
            native_=result;
            const auto* content=Context::ResolveMetadata(output,reader);
            if(!Context::ValidValues(result)||result.stage!=C::OutcomeStage::Produced||
                result.evaluation!=stage_.evaluation||stage_.executionResult.record!=result.header.record||
                stage_.executionResult.revision!=result.header.revision||
                !Context::Established(result.output)||!content||output.owner!=C::OwnerDomain::Resource||
                content->view!=result.output.KnownPart()->value||!Context::ResolveNativeOutput(*content,reader))
                return Reject("Native.ReturnNativeMismatch");
            stage_.nativeOutput=output;return true;
        }
        catch(...){return Reject("Native.ReturnObservationFailed");}
    }
    // currentTarget is read from the actual Resource owner at the last recorded
    // caller write while its action remains held. It is never an old target
    // capture or a pointer-derived guess at current contents.
    template<class Reader>bool ObserveOutput(const C::MetadataRef<C::NativeOutputContentV1>& output,
        const C::ResourceView& currentTarget,const Reader& reader)noexcept
    {
        if(state_!=State::Reserved)return false;
        try
        {
            if(outputObserved_)return Reject("Native.ReturnOutputRepeated");
            outputObserved_=true;
            const auto* content=Context::ResolveMetadata(output,reader);
            const auto* view=content?Context::ResolveNativeOutput(*content,reader):nullptr;
            if(output.owner!=C::OwnerDomain::Resource||!view||!SameTarget(target_,*view)||
                !SameTarget(*view,currentTarget)||!Context::SameContent(view->identity.contentRevision,currentTarget.identity.contentRevision)||
                content->recording!=stage_.lastRecording)return Reject("Native.ReturnTargetMismatch");
            currentTarget_=currentTarget;stage_.returnedOutput=output;stage_.lastRecordedOrdinal=content->producerOrdinal;return true;
        }
        catch(...){return Reject("Native.ReturnObservationFailed");}
    }
    bool ObserveSr(std::uint32_t hostResult,bool succeeded,
        std::optional<C::MetadataRef<C::NativeOutputContentV1>> actualConsumedNative={})noexcept
    {
        if(state_==State::Rejected)
        {
            if(!failureFinished_&&!srObserved_)
            {stage_.hostResult=hostResult;srObserved_=true;srSucceeded_=succeeded;srInput_=actualConsumedNative;}
            return false;
        }
        if(state_!=State::Reserved)return false;
        if(srObserved_)return Reject("Native.ReturnSrRepeated");
        stage_.hostResult=hostResult;srObserved_=true;srSucceeded_=succeeded;srInput_=actualConsumedNative;
        return succeeded||Reject("Native.ReturnSrFailed");
    }
    bool ObserveRestoration(bool parametersRestored,bool commandStateRestored)noexcept
    {
        if(state_!=State::Reserved)return false;
        if(restored_)return Reject("Native.ReturnRestorationRepeated");
        if(!parametersRestored)return Reject("Native.ReturnParametersNotRestored");
        if(!commandStateRestored)return Reject("Native.ReturnCommandStateNotRestored");
        restored_=true;return true;
    }
    template<class Reader>bool Prepare(const Reader& reader)noexcept
    {
        if(state_!=State::Reserved)return false;
        try
        {
            if(!native_||!stage_.nativeOutput||!stage_.returnedOutput||!srObserved_||!srSucceeded_||!restored_)
                return Reject("Native.ReturnIncomplete");
            const auto* sample=Context::ResolveMetadata(stage_.sample,reader);
            const auto* delivery=Context::ResolveMetadata(stage_.delivery,reader);
            const auto* frame=Context::ResolveMetadata(native_->originalLineage,reader);
            const auto* native=Context::ResolveMetadata(*stage_.nativeOutput,reader);
            const auto* output=Context::ResolveMetadata(*stage_.returnedOutput,reader);
            const auto* view=output?Context::ResolveNativeOutput(*output,reader):nullptr;
            if(!sample||*sample!=sample_||!frame||!frame->nativeSample||*frame->nativeSample!=stage_.sample||
                !delivery||delivery->placement!=stage_.placement||delivery->returnBoundary!=stage_.boundary||
                (delivery->kind!=C::NativeDeliveryKind::HostReturn&&delivery->kind!=C::NativeDeliveryKind::FinalConsumerRequired)||
                !native||!Context::ResolveNativeOutput(*native,reader)||!view||!SameTarget(target_,*view)||
                !SameTarget(currentTarget_,*view)||!Context::SameContent(currentTarget_.identity.contentRevision,view->identity.contentRevision)||
                output->recording!=stage_.lastRecording||output->producerOrdinal!=stage_.lastRecordedOrdinal||
                !history_->CanCommitReturn(stage_.evaluation,sample_))return Reject("Native.ReturnIdentityMismatch");
            if(stage_.placement==C::Placement::NativeAfter)
            {
                if(!Context::SameNativeOutputContent(*native,*output,reader))return Reject("Native.ReturnOutputChainMismatch");
            }
            else if(!srInput_||*srInput_!=*stage_.nativeOutput)return Reject("Native.ReturnSrInputMismatch");
            kind_=delivery->kind;
            causes_.Push(Value(*stage_.nativeOutput));causes_.Push(Value(*stage_.returnedOutput));
            stage_.causes.count=static_cast<std::uint32_t>(causes_.Size());stage_.causes.backing=causesSlot_.Reference();
            stage_.outcome=C::NativeStageOutcome::HostReturnRecorded;stage_.reason.Assign("Native.HostReturnRecorded");
            if(!Context::ValidValues(stage_)||!Context::ValidValues(causes_))return Reject("Native.ReturnPublicationInvalid");
            state_=State::Prepared;return true;
        }
        catch(...){return Reject("Native.ReturnPreparationFailed");}
    }
    // No owner reader, callback, allocation, COM release or logging may occur
    // here or in the immediate return tail. Any foreign cleanup is completed
    // before this method; retained roots are released on later retirement.
    bool Commit()noexcept
    {
        if(state_==State::Rejected){FinishRejected();return false;}
        if(state_!=State::Prepared)return false;
        if(!history_->CanCommitReturn(stage_.evaluation,sample_))return Reject("Native.ReturnHistoryChanged");
        if(!causesSlot_.Commit(causes_)||!stageSlot_.Commit(stage_))return Reject("Native.ReturnCommitFailed");
        if(kind_==C::NativeDeliveryKind::HostReturn)history_->CommitReturn();
        state_=State::Committed;return true;
    }
  public:
    NativeReturnTransaction(const NativeReturnTransaction&)=delete;
    NativeReturnTransaction& operator=(const NativeReturnTransaction&)=delete;
    ~NativeReturnTransaction(){Reject("Native.ReturnAbandoned");FinishRejected();}
    bool HistoryAwaitSafe()const noexcept{return state_==State::Reserved||state_==State::Prepared;}
    const C::MetadataRef<C::NativeStageDeliveryV1>& StageReference()const noexcept{return stageSlot_.Reference();}
    const C::NativeStageDeliveryV1* CommittedStage()const noexcept{return state_==State::Committed?&stage_:nullptr;}
    // Read only after the outer return tail, under the existing History lock.
    // This diagnostic projection grants no delivery or retirement authority.
    void ObserveForHost(DlssNr::NativeHostReturnObservationV1& value)const noexcept
    {
        value.status=state_==State::Committed?4u:state_==State::Rejected?3u:2u;
        value.stageOutcome=state_==State::Committed||failurePublished_?static_cast<std::uint32_t>(stage_.outcome):0u;
        value.evaluation=stage_.evaluation.value;value.lastRecordedOrdinal=stage_.lastRecordedOrdinal;
        value.hostResult=stage_.hostResult;
        value.restorationFlags=(value.restorationFlags&~3u)|(restored_?3u:0u);
        std::memset(value.reason,0,sizeof(value.reason));
        const auto reason=stage_.reason.View();
        std::memcpy(value.reason,reason.data(),(std::min)(reason.size(),sizeof(value.reason)-1));
    }
    template<class Reader>bool ObserveOutputForHost(const Reader& reader,const C::ResourceView& current,
        std::uint64_t recordingIncarnation,DlssNr::NativeHostReturnObservationV1& value)const noexcept
    try
    {
        value.provenanceFlags=0;value.outputContentRevision=0;value.recordingIncarnation=0;
        value.outputObjectIncarnation={};value.outputResourceIncarnation={};value.outputViewIncarnation={};
        value.outputResourceGeneration={};value.outputRepresentationGeneration={};value.recordingIdentity={};
        if(state_!=State::Committed||!stage_.returnedOutput||!recordingIncarnation||!Context::CompleteContent(current))return false;
        const auto* content=Context::ResolveMetadata(*stage_.returnedOutput,reader);
        const auto* view=content?Context::ResolveNativeOutput(*content,reader):nullptr;
        if(!view||*view!=current||content->recording!=stage_.lastRecording)return false;
        const auto identity=[](DlssNr::NativeObservedIdentityV1& out,const auto& in){
            const auto ns=in.nameSpace.View(),issuer=in.issuer.View();
            if(ns.size()>=sizeof(out.nameSpace)||issuer.size()>=sizeof(out.issuer))return false;
            out.value=in.value;std::memcpy(out.nameSpace,ns.data(),ns.size());
            std::memcpy(out.issuer,issuer.data(),issuer.size());return true;};
        const auto& id=view->identity;
        if(!identity(value.outputObjectIncarnation,id.objectIncarnation.KnownPart()->value)||
           !identity(value.outputResourceIncarnation,id.resourceIncarnation.KnownPart()->value)||
           !identity(value.outputViewIncarnation,id.resourceViewIncarnation.KnownPart()->value)||
           !identity(value.outputResourceGeneration,id.resourceGeneration.KnownPart()->value.identity)||
           !identity(value.outputRepresentationGeneration,id.representationGeneration.KnownPart()->value.identity)||
           !identity(value.recordingIdentity,stage_.lastRecording))return false;
        value.outputContentRevision=id.contentRevision.KnownPart()->value.value;
        value.recordingIncarnation=recordingIncarnation;value.provenanceFlags=3;return true;
    }
    catch(...){value.provenanceFlags=0;return false;}
};
}
