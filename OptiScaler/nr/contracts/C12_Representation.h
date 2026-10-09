#pragma once
// NR-ARCH-002 candidate; not applied, compiled or executed.
// In-process values only. No public DLL ABI or live authority is established here.
#include "SemanticDescriptions.h"

namespace Neurotic::Contracts
{
// Separate semantic/plan/allocation/content axes; no cache lookup, reuse decision or generation advancement.
struct RepresentationCacheKeys
{
    RecordKey semantic {};
    RecordKey plan {};
    OptionalFact<RecordKey> allocation {};
    ResourceIdentityToken content {};
    OptionalFact<RepresentationGeneration> generation {};
    inline static constexpr std::string_view WireName = "RepresentationCacheKeys";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("semantic", &RepresentationCacheKeys::semantic),
            Field("plan", &RepresentationCacheKeys::plan),
            Field("allocation", &RepresentationCacheKeys::allocation),
            Field("content", &RepresentationCacheKeys::content),
            Field("generation", &RepresentationCacheKeys::generation)
        };
    }
    bool operator==(const RepresentationCacheKeys&) const = default;
};

struct RepresentationPlan
{
    RecordHeader header = RecordHeader {ContractId::C12};
    ResourceIdentityToken source {};
    ProfileKey profile {};
    Symbol purpose {};
    MetadataRef<RasterMapping> raster {};
    MetadataList<TransformStep, 16> transforms {};
    OptionalFact<Symbol> precision {};
    BoundedList<ResourceCapability, 4> requiredCapabilities {};
    ResourceDescriptor output {};
    RepresentationCacheKeys keys {};
    AccessRequirements accessRequirements {};
    OptionalFact<Symbol> retirementContract {};
    inline static constexpr std::string_view WireName = "RepresentationPlan";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &RepresentationPlan::header),
            Field("source", &RepresentationPlan::source),
            Field("profile", &RepresentationPlan::profile),
            Field("purpose", &RepresentationPlan::purpose),
            Field("raster", &RepresentationPlan::raster),
            Field("transforms", &RepresentationPlan::transforms),
            Field("precision", &RepresentationPlan::precision),
            Field("requiredCapabilities", &RepresentationPlan::requiredCapabilities),
            Field("output", &RepresentationPlan::output),
            Field("keys", &RepresentationPlan::keys),
            Field("accessRequirements", &RepresentationPlan::accessRequirements),
            Field("retirementContract", &RepresentationPlan::retirementContract)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C12) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Context) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const RepresentationPlan&) const = default;
};

// Descriptive compatibility does not carry C03 rights. No implicit conversion to ConsumptionLease exists.
struct PreparedView
{
    RecordHeader header = RecordHeader {ContractId::C12};
    ContractRef<ContractId::C12> plan {};
    MetadataRef<ResourceView> view {};
    RepresentationCacheKeys keys {};
    MetadataList<TransformStep, 16> appliedTransforms {};
    AccessRequirements requiredAccess {};
    OptionalFact<Symbol> retirementContract {};
    inline static constexpr std::string_view WireName = "PreparedView";
    static constexpr auto Fields()
    {
        return std::tuple {
            Field("header", &PreparedView::header),
            Field("plan", &PreparedView::plan),
            Field("view", &PreparedView::view),
            Field("keys", &PreparedView::keys),
            Field("appliedTransforms", &PreparedView::appliedTransforms),
            Field("requiredAccess", &PreparedView::requiredAccess),
            Field("retirementContract", &PreparedView::retirementContract)
        };
    }
    Error Check() const
    {
        if (CheckHeader(header, ContractId::C12) != Error::None)
            return Error::WrongRecordType;
        if (CheckOwner(header.owner, OwnerDomain::Resource) != Error::None)
            return Error::WrongOwner;
        return Error::None;
    }
    bool operator==(const PreparedView&) const = default;
};

} // namespace Neurotic::Contracts
