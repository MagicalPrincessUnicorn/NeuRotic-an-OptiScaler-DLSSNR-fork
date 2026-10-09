#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C01_Acquisition.h"

namespace Neurotic::Contracts
{
// Observation-only contexts may be incomplete. No executable qualification is inferred from construction.
struct CanonicalFrameContext
{
    RecordHeader header = RecordHeader {ContractId::C02};
    MetadataRef<FrameIdentity> frame {};
    BoundaryDescription boundary {};
    MetadataList<ContractRef<ContractId::C01>, 64> coherentCandidates {};
    OptionalFact<MetadataRef<ColorDescription>> color {};
    MetadataRef<RasterDescription> renderRaster {};
    MetadataRef<RasterDescription> outputRaster {};
    MetadataList<GuideDescription, 8> guides {};
    MetadataList<SemanticClaim, 16> fields {};
    MetadataList<Contradiction, 8> contradictions {};
    MetadataRef<EvidenceVector> continuity {};
    OptionalFact<bool> rrActive {};
    OptionalFact<bool> fgActive {};
    MetadataRef<GenerationVector> generations {};
    MetadataList<ContractRef<ContractId::C12>, 32> representationPlans {};
    MetadataList<AccessRequirements, 16> accessRequirements {};
    OptionalFact<MetadataRef<DepthDescription>> depth {};
    OptionalFact<MetadataRef<MotionDescription>> motion {};
    OptionalFact<MetadataRef<ExposureDescription>> exposure {};
    OptionalFact<Vec2> jitter {};
    MetadataRef<MaskSet> masks {};
    inline static constexpr std::string_view WireName = "CanonicalFrameContext";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &CanonicalFrameContext::header),
            Field("frame", &CanonicalFrameContext::frame),
            Field("boundary", &CanonicalFrameContext::boundary),
            Field("coherentCandidates", &CanonicalFrameContext::coherentCandidates),
            Field("color", &CanonicalFrameContext::color),
            Field("renderRaster", &CanonicalFrameContext::renderRaster),
            Field("outputRaster", &CanonicalFrameContext::outputRaster),
            Field("guides", &CanonicalFrameContext::guides),
            Field("fields", &CanonicalFrameContext::fields),
            Field("contradictions", &CanonicalFrameContext::contradictions),
            Field("continuity", &CanonicalFrameContext::continuity),
            Field("rrActive", &CanonicalFrameContext::rrActive),
            Field("fgActive", &CanonicalFrameContext::fgActive),
            Field("generations", &CanonicalFrameContext::generations),
            Field("representationPlans", &CanonicalFrameContext::representationPlans),
            Field("accessRequirements", &CanonicalFrameContext::accessRequirements),
            Field("depth", &CanonicalFrameContext::depth),
            Field("motion", &CanonicalFrameContext::motion),
            Field("exposure", &CanonicalFrameContext::exposure),
            Field("jitter", &CanonicalFrameContext::jitter),
            Field("masks", &CanonicalFrameContext::masks)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C02) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Context) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const CanonicalFrameContext&) const = default;
};

} // namespace Neurotic::Contracts
