#pragma once
#include "NativeOwnerSet.h"
#include "NativeExecutionLedger.h"
#include <nr/orchestration/InitialFixedNativeCoordinator.h>
namespace Neurotic::Lifecycle
{
class NativeFinalConsumerAction;
class NativeFinalConsumerTail;
class NativeSourceBoundConsumerAction;
class NativeSourceBoundConsumerTail;
// No public construction/enrollment from a Symbol or serialized scope. The
// actual process bootstrap owns and retains this root through aggregate drain.
class NativeSessionLifetime:public std::enable_shared_from_this<NativeSessionLifetime>
{
    friend class NativeProcessBootstrap;
    friend class Orchestration::StreamCoordinatorKernel;
    friend class Orchestration::InitialFixedNativeCoordinator;
    friend class Protocol::NativeInvocationAdmission;
    friend class Protocol::NativeInvocationIngress;
#ifdef NR_ORCH_INIT_TESTING
    friend class Orchestration::InitialNativeTestAccess;
#endif
    std::unique_ptr<NativeOwnerSet> owners_;
    NativeExecutionLedger execution_;
    const Orchestration::NativeScopeEnrollment enrollment_;
    std::shared_ptr<Orchestration::InitialNativeOwnerPort> port_;
    std::unique_ptr<Orchestration::StreamCoordinatorKernel> kernel_;
    NativeSessionLifetime(Orchestration::NativeScopeEnrollment,std::shared_ptr<Orchestration::InitialNativeOwnerPort>);
    static std::shared_ptr<NativeSessionLifetime> Adopt(std::unique_ptr<NativeOwnerSet>&,
        Orchestration::NativeScopeEnrollment,std::shared_ptr<Orchestration::InitialNativeOwnerPort>);
  public:
    ~NativeSessionLifetime();
    NativeSessionLifetime(const NativeSessionLifetime&)=delete;
    NativeSessionLifetime& operator=(const NativeSessionLifetime&)=delete;
    Orchestration::InitialFixedNativeCoordinator Coordinator(){return Orchestration::InitialFixedNativeCoordinator(shared_from_this());}
    Orchestration::ClosureResult AdvanceClosure();
    // Retained owner joins remain callable after the callback admission drops.
    // Neither operation fabricates provider enrollment or identity exclusion.
    bool SubmitStageDelivery(const C::NativeStageDeliveryV1&);
    bool SubmitCommittedStage(const PrimaryNrClaimHandle&);
    bool AcknowledgeFinalizerRetirement(const FinalizerRetirementAcknowledgment&);
    bool AcknowledgeFinalConsumption(const FinalizerConsumptionAcknowledgment&);
    bool AcknowledgeFinalInterruption(const FinalizerInterruptionAcknowledgment&);
    FinalizationResult ApplyFinalConsumer(const PrimaryNrClaimHandle&,NativeFinalConsumerAction&);
    bool RetainFinalConsumer(const PrimaryNrClaimHandle&,std::shared_ptr<NativeFinalConsumerTail>);
    bool FinalConsumerLogicalClosed(const PrimaryNrClaimHandle&)const;
    SourceBoundResult ApplySourceBoundConsumer(const SourceBoundClaimHandle&,NativeSourceBoundConsumerAction&);
    bool RetainSourceBoundConsumer(const SourceBoundClaimHandle&,std::shared_ptr<NativeSourceBoundConsumerTail>);
};
}
