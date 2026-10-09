#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "C03_Consumption.h"

namespace Neurotic::Contracts
{
// A completed record requires supplied identities/boundary facts. ARCH does not allocate a seal, verify realness or consume a frame.
struct FinalRealFramePacket
{
    RecordHeader header = RecordHeader {ContractId::C13};
    FinalSealId seal {};
    BaseRealFrameId baseRealFrame {};
    RenderStreamId stream {};
    ViewId view {};
    BoundaryDescription consumerBoundary {};
    MetadataList<RecordReference, 32> upstreamReceipts {};
    MetadataRef<ResourceView> output {};
    ContractRef<ContractId::C03> dependency {};
    Symbol handoffContract {};
    std::uint32_t handoffVersion = 1;
    MetadataList<RetentionRegistration, 16> retentions {};
    OptionalFact<Symbol> releaseMethod {};
    OptionalFact<RecordKey> releaseToken {};
    OptionalFact<bool> original {};
    OptionalFact<ReasonId> bypass {};
    MetadataRef<GenerationVector> generations {};
    MetadataRef<MaskSet> masks {};
    OptionalFact<MetadataRef<ResourceView>> hudlessColor {};
    OptionalFact<MetadataRef<ResourceView>> depth {};
    OptionalFact<MetadataRef<ResourceView>> motion {};
    OptionalFact<PresentId> presentationAssociation {};
    OptionalFact<std::uint32_t> expectedGeneratedCount {};
    inline static constexpr std::string_view WireName = "FinalRealFramePacket";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &FinalRealFramePacket::header),
            Field("seal", &FinalRealFramePacket::seal),
            Field("baseRealFrame", &FinalRealFramePacket::baseRealFrame),
            Field("stream", &FinalRealFramePacket::stream),
            Field("view", &FinalRealFramePacket::view),
            Field("consumerBoundary", &FinalRealFramePacket::consumerBoundary),
            Field("upstreamReceipts", &FinalRealFramePacket::upstreamReceipts),
            Field("output", &FinalRealFramePacket::output),
            Field("dependency", &FinalRealFramePacket::dependency),
            Field("handoffContract", &FinalRealFramePacket::handoffContract),
            Field("handoffVersion", &FinalRealFramePacket::handoffVersion),
            Field("retentions", &FinalRealFramePacket::retentions),
            Field("releaseMethod", &FinalRealFramePacket::releaseMethod),
            Field("releaseToken", &FinalRealFramePacket::releaseToken),
            Field("original", &FinalRealFramePacket::original),
            Field("bypass", &FinalRealFramePacket::bypass),
            Field("generations", &FinalRealFramePacket::generations),
            Field("masks", &FinalRealFramePacket::masks),
            Field("hudlessColor", &FinalRealFramePacket::hudlessColor),
            Field("depth", &FinalRealFramePacket::depth),
            Field("motion", &FinalRealFramePacket::motion),
            Field("presentationAssociation", &FinalRealFramePacket::presentationAssociation),
            Field("expectedGeneratedCount", &FinalRealFramePacket::expectedGeneratedCount)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C13) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Finalizer) != Error::None)
            return Error::WrongOwner;
        if (!consumerBoundary.kind.IsKnown() || !consumerBoundary.afterRequiredHostRendering.IsKnown() ||
            !consumerBoundary.afterRequiredHostRendering.KnownPart()->value)
            return Error::MissingField;
        return Error::None;
    }
    bool operator==(const FinalRealFramePacket&) const = default;
};

struct FgHandoffReceipt
{
    RecordHeader header = RecordHeader {ContractId::C13};
    ContractRef<ContractId::C13> packet {};
    ProviderIncarnation provider {};
    OptionalFact<bool> accepted {};
    MetadataList<RetentionRegistration, 16> retentions {};
    OptionalFact<bool> providerReleased {};
    OptionalFact<GeneratedFrameId> generatedFrame {};
    inline static constexpr std::string_view WireName = "FgHandoffReceipt";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &FgHandoffReceipt::header),
            Field("packet", &FgHandoffReceipt::packet),
            Field("provider", &FgHandoffReceipt::provider),
            Field("accepted", &FgHandoffReceipt::accepted),
            Field("retentions", &FgHandoffReceipt::retentions),
            Field("providerReleased", &FgHandoffReceipt::providerReleased),
            Field("generatedFrame", &FgHandoffReceipt::generatedFrame)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C13) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::FrameGeneration) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const FgHandoffReceipt&) const = default;
};

} // namespace Neurotic::Contracts
