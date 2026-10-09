#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
struct RetentionRegistration
{
    RecordKey consumer {};
    OptionalFact<RecordKey> registration {};
    OptionalFact<bool> released {};
    OptionalFact<Symbol> releaseMethod {};
    OptionalFact<RecordKey> releaseToken {};
    inline static constexpr std::string_view WireName = "RetentionRegistration";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("consumer", &RetentionRegistration::consumer),
            Field("registration", &RetentionRegistration::registration),
            Field("released", &RetentionRegistration::released),
            Field("releaseMethod", &RetentionRegistration::releaseMethod),
            Field("releaseToken", &RetentionRegistration::releaseToken)
        };
    }
    bool operator==(const RetentionRegistration&) const = default;
};

// Owner-issued facts are orthogonal. No conjunction here grants consumption or reuse.
struct DependencyProof
{
    RecordHeader header = RecordHeader {ContractId::C03};
    OptionalFact<ObjectIncarnation> producerDevice {};
    OptionalFact<ObjectIncarnation> producerQueue {};
    OptionalFact<ObjectIncarnation> consumerQueue {};
    OptionalFact<RecordKey> recording {};
    OptionalFact<RecordKey> submission {};
    OptionalFact<RecordKey> ownerDependency {};
    OptionalFact<bool> orderedForConsumer {};
    OptionalFact<bool> gpuCompleted {};
    OptionalFact<bool> recordingNonReplayable {};
    OptionalFact<bool> providerReleased {};
    OptionalFact<bool> storageReusable {};
    MetadataRef<GenerationVector> generations {};
    inline static constexpr std::string_view WireName = "DependencyProof";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &DependencyProof::header),
            Field("producerDevice", &DependencyProof::producerDevice),
            Field("producerQueue", &DependencyProof::producerQueue),
            Field("consumerQueue", &DependencyProof::consumerQueue),
            Field("recording", &DependencyProof::recording),
            Field("submission", &DependencyProof::submission),
            Field("ownerDependency", &DependencyProof::ownerDependency),
            Field("orderedForConsumer", &DependencyProof::orderedForConsumer),
            Field("gpuCompleted", &DependencyProof::gpuCompleted),
            Field("recordingNonReplayable", &DependencyProof::recordingNonReplayable),
            Field("providerReleased", &DependencyProof::providerReleased),
            Field("storageReusable", &DependencyProof::storageReusable),
            Field("generations", &DependencyProof::generations)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C03) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Resource) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const DependencyProof&) const = default;
};

// A contract description, not a live lease object. Live state and validation methods remain exclusively with LIFE/resource owners.
struct ConsumptionLease
{
    RecordHeader header = RecordHeader {ContractId::C03};
    ContractRef<ContractId::C02> context {};
    ContractRef<ContractId::C01> candidate {};
    ResourceIdentityToken resource {};
    UsageKind use = UsageKind::CallbackRead;
    ScopeRef commandScope {};
    OptionalFact<ObjectIncarnation> device {};
    OptionalFact<ObjectIncarnation> queue {};
    OptionalFact<ContractRef<ContractId::C03>> producerDependency {};
    MetadataRef<GenerationVector> generations {};
    OptionalFact<RecordKey> expirationBoundary {};
    OptionalFact<RecordKey> revocationEpoch {};
    OptionalFact<bool> revoked {};
    MetadataList<RetentionRegistration, 16> retentions {};
    OptionalFact<Symbol> releaseMethod {};
    OptionalFact<RecordKey> releaseToken {};
    OptionalFact<bool> submissionRecheckRequired {};
    inline static constexpr std::string_view WireName = "ConsumptionLease";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ConsumptionLease::header),
            Field("context", &ConsumptionLease::context),
            Field("candidate", &ConsumptionLease::candidate),
            Field("resource", &ConsumptionLease::resource),
            Field("use", &ConsumptionLease::use),
            Field("commandScope", &ConsumptionLease::commandScope),
            Field("device", &ConsumptionLease::device),
            Field("queue", &ConsumptionLease::queue),
            Field("producerDependency", &ConsumptionLease::producerDependency),
            Field("generations", &ConsumptionLease::generations),
            Field("expirationBoundary", &ConsumptionLease::expirationBoundary),
            Field("revocationEpoch", &ConsumptionLease::revocationEpoch),
            Field("revoked", &ConsumptionLease::revoked),
            Field("retentions", &ConsumptionLease::retentions),
            Field("releaseMethod", &ConsumptionLease::releaseMethod),
            Field("releaseToken", &ConsumptionLease::releaseToken),
            Field("submissionRecheckRequired", &ConsumptionLease::submissionRecheckRequired)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C03) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Resource) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ConsumptionLease&) const = default;
};

} // namespace Neurotic::Contracts
