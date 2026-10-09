#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C04_Capability.h"
#include "NativeDelivery.h"

namespace Neurotic::Contracts
{
struct Degradation
{
    Symbol dimension {};
    OptionalFact<std::uint32_t> level {};
    ReasonId reason {};
    inline static constexpr std::string_view WireName = "Degradation";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("dimension", &Degradation::dimension),
            Field("level", &Degradation::level),
            Field("reason", &Degradation::reason)
        };
    }
    bool operator==(const Degradation&) const = default;
};

struct RoutingDecision
{
    RecordHeader header = RecordHeader {ContractId::C05};
    std::uint64_t planningEpoch {};
    MetadataList<RecordReference, 32> inputSnapshots {};
    MetadataList<OwnerValueReference, 16> ownerInputs {};
    MetadataList<Symbol, 16> inputHashes {};
    std::uint32_t ruleVersion = 1;
    OwnerValueReference userIntent {};
    ContractRef<ContractId::C02> context {};
    MetadataList<ContractRef<ContractId::C01>, 64> sourceBundle {};
    Symbol strategy {};
    ProfileKey profile {};
    MetadataList<ContractRef<ContractId::C12>, 32> preparations {};
    OptionalFact<std::optional<ContractRef<ContractId::C09>>> multipass {};
    OptionalFact<std::optional<RecordReference>> fgSubplan {};
    MetadataList<Degradation, 16> degradation {};
    MetadataList<PolicyReason, 16> selectedReasons {};
    MetadataList<PolicyReason, 32> rejectedReasons {};
    Symbol tieBreakKey {};
    BoundaryDescription commitBoundary {};
    OptionalFact<ContractRef<ContractId::C05>> rollbackTarget {};
    MetadataRef<StructuralSignature> signature {};
    OptionalFact<ContractRef<ContractId::C10>> gapRequest {};
    inline static constexpr std::string_view WireName = "RoutingDecision";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &RoutingDecision::header),
            Field("planningEpoch", &RoutingDecision::planningEpoch),
            Field("inputSnapshots", &RoutingDecision::inputSnapshots),
            Field("ownerInputs", &RoutingDecision::ownerInputs),
            Field("inputHashes", &RoutingDecision::inputHashes),
            Field("ruleVersion", &RoutingDecision::ruleVersion),
            Field("userIntent", &RoutingDecision::userIntent),
            Field("context", &RoutingDecision::context),
            Field("sourceBundle", &RoutingDecision::sourceBundle),
            Field("strategy", &RoutingDecision::strategy),
            Field("profile", &RoutingDecision::profile),
            Field("preparations", &RoutingDecision::preparations),
            Field("multipass", &RoutingDecision::multipass),
            Field("fgSubplan", &RoutingDecision::fgSubplan),
            Field("degradation", &RoutingDecision::degradation),
            Field("selectedReasons", &RoutingDecision::selectedReasons),
            Field("rejectedReasons", &RoutingDecision::rejectedReasons),
            Field("tieBreakKey", &RoutingDecision::tieBreakKey),
            Field("commitBoundary", &RoutingDecision::commitBoundary),
            Field("rollbackTarget", &RoutingDecision::rollbackTarget),
            Field("signature", &RoutingDecision::signature),
            Field("gapRequest", &RoutingDecision::gapRequest)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C05) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::OrchestratorPolicy) != Error::None ||
            userIntent.owner != OwnerDomain::Configuration)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const RoutingDecision&) const = default;
};

struct PlanCommit
{
    RecordHeader header = RecordHeader {ContractId::C05};
    ContractRef<ContractId::C05> decision {};
    BoundaryDescription boundary {};
    OptionalFact<RouteGeneration> routeGeneration {};
    MetadataList<ContractRef<ContractId::C04>, 32> ownerCertificates {};
    OptionalFact<bool> committed {};
    OptionalFact<ContractRef<ContractId::C05>> rollbackTarget {};
    std::optional<MetadataRef<NativeDeliveryContractV1>> nativeDelivery;
    inline static constexpr std::string_view WireName = "PlanCommit";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &PlanCommit::header),
            Field("decision", &PlanCommit::decision),
            Field("boundary", &PlanCommit::boundary),
            Field("routeGeneration", &PlanCommit::routeGeneration),
            Field("ownerCertificates", &PlanCommit::ownerCertificates),
            Field("committed", &PlanCommit::committed),
            Field("rollbackTarget", &PlanCommit::rollbackTarget),
            Field("nativeDelivery", &PlanCommit::nativeDelivery)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C05) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::StreamCoordinator) != Error::None)
            return Error::WrongOwner;
        if (nativeDelivery && nativeDelivery->owner != OwnerDomain::StreamCoordinator)
            return Error::WrongOwner;
        if (nativeDelivery && nativeDelivery->schemaVersion != SchemaVersion{1,0})return Error::UnsupportedVersion;
        return Error::None;
    }
    bool operator==(const PlanCommit&) const = default;
};

} // namespace Neurotic::Contracts
