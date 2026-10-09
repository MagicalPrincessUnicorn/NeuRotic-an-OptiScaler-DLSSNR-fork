#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
struct CapabilityGapRequest
{
    RecordHeader header = RecordHeader {ContractId::C10};
    std::uint64_t planningEpoch {};
    ContractRef<ContractId::C02> context {};
    MetadataRef<FrameIdentity> frame {};
    ProfileKey profile {};
    Symbol purpose {};
    MetadataList<Symbol, 32> requiredMissing {};
    MetadataList<Symbol, 32> optionalMissing {};
    MetadataList<ContractRef<ContractId::C01>, 64> preservedCandidates {};
    BoundedList<SourceClass, 8> permittedEvidence {};
    MetadataList<Symbol, 16> permittedMethods {};
    FramePair sourcePair {};
    MetadataRef<CostEstimate> budget {};
    OptionalFact<RecordKey> deadline {};
    OptionalFact<RecordKey> cancellation {};
    std::uint32_t maximumStages = 1;
    inline static constexpr std::string_view WireName = "CapabilityGapRequest";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &CapabilityGapRequest::header),
            Field("planningEpoch", &CapabilityGapRequest::planningEpoch),
            Field("context", &CapabilityGapRequest::context),
            Field("frame", &CapabilityGapRequest::frame),
            Field("profile", &CapabilityGapRequest::profile),
            Field("purpose", &CapabilityGapRequest::purpose),
            Field("requiredMissing", &CapabilityGapRequest::requiredMissing),
            Field("optionalMissing", &CapabilityGapRequest::optionalMissing),
            Field("preservedCandidates", &CapabilityGapRequest::preservedCandidates),
            Field("permittedEvidence", &CapabilityGapRequest::permittedEvidence),
            Field("permittedMethods", &CapabilityGapRequest::permittedMethods),
            Field("sourcePair", &CapabilityGapRequest::sourcePair),
            Field("budget", &CapabilityGapRequest::budget),
            Field("deadline", &CapabilityGapRequest::deadline),
            Field("cancellation", &CapabilityGapRequest::cancellation),
            Field("maximumStages", &CapabilityGapRequest::maximumStages)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C10) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::OrchestratorPolicy) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const CapabilityGapRequest&) const = default;
};

struct ProviderOffer
{
    RecordHeader header = RecordHeader {ContractId::C10};
    ContractRef<ContractId::C10> request {};
    std::uint64_t planningEpoch {};
    ProviderIncarnation provider {};
    MetadataList<Symbol, 32> producibleFields {};
    SourceClass evidenceClass = SourceClass::Derived;
    OptionalFact<GraphicsApi> api {};
    OptionalFact<ObjectIncarnation> device {};
    BoundaryDescription boundary {};
    MetadataList<RecordReference, 16> prerequisites {};
    MetadataRef<CostEstimate> cost {};
    OptionalFact<MetadataRef<HistoryKey>> history {};
    OptionalFact<Symbol> interopContract {};
    OptionalFact<Symbol> runtimeProfile {};
    MetadataList<Symbol, 16> requalificationConditions {};
    OptionalFact<Symbol> retirementContract {};
    inline static constexpr std::string_view WireName = "ProviderOffer";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ProviderOffer::header),
            Field("request", &ProviderOffer::request),
            Field("planningEpoch", &ProviderOffer::planningEpoch),
            Field("provider", &ProviderOffer::provider),
            Field("producibleFields", &ProviderOffer::producibleFields),
            Field("evidenceClass", &ProviderOffer::evidenceClass),
            Field("api", &ProviderOffer::api),
            Field("device", &ProviderOffer::device),
            Field("boundary", &ProviderOffer::boundary),
            Field("prerequisites", &ProviderOffer::prerequisites),
            Field("cost", &ProviderOffer::cost),
            Field("history", &ProviderOffer::history),
            Field("interopContract", &ProviderOffer::interopContract),
            Field("runtimeProfile", &ProviderOffer::runtimeProfile),
            Field("requalificationConditions", &ProviderOffer::requalificationConditions),
            Field("retirementContract", &ProviderOffer::retirementContract)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C10) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Provider, OwnerDomain::Acquisition) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ProviderOffer&) const = default;
};

} // namespace Neurotic::Contracts
