#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "Identity.h"

namespace Neurotic::Contracts
{
// Identity.h is the only canonical definition site. This facade adds a publication record, not an identity registry.
struct IdentityRecord
{
    RecordHeader header = RecordHeader {ContractId::C14};
    MetadataRef<FrameIdentity> frame {};
    ResourceIdentityToken resource {};
    MetadataRef<GenerationVector> generations {};
    inline static constexpr std::string_view WireName = "IdentityRecord";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &IdentityRecord::header),
            Field("frame", &IdentityRecord::frame),
            Field("resource", &IdentityRecord::resource),
            Field("generations", &IdentityRecord::generations)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C14) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Session, OwnerDomain::IdentityRegistry, OwnerDomain::Topology, OwnerDomain::Provider, OwnerDomain::Resource, OwnerDomain::Context, OwnerDomain::ColorContinuity, OwnerDomain::RayReconstruction, OwnerDomain::OrchestratorPolicy, OwnerDomain::StreamCoordinator, OwnerDomain::Strategy, OwnerDomain::History, OwnerDomain::FrameGeneration, OwnerDomain::Finalizer, OwnerDomain::Presentation, OwnerDomain::Multipass) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const IdentityRecord&) const = default;
};

} // namespace Neurotic::Contracts
