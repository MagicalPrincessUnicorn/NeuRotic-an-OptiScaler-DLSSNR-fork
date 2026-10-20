#include "ContractTestSupport.h"
#include "ContractSpectreLiteralFixtures.h"
#include <memory>

using namespace Neurotic::Contracts;
using namespace Neurotic::Contracts::Test;

// Candidate engineering refusal budgets, not a claim of measured sizes. No game-thread stack change.
template<class T> void CheckRoot(const char* id, std::string_view literal, OwnerDomain expectedOwner)
{
    static_assert(sizeof(T) <= 32 * 1024, "ARCH root exceeds proposed inline metadata budget");
    auto decoded = Codec::Decode<T>(literal);
    NR_CHECK(id, decoded.error == Error::None && decoded.record.has_value());
    const auto& value = decoded.record->Value();
    NR_CHECK(id, value.header.owner == expectedOwner);
    NR_CHECK(id, value.header.record.value == UINT64_C(9007199254740993));
    NR_CHECK(id, value.header.revision == 42);
    Codec::MetadataCatalog catalog;
    NR_CHECK(id, Codec::ImportMetadata(literal, catalog) == Error::None);
    auto encoded = Codec::EncodeWithMetadata(value, catalog);
    NR_CHECK(id, encoded.error == Error::None);
    auto again = Codec::Decode<T>(encoded.text);
    NR_CHECK(id, again.error == Error::None && again.record->Value() == value);
    std::printf("ROOT_SIZE %s bytes=%zu alignment=%zu\n", id, sizeof(T), alignof(T));
}
static_assert(sizeof(DiagnosticEvent) <= 8 * 1024, "M0 envelope needs a smaller value/reference shape");
static_assert(std::is_trivially_copyable_v<MetadataRef<FrameIdentity>>);
static_assert(std::is_same_v<decltype(MultipassNode::settings), OwnerValueReference>);
static_assert(!std::is_convertible_v<MetadataRef<ResourceView>, ConsumptionLease>);
static_assert(!std::is_convertible_v<MetadataRef<ResourceView>, ResourceView*>);
int main()
{
    CheckRoot<AcquisitionCandidate>("root-AcquisitionCandidate", Literals::AcquisitionCandidate(), OwnerDomain::Acquisition);
    CheckRoot<ObservationSet>("root-ObservationSet", Literals::ObservationSet(), OwnerDomain::Acquisition);
    CheckRoot<CanonicalFrameContext>("root-CanonicalFrameContext", Literals::CanonicalFrameContext(), OwnerDomain::Context);
    CheckRoot<DependencyProof>("root-DependencyProof", Literals::DependencyProof(), OwnerDomain::Resource);
    CheckRoot<ConsumptionLease>("root-ConsumptionLease", Literals::ConsumptionLease(), OwnerDomain::Resource);
    CheckRoot<QualificationCertificate>("root-QualificationCertificate", Literals::QualificationCertificate(), OwnerDomain::Context);
    CheckRoot<FrameCapabilitySet>("root-FrameCapabilitySet", Literals::FrameCapabilitySet(), OwnerDomain::Context);
    CheckRoot<RoutingDecision>("root-RoutingDecision", Literals::RoutingDecision(), OwnerDomain::OrchestratorPolicy);
    CheckRoot<PlanCommit>("root-PlanCommit", Literals::PlanCommit(), OwnerDomain::StreamCoordinator);
    CheckRoot<NrExecutionRecipe>("root-NrExecutionRecipe", Literals::NrExecutionRecipe(), OwnerDomain::RenderingProtocol);
    CheckRoot<EvaluationResult>("root-EvaluationResult", Literals::EvaluationResult(), OwnerDomain::Strategy);
    CheckRoot<ResetPlan>("root-ResetPlan", Literals::ResetPlan(), OwnerDomain::ResetRules);
    CheckRoot<ResetAcknowledgment>("root-ResetAcknowledgment", Literals::ResetAcknowledgment(), OwnerDomain::Resource);
    CheckRoot<MultipassPlan>("root-MultipassPlan", Literals::MultipassPlan(), OwnerDomain::Multipass);
    CheckRoot<MultipassResult>("root-MultipassResult", Literals::MultipassResult(), OwnerDomain::Multipass);
    CheckRoot<CapabilityGapRequest>("root-CapabilityGapRequest", Literals::CapabilityGapRequest(), OwnerDomain::OrchestratorPolicy);
    CheckRoot<ProviderOffer>("root-ProviderOffer", Literals::ProviderOffer(), OwnerDomain::Provider);
    CheckRoot<OwnerReceipt>("root-OwnerReceipt", Literals::OwnerReceipt(), OwnerDomain::Strategy);
    CheckRoot<DiagnosticEvent>("root-DiagnosticEvent", Literals::DiagnosticEvent(), OwnerDomain::Diagnostics);
    CheckRoot<RepresentationPlan>("root-RepresentationPlan", Literals::RepresentationPlan(), OwnerDomain::Context);
    CheckRoot<PreparedView>("root-PreparedView", Literals::PreparedView(), OwnerDomain::Resource);
    CheckRoot<FinalRealFramePacket>("root-FinalRealFramePacket", Literals::FinalRealFramePacket(), OwnerDomain::Finalizer);
    CheckRoot<FgHandoffReceipt>("root-FgHandoffReceipt", Literals::FgHandoffReceipt(), OwnerDomain::FrameGeneration);
    CheckRoot<IdentityRecord>("root-IdentityRecord", Literals::IdentityRecord(), OwnerDomain::Session);
    CheckRoot<ValidationFixture>("root-ValidationFixture", Literals::ValidationFixture(), OwnerDomain::Validation);
    CheckRoot<LocalExecutionPlan>("root-LocalExecutionPlan", Literals::LocalExecutionPlan(), OwnerDomain::Performance);
    CheckRoot<CostRecord>("root-CostRecord", Literals::CostRecord(), OwnerDomain::Strategy);
    CheckRoot<MultipassPlan>("root-MultipassPlan-16", Literals::MultipassPlan_Nonempty(), OwnerDomain::Multipass);
    CheckRoot<NrExecutionRecipe>("root-Recipe-16", Literals::NrExecutionRecipe_Nonempty(), OwnerDomain::RenderingProtocol);
    auto mp = ReplaceOnce(std::string(Literals::EvaluationResult()), "\"owner\":\"Strategy\"", "\"owner\":\"Multipass\"");
    NR_CHECK("R02-multipass-wire", Codec::Decode<EvaluationResult>(mp).error == Error::None);
    auto identity = ReplaceOnce(std::string(Literals::IdentityRecord()), "\"owner\":\"Session\"", "\"owner\":\"Multipass\"");
    NR_CHECK("R02-identity-wire", Codec::Decode<IdentityRecord>(identity).error == Error::None);
    for (const auto& [owner, expected] : {
             std::pair {"Unspecified", Error::MissingProvenance},
             std::pair {"Diagnostics", Error::WrongOwner},
             std::pair {"Configuration", Error::WrongOwner}})
    {
        const auto bad = ReplaceOnce(mp, "\"owner\":\"Multipass\"", std::string("\"owner\":\"") + owner + "\"");
        NR_CHECK("R02-reject-owner-wire", Codec::Decode<EvaluationResult>(bad).error == expected);
    }
    auto badCount = ReplaceOnce(std::string(Literals::MultipassPlan_Nonempty()), "\"count\":16", "\"count\":15");
    NR_CHECK("R01-count-matches-body", Codec::Decode<MultipassPlan>(badCount).error == Error::Malformed);
    auto overCount = ReplaceOnce(std::string(Literals::MultipassPlan_Nonempty()), "\"count\":16", "\"count\":17");
    NR_CHECK("R01-count-refuses-overflow", Codec::Decode<MultipassPlan>(overCount).error == Error::LimitExceeded);
    auto missing = ReplaceOnce(std::string(Literals::CanonicalFrameContext()), "\"revision\":\"42\",\"schemaVersion\"", "\"revision\":\"43\",\"schemaVersion\"");
    NR_CHECK("R01-missing-exact-revision", Codec::Decode<CanonicalFrameContext>(missing).error == Error::MissingField);
    auto settings = ReplaceOnce(std::string(Literals::MultipassPlan_Nonempty()), "\"owner\":\"Configuration\"", "\"owner\":\"Diagnostics\"");
    NR_CHECK("CP01-settings-owner-wire", Codec::Decode<MultipassPlan>(settings).error == Error::WrongOwner);
    std::puts("COMPLETE literal root fixtures; no live publication or runtime authority established");
    return 0;
}
