#pragma once
#include "RepresentationCacheKeys.h"

namespace Neurotic::Context
{
// Callback-scoped exact owner publications, never retained pointers or a second C12 schema.
// Resource owner binds the execution receipt to the exact PreparedView revision. Context
// owner binds full-value cache entries to the canonical keys. Authentication stays external.
struct PreparedViewEvidence
{
    const C::PreparedView* prepared=nullptr;
    const C::RepresentationPlan* plan=nullptr;
    const PreparedContentKey* content=nullptr;
    C::ContractRef<C::ContractId::C12> receiptFor;
    C::RecordKey semanticPublication,planPublication;
};
enum class PreparedMismatch {None,Publication,SemanticPlan,SourceContent,TargetContent,Lineage};
struct PreparedViewCompatibilityResult
{
    bool compatible=false;
    PreparedMismatch mismatch=PreparedMismatch::Publication;
};
template<class Reader> PreparedViewCompatibilityResult EvaluatePreparedViewCompatibility(const PreparedViewEvidence& e,
    const RepresentationPlanKey& wanted,const C::ResourceView& currentSource,const C::FrameIdentity& frame,
    const std::optional<C::FrameIdentity>& previous,bool pairRequired,const Reader& reader)
{
    if(!e.prepared||!e.plan||!e.content||!ValidValues(*e.prepared)||!ValidValues(*e.plan))return {};
    const auto& v=*e.prepared;const auto& p=*e.plan;const auto& k=*e.content;
    if(e.receiptFor.recordType.View()!=C::PreparedView::WireName||e.receiptFor.record!=v.header.record||
       e.receiptFor.revision!=v.header.revision||v.plan.recordType.View()!=C::RepresentationPlan::WireName||
       v.plan.record!=p.header.record||v.plan.revision!=p.header.revision||
       v.keys.plan!=e.planPublication||p.keys.plan!=e.planPublication||
       v.keys.semantic!=e.semanticPublication||p.keys.semantic!=e.semanticPublication)return {};
    if(k.plan!=wanted||p.profile!=wanted.profile||p.purpose!=wanted.purpose||
       !SemanticEqual(p.output,wanted.output)||!Established(p.precision)||p.precision.KnownPart()->value!=wanted.precision||
       p.requiredCapabilities!=wanted.capabilities)return {false,PreparedMismatch::SemanticPlan};
    const auto* mapping=ResolveMetadata(p.raster,reader);
    if(!mapping||!SemanticEqual(*mapping,wanted.mapping))return {false,PreparedMismatch::SemanticPlan};
    if(!SemanticEqual(p.source,k.source.identity)||!SemanticEqual(p.keys.content,k.source.identity)||
       !SemanticEqual(v.keys.content,k.source.identity)||!Established(v.keys.generation)||!Established(p.keys.generation)||
       v.keys.generation.KnownPart()->value.Describe()!=wanted.semantic.representation.identity||
       p.keys.generation.KnownPart()->value.Describe()!=wanted.semantic.representation.identity)return {false,PreparedMismatch::SourceContent};
    const auto* target=ResolveMetadata(v.view,reader);
    if(!target||!SemanticEqual(*target,k.target))return {false,PreparedMismatch::TargetContent};
    const auto expected=BuildPreparedContentKey(wanted,currentSource,*target,frame,previous,pairRequired,reader);
    if(!expected||*expected!=k)return {false,PreparedMismatch::SourceContent};
    const TransformLineage* applied=nullptr;const TransformLineage* planned=nullptr;
    if(!ResolveList(v.appliedTransforms,reader,applied)||!ResolveList(p.transforms,reader,planned))return {false,PreparedMismatch::Lineage};
    const TransformLineage empty;
    const auto delta=CompareLineage(applied?*applied:empty,wanted.requested,reader);
    const auto sourceDelta=CompareLineage(wanted.semantic.applied,wanted.requested,reader);
    if(!delta.compatible||delta.delta.Size()!=0||!sourceDelta.compatible||
       !SemanticEqual(planned?*planned:empty,sourceDelta.delta))return {false,PreparedMismatch::Lineage};
    return {true,PreparedMismatch::None};
}
} // namespace Neurotic::Context
