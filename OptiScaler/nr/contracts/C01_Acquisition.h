#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
using CandidateValue = std::variant<ScalarValue, MetadataRef<ResourceView>>;

struct AcquisitionCandidate
{
    RecordHeader header = RecordHeader {ContractId::C01};
    RecordKey provider {};
    OptionalFact<ProviderIncarnation> providerIncarnation {};
    Symbol sourceSchema {};
    OptionalFact<VersionNumber> sourceVersion {};
    SemanticKind semantic = SemanticKind::Color;
    OptionalFact<CandidateValue> payload {};
    BoundaryDescription boundary {};
    MetadataRef<FrameIdentity> frame {};
    std::uint64_t observationRevision {};
    OptionalFact<ContentRevision> contentRevision {};
    MetadataList<SemanticClaim, 16> claims {};
    MetadataRef<EvidenceVector> evidence {};
    AccessRequirements descriptiveLifetime {};
    OptionalFact<MetadataRef<ColorDescription>> color {};
    OptionalFact<MetadataRef<DepthDescription>> depth {};
    OptionalFact<MetadataRef<MotionDescription>> motion {};
    OptionalFact<MetadataRef<ExposureDescription>> exposure {};
    OptionalFact<Vec2> jitter {};
    MetadataRef<MaskSet> masks {};
    OptionalFact<BoundedList<double, 16>> cameraTransform {};
    OptionalFact<bool> cameraCut {};
    OptionalFact<bool> rrActive {};
    OptionalFact<bool> fgActive {};
    OptionalFact<Symbol> algorithmRevision {};
    OptionalFact<MetadataRef<CostEstimate>> cost {};
    inline static constexpr std::string_view WireName = "AcquisitionCandidate";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &AcquisitionCandidate::header),
            Field("provider", &AcquisitionCandidate::provider),
            Field("providerIncarnation", &AcquisitionCandidate::providerIncarnation),
            Field("sourceSchema", &AcquisitionCandidate::sourceSchema),
            Field("sourceVersion", &AcquisitionCandidate::sourceVersion),
            Field("semantic", &AcquisitionCandidate::semantic),
            Field("payload", &AcquisitionCandidate::payload),
            Field("boundary", &AcquisitionCandidate::boundary),
            Field("frame", &AcquisitionCandidate::frame),
            Field("observationRevision", &AcquisitionCandidate::observationRevision),
            Field("contentRevision", &AcquisitionCandidate::contentRevision),
            Field("claims", &AcquisitionCandidate::claims),
            Field("evidence", &AcquisitionCandidate::evidence),
            Field("descriptiveLifetime", &AcquisitionCandidate::descriptiveLifetime),
            Field("color", &AcquisitionCandidate::color),
            Field("depth", &AcquisitionCandidate::depth),
            Field("motion", &AcquisitionCandidate::motion),
            Field("exposure", &AcquisitionCandidate::exposure),
            Field("jitter", &AcquisitionCandidate::jitter),
            Field("masks", &AcquisitionCandidate::masks),
            Field("cameraTransform", &AcquisitionCandidate::cameraTransform),
            Field("cameraCut", &AcquisitionCandidate::cameraCut),
            Field("rrActive", &AcquisitionCandidate::rrActive),
            Field("fgActive", &AcquisitionCandidate::fgActive),
            Field("algorithmRevision", &AcquisitionCandidate::algorithmRevision),
            Field("cost", &AcquisitionCandidate::cost)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C01) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Acquisition, OwnerDomain::Provider) != Error::None)
            return Error::WrongOwner;
        if (sourceSchema.Empty())
            return Error::MissingField;
        return Error::None;
    }
    bool operator==(const AcquisitionCandidate&) const = default;
};

// Grouping is not a coherence certificate. Candidate bodies are separate immutable records.
struct ObservationSet
{
    RecordHeader header = RecordHeader {ContractId::C01};
    MetadataList<ContractRef<ContractId::C01>, 64> candidates {};
    BoundaryDescription boundary {};
    inline static constexpr std::string_view WireName = "ObservationSet";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &ObservationSet::header),
            Field("candidates", &ObservationSet::candidates),
            Field("boundary", &ObservationSet::boundary)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C01) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Acquisition, OwnerDomain::Provider) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const ObservationSet&) const = default;
};

} // namespace Neurotic::Contracts
