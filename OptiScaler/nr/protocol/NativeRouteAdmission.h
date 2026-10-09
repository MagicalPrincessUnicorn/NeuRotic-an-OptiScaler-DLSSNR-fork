#pragma once
#include "NrExecutionRecipe.h"
#include "NativeTemporalBindingMapper.h"
#include <nr/lifecycle/FinalRealFrameFinalizer.h>
#include <nr/orchestration/StreamCoordinatorKernel.h>
#include <functional>
struct ID3D12Resource;
namespace Neurotic::Protocol
{
enum class NativeCheckPoint { BeforeRecord,BeforeModel,BeforeComposition,BeforeCopy,BeforeSubmission };
struct NativeResourceUse { C::Symbol purpose;ID3D12Resource* resource=nullptr;bool write=false; };
using NativeResourceSet=C::BoundedList<NativeResourceUse,8>;
struct NativeLeaseProof
{
    ID3D12Resource* exactNative=nullptr;
    const Lifecycle::LiveConsumptionLease* lease=nullptr;
    const Lifecycle::OwnerFacts* facts=nullptr;
};
struct NativeInvocationSnapshot
{
  private:
    friend class NativeInvocationOwnerPort;
    friend class Orchestration::StreamCoordinatorKernel;
    std::optional<C::NativeSampleIdentityV1> sample_;
    C::NativeDeliveryKind delivery_=C::NativeDeliveryKind::Invalid;
    std::optional<Lifecycle::SourceBoundPrimaryRequest> sourceRequest_;
    std::optional<C::InterceptedSourceTransactionV1> sourceTransaction_;
    std::function<Lifecycle::SourceBoundResult(Lifecycle::FinalRealFrameFinalizer&,
        const Lifecycle::SourceBoundPrimaryRequest&,const Lifecycle::FinalizerEvent&)> sourceClaim_;
    std::function<Lifecycle::SourceBoundResult(Lifecycle::FinalRealFrameFinalizer&,
        const Lifecycle::SourceBoundClaimHandle&,const Lifecycle::FinalCandidateSubmission&,
        const C::NativeStageDeliveryV1&,const Lifecycle::FinalizerEvent&)> sourceStage_;
    std::function<Lifecycle::FinalizationResult(Lifecycle::FinalRealFrameFinalizer&,
        const Lifecycle::PrimaryClaimRequest&,const Lifecycle::FinalizerEvent&)> claim_;
    std::function<Lifecycle::FinalizationResult(Lifecycle::FinalRealFrameFinalizer&,
        const Lifecycle::PrimaryNrClaimHandle&,const Lifecycle::FinalCandidateSubmission&,const Lifecycle::FinalizerEvent&)> submit_;
    std::function<Lifecycle::FinalizationResult(Lifecycle::FinalRealFrameFinalizer&,
        const Lifecycle::PrimaryNrClaimHandle&,const C::NativeStageDeliveryV1&,const Lifecycle::FinalizerEvent&)> stage_;
  public:
    // Resolved only while the authentic owner seals the complete immutable
    // metadata closure. Descriptive access does not create an admission.
    const std::optional<C::NativeSampleIdentityV1>& Sample()const noexcept{return sample_;}
    C::NativeDeliveryKind Delivery()const noexcept{return delivery_;}
    const std::optional<Lifecycle::SourceBoundPrimaryRequest>& SourceBoundRequest()const noexcept{return sourceRequest_;}
    bool SourceEvaluationAssociated()const noexcept
    {
        return sample_&&sourceRequest_&&sourceTransaction_&&sourceRequest_->evaluationAssociation&&
            sourceRequest_->recipe==product.recipe&&sourceTransaction_->sample==*sample_&&
            sourceRequest_->evaluationAssociation->Matches(*sourceTransaction_,product.recipe.evaluation);
    }
    RecipeProduct product;
    Orchestration::PlanAnchor anchor;
    C::RouteGeneration generation;
    C::FrameIdentity frame;
    C::RecordKey consumer,recording,reservation;
    C::BoundedList<Lifecycle::LeaseBinding,8> leases;
    C::MetadataRef<C::GenerationVector> generations;
    C::HandoffContractGeneration handoff;
    std::shared_ptr<const void> metadata;
};
bool SameNativeInvocation(const NativeInvocationSnapshot&,const NativeInvocationSnapshot&)noexcept;
bool NativeInvocationMatches(const NativeInvocationSnapshot&,const Orchestration::MaterializationRecord&)noexcept;

// Each implementation is an actual owner's scoped lock/borrow. It must retain
// that synchronization until destruction, across validation AND the operation.
class NativeOwnerActionGuard
{
  public:
    virtual ~NativeOwnerActionGuard()=default;
    virtual const NativeInvocationSnapshot& Current()const noexcept=0;
    virtual NativeLeaseProof Refresh(NativeCheckPoint,const NativeResourceUse&)noexcept=0;
};
// Registered only by the process ingress, never by a renderer/serialized value.
// Metadata must remain immutable and pinned through authoritative retirement.
class NativeInvocationOwnerPort
{
  protected:
    template<class Store>static bool SealSourceBoundSnapshot(NativeInvocationSnapshot& s,
        const std::shared_ptr<Store>& store,const Lifecycle::SourceBoundPrimaryRequest& request)
    {
        if(!store||s.delivery_!=C::NativeDeliveryKind::FinalConsumerRequired||s.sourceRequest_||
           request.recipe!=s.product.recipe||request.generations!=s.generations||
           !Context::ResolveMetadata(request.transaction,*store)||!Context::ResolveMetadata(request.contract,*store))return false;
        const auto& transaction=*Context::ResolveMetadata(request.transaction,*store);
        if(!s.sample_||transaction.sample!=*s.sample_||
           (request.evaluationAssociation&&!request.evaluationAssociation->Matches(transaction,request.recipe.evaluation))||
           (!s.frame.evaluationId.IsKnown()&&!request.evaluationAssociation)||
           (s.frame.evaluationId.IsKnown()&&(!Context::Established(s.frame.evaluationId)||
               s.frame.evaluationId.KnownPart()->value!=request.recipe.evaluation)))return false;
        s.sourceRequest_=request;
        s.sourceTransaction_=*Context::ResolveMetadata(request.transaction,*store);
        s.sourceClaim_=[store](Lifecycle::FinalRealFrameFinalizer& f,const Lifecycle::SourceBoundPrimaryRequest& r,
            const Lifecycle::FinalizerEvent& e){return f.ReserveSourceBoundPrimary(r,*store,e);};
        s.sourceStage_=[store](Lifecycle::FinalRealFrameFinalizer& f,const Lifecycle::SourceBoundClaimHandle& h,
            const Lifecycle::FinalCandidateSubmission& c,const C::NativeStageDeliveryV1& stage,const Lifecycle::FinalizerEvent& e)
            {return f.SubmitSourceBoundCandidate(h,c,stage,*store,e);};
        return true;
    }
    template<class Store>static bool SealSnapshot(NativeInvocationSnapshot& s,const std::shared_ptr<Store>& store)
    {
        if(!store)return false;
        const auto* frame=Context::ResolveMetadata(s.product.recipe.frame,*store);
        const auto* sample=frame?Context::ResolveNativeSample(*frame,*store):nullptr;
        const auto* delivery=s.product.recipe.nativeDelivery?
            Context::ResolveMetadata(*s.product.recipe.nativeDelivery,*store):nullptr;
        std::size_t remaining=16384;
        if(!frame||*frame!=s.frame||!s.product.recipe.nativeSample||
            s.product.recipe.nativeSample!=frame->nativeSample||!sample||
            !delivery||delivery->Check()!=C::Error::None||!Context::ValidValues(s.product)||
            !BuildNativeBindingMap(s.product,*store)||
            !Orchestration::Detail::Closure(s.product,*store,remaining)||
            !Orchestration::Detail::Closure(s.generations,*store,remaining))return false;
        s.sample_=*sample;
        s.delivery_=delivery->kind;
        s.metadata=store;
        s.claim_=[store](Lifecycle::FinalRealFrameFinalizer& finalizer,const Lifecycle::PrimaryClaimRequest& request,
            const Lifecycle::FinalizerEvent& event){return finalizer.TryClaimPrimary(request,*store,event);};
        s.submit_=[store](Lifecycle::FinalRealFrameFinalizer& finalizer,const Lifecycle::PrimaryNrClaimHandle& claim,
            const Lifecycle::FinalCandidateSubmission& candidate,const Lifecycle::FinalizerEvent& event)
            {return finalizer.SubmitEvaluationResult(claim,candidate,*store,event);};
        s.stage_=[store](Lifecycle::FinalRealFrameFinalizer& finalizer,const Lifecycle::PrimaryNrClaimHandle& claim,
            const C::NativeStageDeliveryV1& stage,const Lifecycle::FinalizerEvent& event)
            {return finalizer.SubmitStageDelivery(claim,stage,*store,event);};
        return true;
    }
  public:
    virtual ~NativeInvocationOwnerPort()=default;
    // Read the authentic outer callback's identity without preparing a recipe
    // or entering an owner action. The coordinator reserves this sample first.
    virtual std::optional<C::NativeSampleIdentityV1> SourceSample(
        const Orchestration::MaterializationRecord&)const=0;
    virtual std::shared_ptr<const NativeInvocationSnapshot> Prepare(const Orchestration::MaterializationRecord&)=0;
    virtual std::unique_ptr<NativeOwnerActionGuard> Acquire(const NativeInvocationSnapshot&,NativeCheckPoint)=0;
    virtual Orchestration::Retirement CancelPreparation()noexcept=0;
    // Retired includes exact recording/replay/provider exclusion and the
    // finalizer claim tail. Unknown never releases a slot or metadata.
    virtual Orchestration::Retirement Retire(const NativeInvocationSnapshot&,
        const std::optional<Lifecycle::PrimaryNrClaimHandle>&)noexcept=0;
    // This never releases the coordinator slot. The sole finalizer must first
    // consume the exact receipt plus IdentityRegistry exclusion for its claim.
    virtual std::optional<Lifecycle::NativeUnsealedRetirement> UnsealedTerminal(
        const NativeInvocationSnapshot&,const Lifecycle::PrimaryNrClaimHandle&)noexcept{return {};}
    virtual bool FinalConsumed(const NativeInvocationSnapshot&,const Lifecycle::PrimaryNrClaimHandle&,
        const Lifecycle::FinalizerConsumptionAcknowledgment&)noexcept{return false;}
    virtual bool FinalInterrupted(const NativeInvocationSnapshot&,const Lifecycle::PrimaryNrClaimHandle&,
        const Lifecycle::FinalizerInterruptionAcknowledgment&)noexcept{return false;}
    virtual std::optional<C::NativeStageDeliveryV1> CommittedStage()const noexcept{return {};}
    virtual bool SourceBoundLogicalClosed(const NativeInvocationSnapshot&,const Lifecycle::SourceBoundClaimHandle&,
        const Lifecycle::SourceBoundLogicalAcknowledgment&)noexcept{return false;}
};
class NativeExecutionObserver;
class NativeInvocationIngress;
class NativeInvocationAdmission
{
    friend class Orchestration::StreamCoordinatorKernel;
    friend class NativeInvocationIngress;
    friend class NativeExecutionObserver;
#ifdef NR_ORCH_INIT_TESTING
    friend class Orchestration::InitialNativeTestAccess;
#endif
    std::shared_ptr<Lifecycle::NativeSessionLifetime> root_;
    std::size_t slot_=0;std::uint64_t serial_=0;
    NativeInvocationAdmission(std::shared_ptr<Lifecycle::NativeSessionLifetime>,std::size_t,std::uint64_t);
    std::unique_ptr<NativeOwnerActionGuard> Action(NativeCheckPoint);
    bool StartModel(const NativeOwnerActionGuard&);
    bool ModelCalled();
    void ModelReturned();
    void Drop()noexcept;
  public:
    ~NativeInvocationAdmission();
    NativeInvocationAdmission(const NativeInvocationAdmission&)=delete;
    NativeInvocationAdmission& operator=(const NativeInvocationAdmission&)=delete;
    NativeInvocationAdmission(NativeInvocationAdmission&&)noexcept;
    NativeInvocationAdmission& operator=(NativeInvocationAdmission&&)noexcept;
    bool AcquirePrimary();
    bool SubmitEvaluation(const C::EvaluationResult&);
    std::optional<Lifecycle::PrimaryNrClaimHandle> Primary()const;
    std::optional<Lifecycle::SourceBoundClaimHandle> SourceBoundPrimary()const;
    C::NativeDeliveryKind Delivery()const noexcept;
    std::optional<C::NativeSampleIdentityV1> Sample()const;
    const RecipeProduct* Product()const noexcept;
};
class NativeInvocationIngress
{
    friend class Lifecycle::NativeProcessBootstrap;
#ifdef NR_ORCH_INIT_TESTING
    friend class Orchestration::InitialNativeTestAccess;
#endif
    // No public conversion from CurrentRouteView or any C05/C03 value.
    static std::optional<NativeInvocationAdmission> Admit(
        const std::shared_ptr<Lifecycle::NativeSessionLifetime>&,std::shared_ptr<NativeInvocationOwnerPort>);
};
}
