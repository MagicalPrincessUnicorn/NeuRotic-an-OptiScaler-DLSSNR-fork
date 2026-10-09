#pragma once
#include "RepresentationLineage.h"
#include "NativeSampleQualification.h"

namespace Neurotic::Context
{
// Internal full-value keys. Never truncate equality to a digest or treat equality as permission.
// Meaning references name immutable qualified metadata, not a C01 resource publication.
// Republished equivalent metadata may conservatively miss; resource content is never in A/B.
struct SemanticRepresentationKey
{
    std::uint32_t ruleVersion=1;
    C::RecordReference meaning;
    C::SemanticKind kind=C::SemanticKind::Color;
    C::BoundaryDescription boundary;
    C::FrameIdentity scope; // Session/stream/view/episode only; ordinary frame progression is not structural.
    C::RasterDescription raster;
    C::ResourceDescriptor descriptor;
    TransformLineage applied;
    C::GenerationToken representation;
    bool operator==(const SemanticRepresentationKey& b) const
    {
        return ruleVersion==b.ruleVersion&&meaning==b.meaning&&kind==b.kind&&SemanticEqual(boundary,b.boundary)&&
          SemanticEqual(scope,b.scope)&&SemanticEqual(raster,b.raster)&&SemanticEqual(descriptor,b.descriptor)&&SemanticEqual(applied,b.applied)&&representation==b.representation;
    }
};
enum class RepresentationIntent {ConsumeSource,AllocateTarget};
struct RepresentationPlanKey
{
    SemanticRepresentationKey semantic;
    C::ProfileKey profile;
    C::Symbol purpose;
    C::RasterMapping mapping;
    C::ResourceDescriptor output;
    C::Symbol precision;
    C::BoundedList<C::ResourceCapability,4> capabilities;
    TransformLineage requested;
    C::GenerationToken resource;
    C::GenerationVector qualifiedGenerations;
    std::optional<C::RecordKey> qualificationPolicy;
    RepresentationIntent intent=RepresentationIntent::ConsumeSource;
    bool operator==(const RepresentationPlanKey& b) const
    {
        return semantic==b.semantic&&profile==b.profile&&purpose==b.purpose&&SemanticEqual(mapping,b.mapping)&&
          SemanticEqual(output,b.output)&&precision==b.precision&&capabilities==b.capabilities&&
          SemanticEqual(requested,b.requested)&&resource==b.resource&&
          qualifiedGenerations==b.qualifiedGenerations&&qualificationPolicy==b.qualificationPolicy&&intent==b.intent;
    }
};
struct AllocationRequirementKey
{
    C::SessionId session;
    C::ObjectIncarnation device;
    C::ResourceDescriptor output;
    C::BoundedList<C::ResourceCapability,4> capabilities;
    C::Symbol allocationClass;
    bool operator==(const AllocationRequirementKey& b) const
    {
        return session==b.session&&device==b.device&&SemanticEqual(output,b.output)&&
          capabilities==b.capabilities&&allocationClass==b.allocationClass;
    }
};
struct PreparedContentKey
{
    RepresentationPlanKey plan;
    C::ResourceView source,target;
    C::FrameIdentity frame;
    std::optional<C::FrameIdentity> previous;
    TransformLineage executed;
    bool operator==(const PreparedContentKey& b) const
    {
        return plan==b.plan&&SemanticEqual(source,b.source)&&SemanticEqual(target,b.target)&&
          SemanticEqual(frame,b.frame)&&SemanticEqual(previous,b.previous)&&SemanticEqual(executed,b.executed);
    }
};
inline bool CompleteDescriptor(const C::ResourceDescriptor& d)
{
    return ValidValues(d)&&Established(d.allocation)&&d.allocation.KnownPart()->value.width>0&&
      d.allocation.KnownPart()->value.height>0&&Established(d.format)&&!d.format.KnownPart()->value.Empty()&&
      Established(d.sampleCount)&&d.sampleCount.KnownPart()->value>0&&Established(d.mipLevels)&&d.mipLevels.KnownPart()->value>0&&
      Established(d.arrayLayers)&&d.arrayLayers.KnownPart()->value>0&&Established(d.api);
}
// Structural metadata can be qualified before current pixel contents are known.
// This does not establish a prepared-content key or any resource-use permission.
inline bool CompleteResourceStructure(const C::ResourceView& v)
{
    const bool complete=ValidValues(v)&&CompleteDescriptor(v.descriptor)&&ValidRaster(v.raster)&&Established(v.device)&&
      Established(v.adapter)&&!v.adapter.KnownPart()->value.Empty()&&Established(v.mip)&&Established(v.arrayLayer)&&Established(v.plane)&&
      Established(v.identity.objectIncarnation)&&Established(v.identity.resourceIncarnation)&&Established(v.identity.resourceViewIncarnation)&&
      Established(v.identity.resourceGeneration)&&Established(v.identity.representationGeneration);
    if(!complete)return false;
    const auto mip=v.mip.KnownPart()->value;const auto allocation=v.descriptor.allocation.KnownPart()->value;
    const C::Extent selected{mip>=32?1:(std::max)(1u,allocation.width>>mip),mip>=32?1:(std::max)(1u,allocation.height>>mip)};
    return v.raster.allocation.KnownPart()->value==selected;
}
inline bool CompleteContent(const C::ResourceView& v)
{return CompleteResourceStructure(v)&&Established(v.identity.contentRevision);}
template<class Reader> std::optional<SemanticRepresentationKey> BuildSemanticKey(const C::RecordReference& meaning,
    C::SemanticKind kind,const C::BoundaryDescription& boundary,const C::FrameIdentity& frame,const C::ResourceView& source,const TransformLineage& applied,const Reader& reader)
{
    if(!ValidValues(meaning)||meaning.record.Check()!=C::Error::None||meaning.revision==0||!ValidValues(kind)||!ValidValues(boundary)||
       !ValidValues(frame)||!SameObservationScope(frame,frame,reader,false)||!ValidRaster(source.raster)||!CompleteDescriptor(source.descriptor)||!ValidValues(source.identity)||
       !Established(source.identity.representationGeneration)||!ValidLineage(applied,true,reader))return {};
    C::FrameIdentity scope;scope.sessionId=frame.sessionId;scope.renderStreamId=frame.renderStreamId;scope.viewId=frame.viewId;scope.episodeId=frame.episodeId;
    return SemanticRepresentationKey{1,meaning,kind,boundary,scope,source.raster,source.descriptor,applied,
        source.identity.representationGeneration.KnownPart()->value};
}
template<class Reader> std::optional<RepresentationPlanKey> BuildRepresentationPlanKey(const SemanticRepresentationKey& semantic,
    const C::ResourceView& source,const C::ProfileKey& profile,const C::Symbol& purpose,const C::RasterMapping& mapping,
    const C::ResourceDescriptor& output,const C::Symbol& precision,const C::BoundedList<C::ResourceCapability,4>& caps,
    const TransformLineage& requested,const Reader& reader,const C::GenerationVector& qualifiedGenerations={},
    const std::optional<C::RecordKey>& qualificationPolicy={})
{
    if(!ValidValues(profile)||profile.consumer.Empty()||profile.profile.Empty()||profile.version==0||purpose.Empty()||precision.Empty()||
       !ValidValues(caps)||!ValidMapping(mapping)||!SemanticEqual(source.raster,mapping.source)||!CompleteDescriptor(output)||
       !ValidValues(source.identity)||!Established(source.identity.resourceGeneration)||!Established(source.identity.representationGeneration)||
       semantic.representation!=source.identity.representationGeneration.KnownPart()->value||
       !SemanticEqual(semantic.raster,source.raster)||!SemanticEqual(semantic.descriptor,source.descriptor)||
       !CompareLineage(semantic.applied,requested,reader).compatible||!ValidValues(qualifiedGenerations)||!ValidValues(qualificationPolicy))return {};
    // Qualification may bind an object lifetime, but B is a recipe key. Concrete
    // incarnations remain exclusively in the exact D content identity.
    C::GenerationVector structural;
    for(const auto& token:qualifiedGenerations.Entries())
    {
        const auto kind=token.identity.kind;
        if(kind==C::IdentityKind::ObjectIncarnation||kind==C::IdentityKind::ResourceIncarnation||
           kind==C::IdentityKind::ResourceViewIncarnation||kind==C::IdentityKind::ProviderIncarnation)continue;
        if(structural.Insert(token)!=C::Error::None)return {};
    }
    return RepresentationPlanKey{semantic,profile,purpose,mapping,output,precision,caps,requested,
        source.identity.resourceGeneration.KnownPart()->value,structural,qualificationPolicy};
}
inline std::optional<AllocationRequirementKey> BuildAllocationRequirementKey(const C::FrameIdentity& frame,
    const C::OptionalFact<C::ObjectIncarnation>& device,const C::ResourceDescriptor& output,
    const C::BoundedList<C::ResourceCapability,4>& caps,const C::Symbol& allocationClass)
{
    if(!ValidValues(frame)||!Established(frame.sessionId)||!ValidValues(device)||!Established(device)||
       !CompleteDescriptor(output)||!ValidValues(caps)||allocationClass.Empty())return {};
    return AllocationRequirementKey{frame.sessionId.KnownPart()->value,device.KnownPart()->value,output,caps,allocationClass};
}
template<class Reader> std::optional<PreparedContentKey> BuildPreparedContentKey(const RepresentationPlanKey& plan,
    const C::ResourceView& source,const C::ResourceView& target,const C::FrameIdentity& frame,
    const std::optional<C::FrameIdentity>& previous,bool pairRequired,const Reader& reader)
{
    if(plan.intent!=RepresentationIntent::ConsumeSource||!CompleteContent(source)||!CompleteContent(target)||
       !ValidValues(frame)||!SameObservationScope(frame,frame,reader)||
       !SemanticEqual(source.raster,plan.mapping.source)||!SemanticEqual(target.raster,plan.mapping.target)||
       !SemanticEqual(target.descriptor,plan.output)||!SemanticEqual(source.descriptor,plan.semantic.descriptor)||
       source.identity.resourceGeneration.KnownPart()->value!=plan.resource||
       source.identity.representationGeneration.KnownPart()->value!=plan.semantic.representation||
       !ValidLineage(plan.requested,false,reader))return {};
    if(pairRequired && (!previous||!ValidValues(*previous)||!SameScope(frame,*previous,false)||
       !Established(previous->baseRealFrameId)||!Established(frame.baseRealFrameId)||
       previous->baseRealFrameId.KnownPart()->value==frame.baseRealFrameId.KnownPart()->value||
       previous->baseRealFrameId.KnownPart()->value.nameSpace!=frame.baseRealFrameId.KnownPart()->value.nameSpace||
       previous->baseRealFrameId.KnownPart()->value.issuer!=frame.baseRealFrameId.KnownPart()->value.issuer))return {};
    // The content key describes the required executed lineage. Only a resource-owner
    // PreparedView plus its exact execution receipt can establish that it actually ran.
    return PreparedContentKey{plan,source,target,frame,pairRequired?previous:std::nullopt,plan.requested};
}
} // namespace Neurotic::Context
