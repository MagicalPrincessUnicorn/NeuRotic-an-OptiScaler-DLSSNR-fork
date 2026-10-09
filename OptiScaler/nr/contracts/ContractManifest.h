#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C01_Acquisition.h"
#include "C02_Context.h"
#include "C03_Consumption.h"
#include "C04_Capability.h"
#include "C05_Routing.h"
#include "C06_ProtocolInput.h"
#include "C07_ProtocolOutput.h"
#include "C08_Reset.h"
#include "C09_Multipass.h"
#include "C10_Gap.h"
#include "C11_Diagnostics.h"
#include "C12_Representation.h"
#include "C13_FinalFrame.h"
#include "C14_Identity.h"
#include "C15_Validation.h"
#include "C16_Execution.h"

namespace Neurotic::Contracts
{
struct ContractManifestEntry
{
    ContractId contract;
    std::string_view records;
    std::string_view operationalOwners;
};
inline constexpr std::array<ContractManifestEntry, 16> ContractManifest {{
    {ContractId::C01, "AcquisitionCandidate / ObservationSet", "Acquisition / Provider"},
    {ContractId::C02, "CanonicalFrameContext", "Context"},
    {ContractId::C03, "DependencyProof / ConsumptionLease", "Resource"},
    {ContractId::C04, "QualificationCertificate / FrameCapabilitySet", "Context / FrameGeneration / Multipass / Resource / Strategy"},
    {ContractId::C05, "RoutingDecision / PlanCommit", "OrchestratorPolicy / StreamCoordinator"},
    {ContractId::C06, "NrExecutionRecipe", "RenderingProtocol"},
    {ContractId::C07, "EvaluationResult", "Strategy / Multipass"},
    {ContractId::C08, "ResetPlan / ResetAcknowledgment", "Context / FrameGeneration / History / Multipass / Provider / ResetRules / Resource / Strategy"},
    {ContractId::C09, "MultipassPlan / MultipassResult", "Multipass"},
    {ContractId::C10, "CapabilityGapRequest / ProviderOffer", "Acquisition / OrchestratorPolicy / Provider"},
    {ContractId::C11, "OwnerReceipt / DiagnosticEvent", "OperationOwner / Diagnostics"},
    {ContractId::C12, "RepresentationPlan / PreparedView", "Context / Resource"},
    {ContractId::C13, "FinalRealFramePacket / FgHandoffReceipt", "Finalizer / FrameGeneration"},
    {ContractId::C14, "IdentityRecord", "ColorContinuity / Context / Finalizer / FrameGeneration / History / IdentityRegistry / OrchestratorPolicy / Presentation / Provider / RayReconstruction / Resource / Session / Strategy / StreamCoordinator / Topology / Multipass"},
    {ContractId::C15, "ValidationFixture", "Validation"},
    {ContractId::C16, "LocalExecutionPlan / CostRecord", "FrameGeneration / Performance / Resource / Strategy"},
}};
} // namespace Neurotic::Contracts
