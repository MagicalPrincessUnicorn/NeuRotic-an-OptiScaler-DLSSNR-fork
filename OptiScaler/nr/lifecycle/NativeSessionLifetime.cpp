#include "NativeSessionLifetime.h"
#include "NativeInitialMaterialization.h"
#include "Fsr3SourceBoundConsumerPublication.h"
namespace Neurotic::Lifecycle
{
// Compile the concrete production reader/Resource/session specialization even
// while actual SDK qualification correctly prevents source-bound enrollment.
template class Fsr3SourceBoundConsumerPublication<NativeInitialMaterialization::Store>;
NativeSessionLifetime::NativeSessionLifetime(Orchestration::NativeScopeEnrollment enrollment,
    std::shared_ptr<Orchestration::InitialNativeOwnerPort> port)
    :enrollment_(std::move(enrollment)),port_(std::move(port)),
     kernel_(new Orchestration::StreamCoordinatorKernel(*this))
{}
std::shared_ptr<NativeSessionLifetime> NativeSessionLifetime::Adopt(std::unique_ptr<NativeOwnerSet>& owners,
    Orchestration::NativeScopeEnrollment enrollment,std::shared_ptr<Orchestration::InitialNativeOwnerPort> port)
{
    if(!owners||!owners->Ready()||!port||!owners->Session()||*owners->Session()!=enrollment.session||
        !ValidIdentity(enrollment.stream.Describe())||!ValidIdentity(enrollment.view.Describe())||
        !enrollment.scope.key||!Context::ValidValues(enrollment.scope))throw std::invalid_argument("Native enrollment");
    // Validation, object, shared control block, kernel and Impl can all throw.
    // None may consume the process root's unique owner. The final tail cannot.
    auto result=std::shared_ptr<NativeSessionLifetime>(new NativeSessionLifetime(std::move(enrollment),std::move(port)));
    result->owners_=std::move(owners);
    return result;
}
NativeSessionLifetime::~NativeSessionLifetime()=default;
Orchestration::ClosureResult NativeSessionLifetime::AdvanceClosure(){return kernel_->Close();}
bool NativeSessionLifetime::SubmitStageDelivery(const C::NativeStageDeliveryV1& stage)
{return kernel_->SubmitStageDelivery(stage);}
bool NativeSessionLifetime::SubmitCommittedStage(const PrimaryNrClaimHandle& claim)
{return kernel_->SubmitCommittedStage(claim);}
bool NativeSessionLifetime::AcknowledgeFinalizerRetirement(const FinalizerRetirementAcknowledgment& acknowledgment)
{return kernel_->AcknowledgeFinalizerRetirement(acknowledgment);}
bool NativeSessionLifetime::AcknowledgeFinalConsumption(const FinalizerConsumptionAcknowledgment& acknowledgment)
{return kernel_->AcknowledgeFinalConsumption(acknowledgment);}
bool NativeSessionLifetime::AcknowledgeFinalInterruption(const FinalizerInterruptionAcknowledgment& acknowledgment)
{return kernel_->AcknowledgeFinalInterruption(acknowledgment);}
FinalizationResult NativeSessionLifetime::ApplyFinalConsumer(const PrimaryNrClaimHandle& claim,NativeFinalConsumerAction& action)
{return kernel_->ApplyFinalConsumer(claim,action);}
bool NativeSessionLifetime::RetainFinalConsumer(const PrimaryNrClaimHandle& claim,std::shared_ptr<NativeFinalConsumerTail> tail)
{return kernel_->RetainFinalConsumer(claim,std::move(tail));}
bool NativeSessionLifetime::FinalConsumerLogicalClosed(const PrimaryNrClaimHandle& claim)const
{return kernel_->FinalConsumerLogicalClosed(claim);}
SourceBoundResult NativeSessionLifetime::ApplySourceBoundConsumer(const SourceBoundClaimHandle& claim,NativeSourceBoundConsumerAction& action)
{return kernel_->ApplySourceBoundConsumer(claim,action);}
bool NativeSessionLifetime::RetainSourceBoundConsumer(const SourceBoundClaimHandle& claim,std::shared_ptr<NativeSourceBoundConsumerTail> tail)
{return kernel_->RetainSourceBoundConsumer(claim,std::move(tail));}
}
namespace Neurotic::Orchestration
{
InitResult InitialFixedNativeCoordinator::TryActivate(){return root_->kernel_->Activate();}
InitResult InitialFixedNativeCoordinator::TryReadCurrent()const{return root_->kernel_->Read();}
void InitialFixedNativeCoordinator::Revoke(RevocationCause cause){root_->kernel_->Revoke(cause);}
CoordinatorSnapshot InitialFixedNativeCoordinator::Inspect()const{return root_->kernel_->Inspect();}
}
