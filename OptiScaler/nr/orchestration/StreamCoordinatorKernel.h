#pragma once
#include "RoutingPublication.h"
#include <nr/protocol/NativeProfilePreparation.h>
#include <nr/lifecycle/FinalRealFrameTypes.h>
#include <nr/lifecycle/SourceBoundConsumerTypes.h>
#include <memory>
#include <mutex>

namespace Neurotic::Lifecycle { class NativeSessionLifetime; class NativeProcessBootstrap; class NativeFinalConsumerAction; class NativeFinalConsumerTail; }
namespace Neurotic::Lifecycle { class NativeSourceBoundConsumerAction; class NativeSourceBoundConsumerTail; }
namespace Neurotic::Protocol
{
class NativeInvocationAdmission;class NativeInvocationIngress;class NativeInvocationOwnerPort;class NativeOwnerActionGuard;
struct RecipeProduct;enum class NativeCheckPoint;
}
namespace Neurotic::Orchestration
{
enum class RoutePhase { Empty, Preparing, Active, Closing, Quarantined, Closed };
enum class InitStatus { Refused, Disabled, Busy, Activated, ExistingReadOnly, Terminal };
enum class RevocationCause { Disabled, SessionClose, OwnerLost };
enum class Retirement { Unknown, Outstanding, Retired };
enum class InitialOwner : std::size_t
{ Topology, Configuration, Strategy, Profile, Resource, Recording, Provider, FrameGeneration, History, Metadata, LegacyExclusion, Count };
inline constexpr std::size_t InitialOwnerCount=static_cast<std::size_t>(InitialOwner::Count);
struct NativeScopeEnrollment
{
    C::SessionId session; C::RenderStreamId stream; C::ViewId view; C::ScopeRef scope;
    bool operator==(const NativeScopeEnrollment&)const=default;
};
struct NativeDrainReadiness
{
    std::array<Retirement,InitialOwnerCount> owners{};
    Retirement preparation=Retirement::Unknown,callbacks=Retirement::Unknown,unsealedClaims=Retirement::Unknown;
    bool AllRetired()const noexcept
    {
        if(preparation!=Retirement::Retired||callbacks!=Retirement::Retired||unsealedClaims!=Retirement::Retired)return false;
        for(auto r:owners)if(r!=Retirement::Retired)return false;
        return true;
    }
};
struct MaterializationRecord
{
    NativeScopeEnrollment enrollment;
    C::PlanCommit plan; C::RoutingDecision decision; PlanAnchor anchor;
    C::NativeDeliveryContractV1 delivery;
    C::NativeSampleIdentityV1 initialSample;
    Protocol::NativeProfilePreparationProjection preparation;
    // Pins original owner publications, never republishes their bodies as ours.
    std::shared_ptr<const void> metadata;
};

namespace Detail
{
// Bounded transitive validation. Cycles or excessive closure fail before issue.
template<class T>struct MetadataTarget { static constexpr bool value=false; };
template<class T>struct MetadataTarget<C::MetadataRef<T>> { static constexpr bool value=true; };
template<class T,class Reader>bool Closure(const T& v,const Reader& reader,std::size_t& remaining)
{
    if(!remaining)return false;
    --remaining;
    if constexpr(MetadataTarget<T>::value)
    {const auto* body=Context::ResolveMetadata(v,reader);return body&&Closure(*body,reader,remaining);}
    else if constexpr(requires{v.KnownPart();v.UnknownPart();})
    {return !v.IsKnown()||Closure(v.KnownPart()->value,reader,remaining);}
    else if constexpr(requires{v.has_value();*v;})return !v||Closure(*v,reader,remaining);
    else if constexpr(requires{v.index();})return std::visit([&](const auto& part){return Closure(part,reader,remaining);},v);
    else if constexpr(requires{T::Fields();})return std::apply([&](const auto&... f){return (Closure(v.*(f.pointer),reader,remaining)&&...);},T::Fields());
    else if constexpr(requires{v.Entries();}){for(const auto& e:v.Entries())if(!Closure(e,reader,remaining))return false;return true;}
    else if constexpr(requires{v.begin();v.end();}){for(const auto& e:v)if(!Closure(e,reader,remaining))return false;return true;}
    else return true;
}
}

// Held by the registered external owners, not by diagnostic callers. A concrete
// implementation must keep every prerequisite owner locked/reserved until
// Activated or Cancel has completed. Neither callback runs under the slot lock.
class PreparedInitialNative
{
    friend class StreamCoordinatorKernel;
    std::shared_ptr<MaterializationRecord> record_;
    bool consumed_=false;
  protected:
    PreparedInitialNative()=default;
    template<class Store>bool Prepare(const NativeScopeEnrollment& enrolled,const RoutingInput& input,
        const Protocol::NativeProfilePreparationRequest& request,const C::RecordHeader& decisionHeader,
        const std::shared_ptr<Store>& store)
    {
        if(!store||input.scope!=enrolled.scope||input.incumbent.current||input.legacyEffective||
            input.intent.mode.View()!="FixedLegacy"||!input.intent.fixedStrategy||
            input.intent.fixedStrategy->View()!="NativeTemporal"||
            !input.intent.fixedPlacement||*input.intent.fixedPlacement!=request.placement||
            (request.placement!=C::Placement::NativeBefore&&request.placement!=C::Placement::NativeAfter)||
            !Context::Established(input.frame.sessionId)||input.frame.sessionId.KnownPart()->value!=enrolled.session||
            !Context::Established(input.frame.renderStreamId)||input.frame.renderStreamId.KnownPart()->value!=enrolled.stream||
            !Context::Established(input.frame.viewId)||input.frame.viewId.KnownPart()->value!=enrolled.view)return false;
        const auto selected=Select(input,*store);
        if(!selected.valid||!selected.proposed||selected.proposed->strategy!=*input.intent.fixedStrategy||
            selected.proposed->placement!=*input.intent.fixedPlacement||selected.proposed->profile!=request.requestedProfile)return false;
        if(!selected.proposed->nativeDelivery)return false;
        const auto* sample=Context::ResolveNativeSample(input.frame,*store);
        if(!sample)return false;
        const auto* preparedContext=Context::ResolveMetadata(request.context,*store);
        const auto* preparedFrame=preparedContext?Context::ResolveMetadata(preparedContext->frame,*store):nullptr;
        if(!preparedFrame||*preparedFrame!=input.frame)return false;
        const auto* delivery=Context::ResolveMetadata(*selected.proposed->nativeDelivery,*store);
        if(!delivery||selected.proposed->nativeDelivery->owner!=C::OwnerDomain::StreamCoordinator||
            selected.proposed->nativeDelivery->schemaVersion!=C::SchemaVersion{1,0}||
            delivery->profile!=request.requestedProfile||delivery->placement!=request.placement)return false;
        if constexpr(requires{store->reason.Assign("ORCH.Prepare.Profile");})store->reason.Assign("ORCH.Prepare.Profile");
        auto projection=Protocol::PrepareNativeProfile(request,*store);
        if(!projection.projection)
        {if constexpr(requires{store->reason=projection.reason;})store->reason=projection.reason;return false;}
        if(projection.projection->Value().profileCertificate.header.scope!=enrolled.scope)return false;
        if constexpr(requires{store->reason.Assign("ORCH.Prepare.Decision");})store->reason.Assign("ORCH.Prepare.Decision");
        auto decision=PublishDecision(input,selected,decisionHeader,1,*store);
        if(!decision||!Context::Established(decision->commitBoundary.kind))return false;
        auto record=std::make_shared<MaterializationRecord>();
        record->enrollment=enrolled;record->anchor=*selected.proposed;record->decision=*decision;record->delivery=*delivery;
        record->initialSample=*sample;
        record->preparation=projection.projection->Value();record->metadata=store;
        auto& plan=record->plan;plan.decision={Symbol(C::RoutingDecision::WireName),decision->header.record,decision->header.revision};
        plan.boundary=decision->commitBoundary;
        plan.nativeDelivery=selected.proposed->nativeDelivery;
        C::BoundedList<C::ContractRef<C::ContractId::C04>,32> certificates;
        const auto& profile=record->preparation.profileCertificate;
        certificates.Push({Symbol(C::QualificationCertificate::WireName),profile.header.record,profile.header.revision});
        if(record->preparation.semanticCertificate)certificates.Push(*record->preparation.semanticCertificate);
        plan.ownerCertificates.count=static_cast<std::uint32_t>(certificates.Size());
        plan.ownerCertificates.backing=store->Publish(certificates,C::OwnerDomain::StreamCoordinator);
        if constexpr(requires{store->reason.Assign("ORCH.Prepare.TransitiveClosure");})store->reason.Assign("ORCH.Prepare.TransitiveClosure");
        std::size_t budget=16384;
        if(!Detail::Closure(record->decision,*store,budget)||!Detail::Closure(record->anchor,*store,budget)||
            !Detail::Closure(record->preparation.profileCertificate,*store,budget)||
            !Detail::Closure(record->preparation.inputContext,*store,budget)||
            !Detail::Closure(record->preparation.inputSettings,*store,budget)||
            !Detail::Closure(record->preparation.inputRepresentations,*store,budget)||
            !Detail::Closure(plan.ownerCertificates,*store,budget)||!Detail::Closure(plan.nativeDelivery,*store,budget))return false;
        record_=std::move(record);return true;
    }
  public:
    PreparedInitialNative(const PreparedInitialNative&)=delete;
    PreparedInitialNative& operator=(const PreparedInitialNative&)=delete;
    virtual ~PreparedInitialNative()=default;
    virtual Retirement Cancel()noexcept=0;
    virtual void Activated()noexcept=0;
};
class InitialNativeOwnerPort
{
  public:
    virtual ~InitialNativeOwnerPort()=default;
    // Explicit opt-in and legacy exclusion are checked under the held owner
    // guards in Prepare. An absent port or guard never means ready.
    virtual bool Enabled()noexcept=0;
    // Read the requested stable meaning under the actual owner's synchronization.
    // This is only an idempotent initialization check, never execution admission.
    // Unknown/unsupported owners cannot silently accept a changed contract.
    virtual bool MatchesActive(const MaterializationRecord&)noexcept{return false;}
    virtual std::unique_ptr<PreparedInitialNative> Prepare(const NativeScopeEnrollment&)=0;
    // Must stop ingress and return an observation under owner synchronization.
    // Retired is terminal for this genuine scope; no same-scope reopening.
    virtual NativeDrainReadiness ObserveRetirement()noexcept=0;
};
class CurrentRouteView
{
    friend class StreamCoordinatorKernel;
    std::shared_ptr<Lifecycle::NativeSessionLifetime> root_;
    std::shared_ptr<const MaterializationRecord> record_;
    CurrentRouteView(std::shared_ptr<Lifecycle::NativeSessionLifetime> root,std::shared_ptr<const MaterializationRecord> record)
        :root_(std::move(root)),record_(std::move(record)){}
  public:
    const C::PlanCommit& Plan()const noexcept{return record_->plan;}
    const PlanAnchor& Anchor()const noexcept{return record_->anchor;}
    const NativeScopeEnrollment& Scope()const noexcept{return record_->enrollment;}
};
struct InitResult { InitStatus status=InitStatus::Refused;std::optional<CurrentRouteView> current; };
struct CoordinatorSnapshot { RoutePhase phase=RoutePhase::Empty;std::uint64_t successfulIssues=0,publications=0; };
struct ClosureResult { bool closed=false;bool finalizerStopped=false; };
#ifdef NR_ORCH_INIT_TESTING
enum class TestFault { None, IssuerBusy, AfterIssue };
class InitialNativeTestAccess;
#endif
class StreamCoordinatorKernel
{
    friend class Lifecycle::NativeSessionLifetime;
    friend class InitialFixedNativeCoordinator;
    friend class Protocol::NativeInvocationAdmission;
    friend class Protocol::NativeInvocationIngress;
#ifdef NR_ORCH_INIT_TESTING
    friend class InitialNativeTestAccess;
    void SetTestingFault(TestFault);
#endif
    struct Impl;std::unique_ptr<Impl> impl_;
    explicit StreamCoordinatorKernel(Lifecycle::NativeSessionLifetime& root);
    InitResult Activate();
    ClosureResult Close();
    void SweepInvocations();
    std::optional<Protocol::NativeInvocationAdmission> AdmitInvocation(std::shared_ptr<Protocol::NativeInvocationOwnerPort>);
    void DropInvocation(std::size_t,std::uint64_t)noexcept;
    bool ClaimInvocation(std::size_t,std::uint64_t);
    bool SubmitInvocation(std::size_t,std::uint64_t,const C::EvaluationResult&);
    bool SubmitStageDelivery(const C::NativeStageDeliveryV1&);
    bool SubmitCommittedStage(const Lifecycle::PrimaryNrClaimHandle&);
    bool AcknowledgeFinalizerRetirement(const Lifecycle::FinalizerRetirementAcknowledgment&);
    bool AcknowledgeFinalConsumption(const Lifecycle::FinalizerConsumptionAcknowledgment&);
    bool AcknowledgeFinalInterruption(const Lifecycle::FinalizerInterruptionAcknowledgment&);
    Lifecycle::FinalizationResult ApplyFinalConsumer(const Lifecycle::PrimaryNrClaimHandle&,Lifecycle::NativeFinalConsumerAction&);
    bool RetainFinalConsumer(const Lifecycle::PrimaryNrClaimHandle&,std::shared_ptr<Lifecycle::NativeFinalConsumerTail>);
    bool FinalConsumerLogicalClosed(const Lifecycle::PrimaryNrClaimHandle&)const;
    std::optional<Lifecycle::PrimaryNrClaimHandle> InvocationClaim(std::size_t,std::uint64_t)const;
    std::optional<Lifecycle::SourceBoundClaimHandle> InvocationSourceBoundClaim(std::size_t,std::uint64_t)const;
    Lifecycle::SourceBoundResult ApplySourceBoundConsumer(const Lifecycle::SourceBoundClaimHandle&,Lifecycle::NativeSourceBoundConsumerAction&);
    bool RetainSourceBoundConsumer(const Lifecycle::SourceBoundClaimHandle&,std::shared_ptr<Lifecycle::NativeSourceBoundConsumerTail>);
    C::NativeDeliveryKind InvocationDelivery(std::size_t,std::uint64_t)const noexcept;
    std::optional<C::NativeSampleIdentityV1> InvocationSample(std::size_t,std::uint64_t)const;
    const Protocol::RecipeProduct* InvocationProduct(std::size_t,std::uint64_t)const noexcept;
    std::unique_ptr<Protocol::NativeOwnerActionGuard> InvocationAction(std::size_t,std::uint64_t,Protocol::NativeCheckPoint);
    bool StartInvocationModel(std::size_t,std::uint64_t,const Protocol::NativeOwnerActionGuard&);
    bool InvocationModelCalled(std::size_t,std::uint64_t);
    void InvocationModelReturned(std::size_t,std::uint64_t);
  public:
    ~StreamCoordinatorKernel();
    StreamCoordinatorKernel(const StreamCoordinatorKernel&)=delete;
    StreamCoordinatorKernel& operator=(const StreamCoordinatorKernel&)=delete;
    InitResult Read();
    void Revoke(RevocationCause);
    CoordinatorSnapshot Inspect()const;
};
}
