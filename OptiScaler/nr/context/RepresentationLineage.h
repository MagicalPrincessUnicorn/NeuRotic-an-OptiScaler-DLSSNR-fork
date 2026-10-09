#pragma once
// Pure mechanical lineage comparison. No resource rights, pixels, GPU work or policy inference.
#include "ContextMetadata.h"
#include "../contracts/C12_Representation.h"

namespace Neurotic::Context
{
using TransformLineage=C::BoundedList<C::TransformStep,16>;
struct LineageComparison
{
    bool compatible=false;
    TransformLineage delta;
};
template<class Reader> bool ValidTransform(const C::TransformStep& step,const Reader& reader)
{
    if(!ValidValues(step)||step.operation.Empty()||step.version==0||!Established(step.sourceUnits)||
       !Established(step.targetUnits)||!Established(step.qualification)||!Established(step.alreadyApplied)||
       step.sourceUnits.KnownPart()->value.Empty()||step.targetUnits.KnownPart()->value.Empty())return false;
    const C::BoundedList<C::SemanticClaim,8>* params=nullptr;
    if(!ResolveList(step.parameters,reader,params))return false;
    if(params)for(std::size_t i=0;i<params->Size();++i)
    {
        const auto& p=*params->Get(i);
        if(p.field.Empty()||!Established(p.effective))return false;
        for(std::size_t j=0;j<i;++j)if(params->Get(j)->field==p.field)return false;
    }
    return true;
}
template<class Reader> bool SameTransform(const C::TransformStep& a,const C::TransformStep& b,const Reader& reader)
{
    if(!ValidTransform(a,reader)||!ValidTransform(b,reader)||a.kind!=b.kind||a.operation!=b.operation||
       a.version!=b.version||!SemanticEqual(a.sourceUnits,b.sourceUnits)||!SemanticEqual(a.targetUnits,b.targetUnits)||
       !SemanticEqual(a.qualification,b.qualification))return false;
    const C::BoundedList<C::SemanticClaim,8>* ap=nullptr;const C::BoundedList<C::SemanticClaim,8>* bp=nullptr;
    if(!ResolveList(a.parameters,reader,ap)||!ResolveList(b.parameters,reader,bp))return false;
    if(!ap||!bp)return ap==bp;
    return SemanticEqual(*ap,*bp);
}
template<class Reader> bool ValidLineage(const TransformLineage& steps,bool applied,const Reader& reader)
{
    for(std::size_t i=0;i<steps.Size();++i)
    {
        const auto& step=*steps.Get(i);
        if(!ValidTransform(step,reader)||step.alreadyApplied.KnownPart()->value!=applied)return false;
        for(std::size_t j=0;j<i;++j)
        {
            const auto& previous=*steps.Get(j);
            if(previous.kind==step.kind && (step.kind==C::TransformKind::MotionScale||
               step.kind==C::TransformKind::JitterRemoval||step.kind==C::TransformKind::ExposureUndo||
               step.kind==C::TransformKind::ExposureApply||previous.operation==step.operation))return false;
        }
    }
    return true;
}
template<class Reader> LineageComparison CompareLineage(const TransformLineage& applied,
                                                        const TransformLineage& requested,const Reader& reader)
{
    LineageComparison result;
    if(!ValidLineage(applied,true,reader)||!ValidLineage(requested,false,reader)||applied.Size()>requested.Size())return result;
    for(std::size_t i=0;i<applied.Size();++i)if(!SameTransform(*applied.Get(i),*requested.Get(i),reader))return result;
    for(std::size_t i=applied.Size();i<requested.Size();++i)if(!result.delta.Push(*requested.Get(i)))return {};
    result.compatible=true;return result;
}
// Explicit versioned recipe declaration, not an inferred rewrite by operation name.
struct FusionRule
{
    C::Symbol operation;
    std::uint32_t version=0;
    TransformLineage steps;
    C::RasterMapping mapping;
    C::Symbol precision;
    C::ResourceDescriptor output;
    C::BoundedList<C::ResourceCapability,4> capabilities;
};
struct FusedOperation
{
    C::Symbol operation;
    std::uint32_t version=0;
    TransformLineage constituents;
};
inline bool ValidMapping(const C::RasterMapping& mapping)
{
    return ValidValues(mapping)&&ValidRaster(mapping.source)&&ValidRaster(mapping.target)&&
           Established(mapping.scale)&&Established(mapping.offset)&&Established(mapping.mappingEvidence)&&
           mapping.scale.KnownPart()->value.x!=0&&mapping.scale.KnownPart()->value.y!=0;
}
template<class Reader> std::optional<FusedOperation> MatchFusion(const FusionRule& rule,const TransformLineage& delta,
                        const C::RasterMapping& mapping,const C::Symbol& precision,const Reader& reader)
{
    if(rule.operation.Empty()||rule.version==0||rule.steps.Size()<2||rule.steps.Size()!=delta.Size()||
       rule.precision.Empty()||rule.precision!=precision||!ValidMapping(mapping)||!ValidMapping(rule.mapping)||
       !SemanticEqual(rule.mapping,mapping)||!ValidLineage(rule.steps,false,reader)||!ValidLineage(delta,false,reader))return {};
    for(std::size_t i=0;i<delta.Size();++i)if(!SameTransform(*rule.steps.Get(i),*delta.Get(i),reader))return {};
    return FusedOperation{rule.operation,rule.version,delta};
}
} // namespace Neurotic::Context
