#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
enum class Eligibility : std::uint32_t
{
    Unqualified,
    Eligible,
    Ineligible
};
template<> struct EnumTraits<Eligibility>
{
    inline static constexpr auto Values = std::array {
        std::pair {Eligibility::Unqualified, std::string_view {"Unqualified"}},
        std::pair {Eligibility::Eligible, std::string_view {"Eligible"}},
        std::pair {Eligibility::Ineligible, std::string_view {"Ineligible"}}
    };
};

struct QualificationCertificate
{
    RecordHeader header = RecordHeader {ContractId::C04};
    ContractRef<ContractId::C02> context {};
    ProfileKey profile {};
    Symbol purpose {};
    Eligibility eligibility = Eligibility::Unqualified;
    MetadataList<Symbol, 32> missingFields {};
    MetadataList<Contradiction, 8> contradictions {};
    BoundedList<SourceClass, 8> acceptedEvidence {};
    OptionalFact<std::uint64_t> maximumAge {};
    std::optional<NativeSampleFreshnessV1> nativeFreshness;
    MetadataList<RecordReference, 16> rasterCoverage {};
    BoundedList<ColorDomain, 4> colorCoverage {};
    BoundedList<Placement, 4> placements {};
    MetadataList<PolicyReason, 16> reasons {};
    std::uint32_t evidenceVersion = 1;
    MetadataRef<StructuralSignature> signature {};
    OptionalFact<Symbol> qualityBand {};
    inline static constexpr std::string_view WireName = "QualificationCertificate";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &QualificationCertificate::header),
            Field("context", &QualificationCertificate::context),
            Field("profile", &QualificationCertificate::profile),
            Field("purpose", &QualificationCertificate::purpose),
            Field("eligibility", &QualificationCertificate::eligibility),
            Field("missingFields", &QualificationCertificate::missingFields),
            Field("contradictions", &QualificationCertificate::contradictions),
            Field("acceptedEvidence", &QualificationCertificate::acceptedEvidence),
            Field("maximumAge", &QualificationCertificate::maximumAge),
            Field("nativeFreshness", &QualificationCertificate::nativeFreshness),
            Field("rasterCoverage", &QualificationCertificate::rasterCoverage),
            Field("colorCoverage", &QualificationCertificate::colorCoverage),
            Field("placements", &QualificationCertificate::placements),
            Field("reasons", &QualificationCertificate::reasons),
            Field("evidenceVersion", &QualificationCertificate::evidenceVersion),
            Field("signature", &QualificationCertificate::signature),
            Field("qualityBand", &QualificationCertificate::qualityBand)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C04) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Context, OwnerDomain::Resource, OwnerDomain::Strategy, OwnerDomain::Multipass, OwnerDomain::FrameGeneration) != Error::None)
            return Error::WrongOwner;
        if (purpose.Empty())
            return Error::MissingField;
        if(nativeFreshness&&maximumAge.IsKnown())return Error::ContradictoryEntry;
        return Error::None;
    }
    bool operator==(const QualificationCertificate&) const = default;
};

struct FrameCapabilitySet
{
    RecordHeader header = RecordHeader {ContractId::C04};
    ContractRef<ContractId::C02> context {};
    MetadataList<ContractRef<ContractId::C04>, 32> certificates {};
    MetadataList<ContractRef<ContractId::C02>, 16> alternativeBundles {};
    inline static constexpr std::string_view WireName = "FrameCapabilitySet";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &FrameCapabilitySet::header),
            Field("context", &FrameCapabilitySet::context),
            Field("certificates", &FrameCapabilitySet::certificates),
            Field("alternativeBundles", &FrameCapabilitySet::alternativeBundles)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C04) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Context) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const FrameCapabilitySet&) const = default;
};

} // namespace Neurotic::Contracts
