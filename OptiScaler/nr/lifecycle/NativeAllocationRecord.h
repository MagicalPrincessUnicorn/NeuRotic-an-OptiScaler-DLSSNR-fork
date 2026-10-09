#pragma once
#include "IdentityIssuers.h"
#include <nr/context/RepresentationCacheKeys.h>
#include <nr/contracts/C12_Representation.h>
#include <optional>

struct ID3D12Resource;
namespace Neurotic::Lifecycle
{
struct NativePreparationLineage
{
    C::MetadataList<C::TransformStep,16> sourceApplied,required;
    Context::TransformLineage executedDelta;
};
// One existing Resource-owner allocation, from registration until retirement. The
// allocation owner retains this alongside its native storage/tickets and holds
// its lock across these calls. This record never allocates or releases a GPU
// object, grants a lease, or turns pointer equality into incarnation identity.
// Slot facades are bound once at startup. Replacing a slot requires a new record;
// the old record remains retained with the old allocation and becomes read-only.
class NativeAllocationRecord
{
    friend class NativeResourceRegistry;
#ifdef NR_SPECTRE_SOURCE_TESTING
    friend class NativeAllocationRecordTestAccess;
#endif
    ResourceIssuer resource_;
    RepresentationIssuer representation_;
    OwnerBinding owner_;
    ID3D12Resource* native_=nullptr; // owner-local registration; never serialized
    std::optional<C::ResourceView> view_;
    std::optional<C::OptionalFact<C::ContentRevision>> opaqueReserved_;
    bool revoked_=false,registrationAttempted_=false;
    struct Association
    {
        C::ContractRef<C::ContractId::C12> plan;
        C::ProfileKey profile;
        C::RepresentationCacheKeys keys;
        C::ContractRef<C::ContractId::C02> context;
        C::EvaluationId evaluation;
        C::ResourceIdentityToken allocation;
    };
    std::optional<Association> association_;
    template<class T>static C::OptionalFact<C::StableIdentity> IdentityFact(const Outcome<T>& value)
    {const auto& known=*value.value.KnownPart();return C::OptionalFact<C::StableIdentity>::FromKnown(known.value.Describe(),known.evidence);}
    template<class T>static C::OptionalFact<C::GenerationToken> GenerationFact(const Outcome<T>& value)
    {const auto& known=*value.value.KnownPart();return C::OptionalFact<C::GenerationToken>::FromKnown({known.value.Describe()},known.evidence);}
    // Only the retaining registry establishes projections of this physical
    // lifetime. Its reserved cache entry survives any partial identity issue.
    std::optional<C::ResourceView> ProjectRectangle(const C::Rectangle& rect,const OwnerEvent& event)
    {
        if(revoked_||!view_||CheckOwnerEvent(owner_,event)!=IdentityStatus::Ok||!rect.width||!rect.height)return {};
        const auto size=view_->descriptor.allocation.KnownPart()->value;
        if(rect.x>size.width||rect.y>size.height||rect.width>size.width-rect.x||rect.height>size.height-rect.y)return {};
        const auto view=resource_.View(event);const auto representation=representation_.Structure(event);
        if(view.status!=IdentityStatus::Ok||representation.status!=IdentityStatus::Ok)return {};
        auto result=*view_;result.identity.resourceViewIncarnation=IdentityFact(view);
        result.identity.representationGeneration=GenerationFact(representation);
        result.raster.active=C::OptionalFact<C::Rectangle>::FromKnown(rect,event.evidence);return result;
    }
  public:
    NativeAllocationRecord(const ResourceIssuer& resource,const RepresentationIssuer& representation,const OwnerBinding& owner):
        resource_(resource),representation_(representation),owner_(owner){}
    NativeAllocationRecord(const NativeAllocationRecord&)=delete;
    NativeAllocationRecord& operator=(const NativeAllocationRecord&)=delete;
    bool Registered(ID3D12Resource* native,const C::ResourceView& physical,const OwnerEvent& event)
    {
        if(registrationAttempted_||revoked_||!native||CheckOwnerEvent(owner_,event)!=IdentityStatus::Ok||
           !Context::ValidValues(physical)||!Context::CompleteDescriptor(physical.descriptor)||
           !Context::ValidRaster(physical.raster)||!Context::Established(physical.device)||
           !Context::Established(physical.adapter)||physical.adapter.KnownPart()->value.Empty()||
           !Context::Established(physical.mip)||!Context::Established(physical.arrayLayer)||!Context::Established(physical.plane))return false;
        registrationAttempted_=true; // partial issue cannot be retried as the same registration
        const auto object=resource_.Object(event);const auto resource=resource_.Resource(event);
        const auto view=resource_.View(event);const auto structure=resource_.Structure(event);
        const auto representation=representation_.Structure(event);
        if(object.status!=IdentityStatus::Ok||resource.status!=IdentityStatus::Ok||view.status!=IdentityStatus::Ok||
           structure.status!=IdentityStatus::Ok||representation.status!=IdentityStatus::Ok)return false;
        auto value=physical;value.identity={};value.identity.objectIncarnation=IdentityFact(object);
        value.identity.resourceIncarnation=IdentityFact(resource);value.identity.resourceViewIncarnation=IdentityFact(view);
        value.identity.resourceGeneration=GenerationFact(structure);value.identity.representationGeneration=GenerationFact(representation);
        native_=native;view_=value;return true;
    }
    // Call only at an observed allocation creation. Registration can also bind
    // a first-observed retained lifetime without claiming its unseen creation.
    bool Created(ID3D12Resource* native,const C::ResourceView& physical,const OwnerEvent& event)
    {return Registered(native,physical,event);}
    bool Written(ID3D12Resource* native,const OwnerEvent& event)
    {
        if(revoked_||!view_||opaqueReserved_||native!=native_||CheckOwnerEvent(owner_,event)!=IdentityStatus::Ok)return false;
        const auto& identity=view_->identity.resourceIncarnation.KnownPart()->value;
        const auto revision=resource_.ContentWrite(event,{identity.nameSpace,identity.issuer,identity.value});
        if(revision.status!=IdentityStatus::Ok)return false;
        view_->identity.contentRevision=revision.value;association_.reset();return true;
    }
    // Issue the Resource owner's next content revision before an opaque call,
    // but keep the visible ResourceView at its old version until the exact
    // operation receipt authenticates recorded work and the successful return.
    std::optional<C::OptionalFact<C::ContentRevision>> ReserveOpaqueWrite(ID3D12Resource* native,
        const OwnerEvent& event)
    {
        if(revoked_||!view_||opaqueReserved_||native!=native_||
           CheckOwnerEvent(owner_,event)!=IdentityStatus::Ok)return {};
        const auto& identity=view_->identity.resourceIncarnation.KnownPart()->value;
        const auto revision=resource_.ContentWrite(event,{identity.nameSpace,identity.issuer,identity.value});
        if(revision.status!=IdentityStatus::Ok||!revision.value.IsKnown())return {};
        opaqueReserved_=revision.value;
        return opaqueReserved_;
    }
    bool CommitOpaqueWrite(ID3D12Resource* native,const C::ResourceIdentityToken& before,
        const C::OptionalFact<C::ContentRevision>& reserved)
    {
        if(revoked_||!view_||native!=native_||!opaqueReserved_||
           *opaqueReserved_!=reserved||view_->identity!=before)return false;
        view_->identity.contentRevision=reserved;
        association_.reset();opaqueReserved_.reset();return true;
    }
    bool InvalidateOpaqueWrite(ID3D12Resource* native,const C::ResourceIdentityToken& before,
        const C::OptionalFact<C::ContentRevision>& reserved)noexcept
    {
        if(revoked_||!view_||native!=native_||!opaqueReserved_||
           *opaqueReserved_!=reserved||view_->identity!=before)return false;
        // Possible partial writes invalidate the old contents. Keep the exact
        // unresolved reservation; a later attempt cannot launder that failure.
        view_->identity.contentRevision={};association_.reset();return true;
    }
    const C::ResourceView* View()const noexcept{return view_?&*view_:nullptr;}
    bool IsCurrent(ID3D12Resource* native)const noexcept{return !revoked_&&view_.has_value()&&native==native_;}
    void Revoke()noexcept{revoked_=true;association_.reset();}
    template<class Store>std::optional<C::PreparedView> PublishPrepared(const C::RepresentationPlan& plan,
        const C::ResourceView& source,const C::ContractRef<C::ContractId::C02>& context,const C::EvaluationId& evaluation,
        const NativePreparationLineage& lineage,const C::RecordHeader& header,Store& store)
    {
        // ModelTarget describes a future writable destination. Actual target
        // realization cannot masquerade as this source-conversion publication.
        if(plan.purpose.View()=="OutputModelTarget"||revoked_||!view_||!Context::CompleteContent(*view_)||!Context::CompleteContent(source)||
           !Context::ValidValues(plan)||plan.Check()!=C::Error::None||!Context::ValidValues(header)||
           header.contract!=C::ContractId::C12||header.owner!=C::OwnerDomain::Resource||header.scope!=plan.header.scope||
           context.recordType.View()!=C::CanonicalFrameContext::WireName||context.record.Check()!=C::Error::None||context.revision==0||
           evaluation.Check()!=C::Error::None||!Context::SemanticEqual(source.identity,plan.source)||
           !Context::SemanticEqual(plan.keys.content,source.identity)||!Context::SemanticEqual(view_->descriptor,plan.output)||
           !Context::SameFact(source.device,view_->device)||!Context::ValidLineage(lineage.executedDelta,true,store))return {};
        const auto* mapping=Context::ResolveMetadata(plan.raster,store);
        const Context::TransformLineage *requested=nullptr,*sourceApplied=nullptr,*required=nullptr;
        if(!mapping||!Context::SemanticEqual(mapping->source,source.raster)||!Context::SemanticEqual(mapping->target,view_->raster)||
           !Context::ResolveList(plan.transforms,store,requested)||
           !Context::ResolveList(lineage.sourceApplied,store,sourceApplied)||
           !Context::ResolveList(lineage.required,store,required)||
           (lineage.sourceApplied.backing&&lineage.sourceApplied.backing->owner!=C::OwnerDomain::Context)||
           (lineage.required.backing&&lineage.required.backing->owner!=C::OwnerDomain::Context))return {};
        const Context::TransformLineage empty;
        const auto delta=Context::CompareLineage(sourceApplied?*sourceApplied:empty,required?*required:empty,store);
        if(!delta.compatible||!Context::SemanticEqual(delta.delta,requested?*requested:empty))return {};
        const auto comparison=Context::CompareLineage(lineage.executedDelta,delta.delta,store);
        if(!comparison.compatible||comparison.delta.Size()!=0)return {};
        auto applied=sourceApplied?*sourceApplied:empty;
        for(const auto& step:lineage.executedDelta)if(!applied.Push(step))return {};
        if(!Context::ValidLineage(applied,true,store))return {};
        C::PreparedView result;result.header=header;
        result.plan={SymbolName<C::RepresentationPlan>(),plan.header.record,plan.header.revision};
        result.keys=plan.keys;result.requiredAccess=plan.accessRequirements;result.retirementContract=plan.retirementContract;
        result.view=store.Publish(*view_,C::OwnerDomain::Resource);
        result.appliedTransforms.count=static_cast<std::uint32_t>(applied.Size());
        if(applied.Size())result.appliedTransforms.backing=store.Publish(applied,C::OwnerDomain::Resource);
        const auto* published=Context::ResolveMetadata(result.view,store);
        const Context::TransformLineage* publishedLineage=nullptr;
        if(!Context::ValidValues(result)||result.Check()!=C::Error::None||
           result.view.owner!=C::OwnerDomain::Resource||!published||!Context::SemanticEqual(*published,*view_)||
           (result.appliedTransforms.backing&&result.appliedTransforms.backing->owner!=C::OwnerDomain::Resource)||
           !Context::ResolveList(result.appliedTransforms,store,publishedLineage)||
           !Context::SemanticEqual(publishedLineage?*publishedLineage:empty,applied))return {};
        association_=Association{result.plan,plan.profile,plan.keys,context,evaluation,view_->identity};return result;
    }
    bool Matches(const C::RepresentationPlan& plan,const C::ContractRef<C::ContractId::C02>& context,
        const C::EvaluationId& evaluation,const C::ResourceIdentityToken& allocation)const
    {
        return !revoked_&&view_&&association_&&association_->plan.record==plan.header.record&&
            association_->plan.revision==plan.header.revision&&association_->profile==plan.profile&&
            association_->keys==plan.keys&&association_->context==context&&association_->evaluation==evaluation&&
            Context::SemanticEqual(association_->allocation,allocation)&&Context::SemanticEqual(view_->identity,allocation);
    }
  private:
    template<class T>static C::Symbol SymbolName(){C::Symbol value;value.Assign(T::WireName);return value;}
};
}
