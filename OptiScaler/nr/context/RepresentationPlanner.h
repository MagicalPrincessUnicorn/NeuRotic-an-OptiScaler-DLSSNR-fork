#pragma once
#include "CapabilityQualification.h"
#include "PreparedViewValidation.h"

namespace Neurotic::Context
{
enum class PreparationPath {NoWork,Unavailable,DirectUse,MetadataOnly,ConsumerTransform,ExistingPreparedView,NewConversion,StructuralOnly,TargetAllocation};
struct RepresentationRequirement
{
    bool requested=false,required=false,pairRequired=false;
    C::SemanticKind kind=C::SemanticKind::Color;
    C::Symbol field;
    // Consumer binding purpose is independent of the C04 qualification purpose.
    // Absence preserves older callers; an explicit empty purpose is invalid.
    std::optional<C::Symbol> consumerPurpose;
    RepresentationIntent intent=RepresentationIntent::ConsumeSource;
    std::optional<C::AccessRequirements> targetAccess;
    C::ContractRef<C::ContractId::C01> selected;
    C::RasterMapping mapping;
    C::ResourceDescriptor output;
    C::Symbol precision,allocationClass;
    C::BoundedList<C::ResourceCapability,4> capabilities;
    TransformLineage transforms;
    // Explicit consumer recipe declarations; unknown is not permission to reinterpret pixels.
    C::OptionalFact<bool> metadataOnly;
    std::optional<FusionRule> consumerTransform;
    std::optional<C::FrameIdentity> previous;
};
struct SourceRepresentationFacts
{
    C::ResourceIdentityToken source;
    C::MetadataList<C::TransformStep,16> applied;
    C::OptionalFact<C::Symbol> precision;
    C::OptionalFact<C::BoundedList<C::ResourceCapability,4>> capabilities;
};
struct RepresentationEvidenceSnapshot
{
    C::ContractRef<C::ContractId::C02> context;
    C::ContractRef<C::ContractId::C04> qualification;
    PreparationPath path=PreparationPath::Unavailable;
    std::uint32_t planKeys=0,allocationKeys=0,preparedLookups=0,conversions=0;
    PreparedMismatch lastMismatch=PreparedMismatch::None;
    bool optionalNoWork=false;
    C::UnknownFact refusal;
};
struct PreparationDecision
{
    PreparationPath path=PreparationPath::Unavailable;
    // Present only for explicit structural planning. It does not declare current
    // contents usable, a prepared view reusable, or a conversion executed.
    std::optional<PreparationPath> plannedPath;
    std::optional<RepresentationPlanKey> key;
    std::optional<AllocationRequirementKey> allocation;
    TransformLineage delta;
    std::optional<C::MetadataRef<C::ResourceView>> source;
    std::optional<C::ContractRef<C::ContractId::C12>> prepared;
    C::ResourceIdentityToken sourceIdentity;
    C::AccessRequirements access;
    RepresentationEvidenceSnapshot evidence;
};
inline bool HasCapabilities(const C::BoundedList<C::ResourceCapability,4>& actual,
                            const C::BoundedList<C::ResourceCapability,4>& required)
{
    for(const auto cap:required){bool found=false;for(const auto have:actual)if(have==cap)found=true;if(!found)return false;}return true;
}
template<class T,class Reader> std::optional<C::RecordReference> BoundMeaning(
    const C::OptionalFact<C::MetadataRef<T>>& qualified,const C::OptionalFact<C::MetadataRef<T>>& source,const Reader& reader)
{
    const auto* q=ResolveOptional(qualified,reader);const auto* s=ResolveOptional(source,reader);
    if(!q||!s)return {};
    // The selected source must agree with every established member of the C02 description.
    // This is binding, not re-qualification or conversion of raw observations.
    const auto same=[]<class V>(const V& a,const V& b){
        if constexpr(requires{a.IsKnown();})return !a.IsKnown()||SemanticEqual(a,b);
        else return SemanticEqual(a,b);
    };
    const bool bound=std::apply([&](const auto&... f){return (same(q->*(f.pointer),s->*(f.pointer))&&...);},T::Fields());
    if(!bound)return {};
    const auto& ref=qualified.KnownPart()->value;C::Symbol type;type.Assign(T::WireName);
    return C::RecordReference{C::ContractId::C02,type,ref.record,ref.revision};
}
template<class Reader> std::optional<C::RecordReference> SourceMeaning(const C::CanonicalFrameContext& context,
    const C::AcquisitionCandidate& source,const Reader& reader)
{
    switch(source.semantic)
    {
    case C::SemanticKind::Color:return BoundMeaning(context.color,source.color,reader);
    case C::SemanticKind::Depth:return BoundMeaning(context.depth,source.depth,reader);
    case C::SemanticKind::Motion:return BoundMeaning(context.motion,source.motion,reader);
    case C::SemanticKind::Exposure:return BoundMeaning(context.exposure,source.exposure,reader);
    default:return {}; // No invented guide interpretation for a kind not represented by this C02 bundle.
    }
}
inline bool FieldForKind(std::string_view field,C::SemanticKind kind)
{
    switch(kind)
    {
    case C::SemanticKind::Color:return field.starts_with("color.");
    case C::SemanticKind::Depth:return field.starts_with("depth.");
    case C::SemanticKind::Motion:return field=="motion.canonical"||field=="motion.native";
    case C::SemanticKind::Exposure:return field.starts_with("exposure.");
    default:return false;
    }
}
// Reader.ResolveCandidate dereferences only the exact C01 reference already admitted by
// C02. It cannot discover providers, pick alternatives, or turn raw claims into meaning.
template<class Reader> PreparationDecision DeriveRepresentationPlan(const C::CanonicalFrameContext& context,
    const C::QualificationCertificate& certificate,const SemanticProfile& profile,const RepresentationRequirement& req,
    const SourceRepresentationFacts& facts,std::span<const PreparedViewEvidence> prepared,const Reader& reader,
    bool structuralOnly=false)
{
    PreparationDecision out;
    auto finish=[&](PreparationPath path){
        if(structuralOnly&&path!=PreparationPath::Unavailable&&path!=PreparationPath::NoWork)
        {out.plannedPath=path;path=PreparationPath::StructuralOnly;}
        out.path=path;out.evidence.path=path;if(path!=PreparationPath::Unavailable)out.evidence.refusal={};return out;};
    if(!req.requested){out.evidence.optionalNoWork=true;return finish(PreparationPath::NoWork);}
    out.evidence.refusal=Missing("CTX.RepresentationUnavailable");
    C::Symbol contextType,certificateType;contextType.Assign(C::CanonicalFrameContext::WireName);certificateType.Assign(C::QualificationCertificate::WireName);
    out.evidence.context={contextType,context.header.record,context.header.revision};
    out.evidence.qualification={certificateType,certificate.header.record,certificate.header.revision};
    if((req.consumerPurpose&&req.consumerPurpose->Empty())||
       (req.intent!=RepresentationIntent::ConsumeSource&&req.intent!=RepresentationIntent::AllocateTarget)||
       (req.intent==RepresentationIntent::ConsumeSource&&req.targetAccess)||
       !CertificateMatches(certificate,context,profile,reader)||certificate.eligibility!=C::Eligibility::Eligible||
       !FieldForKind(req.field.View(),req.kind))return out;
    bool declared=false;for(const auto& field:profile.requirements)if(field.field==req.field&&(!req.required||field.required))declared=true;
    bool supported=false;if(!declared||!FieldEstablished(req.field.View(),context,reader,supported)||!supported)return out;
    const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* coherent=nullptr;
    const C::BoundedList<C::GuideDescription,8>* guides=nullptr;
    if(!ResolveList(context.coherentCandidates,reader,coherent)||!coherent||!ResolveList(context.guides,reader,guides)||!guides)return out;
    bool member=false;for(const auto& ref:*coherent)if(ref==req.selected)member=true;
    const C::GuideDescription* guide=nullptr;for(const auto& item:*guides)
        if(Established(item.candidate)&&item.candidate.KnownPart()->value==req.selected&&item.kind==req.kind)guide=&item;
    if(!member||!guide)return out;
    const auto* candidate=reader.ResolveCandidate(req.selected);
    if(!candidate||!ValidValues(*candidate)||req.selected.recordType.View()!=C::AcquisitionCandidate::WireName||
       candidate->header.record!=req.selected.record||candidate->header.revision!=req.selected.revision||candidate->semantic!=req.kind||
       !Established(candidate->payload))return out;
    const auto* resourceRef=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate->payload.KnownPart()->value);
    const auto* source=resourceRef?ResolveMetadata(*resourceRef,reader):nullptr;
    const auto* frame=ResolveMetadata(context.frame,reader);const auto* sourceFrame=ResolveMetadata(candidate->frame,reader);
    const auto meaning=SourceMeaning(context,*candidate,reader);
    if(!source||!frame||!sourceFrame||!SameObservationScope(*frame,*sourceFrame,reader)||!meaning||!CompleteResourceStructure(*source)||
       (!structuralOnly&&!CompleteContent(*source))||
       !SemanticEqual(guide->raster,source->raster)||!SemanticEqual(facts.source,source->identity)||
       !ValidValues(facts.precision)||!Established(facts.precision)||!ValidValues(facts.capabilities)||!Established(facts.capabilities)||
       !ValidMapping(req.mapping)||!SemanticEqual(req.mapping.source,source->raster)||!CompleteDescriptor(req.output)||req.precision.Empty())return out;
    if(frame->nativeSample)
    {
        const auto* evidence=ResolveMetadata(candidate->evidence,reader);
        if(!evidence||!NativeEvidenceFresh(*evidence,*frame,reader)||
            !Established(candidate->descriptiveLifetime.callbackScope)||
            candidate->descriptiveLifetime.callbackScope.KnownPart()->value!=evidence->nativeFreshness->callback)return out;
    }
    const TransformLineage* applied=nullptr;if(!ResolveList(facts.applied,reader,applied))return out;
    const TransformLineage empty;const auto comparison=CompareLineage(applied?*applied:empty,req.transforms,reader);
    if(!comparison.compatible)return out;
    out.source=*resourceRef;out.sourceIdentity=source->identity;out.access=candidate->descriptiveLifetime;out.delta=comparison.delta;
    C::GenerationVector dependencies;
    if(!SelectGenerations(context,profile,reader,dependencies))return out;
    const auto semantic=BuildSemanticKey(*meaning,req.kind,context.boundary,*frame,*source,applied?*applied:empty,reader);
    if(!semantic)return out;
    out.key=BuildRepresentationPlanKey(*semantic,*source,profile.key,req.consumerPurpose.value_or(profile.purpose),req.mapping,req.output,req.precision,req.capabilities,req.transforms,reader,dependencies,profile.publication);
    if(!out.key)return out;++out.evidence.planKeys;out.key->intent=req.intent;
    if(req.intent==RepresentationIntent::AllocateTarget)
    {
        // Bounded descriptive ModelTarget operation, not a source conversion.
        // Its expected capabilities say nothing about the actual source object.
        if(!structuralOnly||!req.consumerPurpose||req.consumerPurpose->View()!="OutputModelTarget"||
           !req.targetAccess||!ValidValues(*req.targetAccess)||req.pairRequired||req.previous||
           req.transforms.Size()!=0||(applied&&applied->Size()!=0)||req.consumerTransform||req.metadataOnly.IsKnown())return out;
        const auto& access=*req.targetAccess;
        bool storage=false;for(const auto cap:req.capabilities)if(cap==C::ResourceCapability::Storage)storage=true;
        if(!storage||access.uses.Size()!=1||*access.uses.Get(0)!=C::UsageKind::WriteExclusive||
           !Established(access.device)||!SameFact(access.device,source->device)||
           !Established(access.callbackScope)||!SameFact(access.callbackScope,candidate->descriptiveLifetime.callbackScope)||
           !SemanticEqual(access.queue,candidate->descriptiveLifetime.queue)||
           !Established(access.requiresExactContent)||access.requiresExactContent.KnownPart()->value||
           !Established(access.retirementContract)||access.retirementContract.KnownPart()->value.Empty())return out;
        out.allocation=BuildAllocationRequirementKey(*frame,source->device,req.output,req.capabilities,req.allocationClass);
        if(!out.allocation)return out;
        out.access=access;++out.evidence.allocationKeys;
        return finish(PreparationPath::TargetAllocation);
    }
    const bool descriptorMatches=SemanticEqual(source->descriptor,req.output);
    const bool rasterMatches=SemanticEqual(source->raster,req.mapping.target)&&
        req.mapping.scale.KnownPart()->value==C::Vec2{1,1}&&req.mapping.offset.KnownPart()->value==C::Vec2{0,0};
    const bool sourceFits=descriptorMatches&&facts.precision.KnownPart()->value==req.precision&&
        HasCapabilities(facts.capabilities.KnownPart()->value,req.capabilities);
    if(comparison.delta.Size()==0&&sourceFits&&rasterMatches)return finish(PreparationPath::DirectUse);
    if(comparison.delta.Size()==0&&sourceFits&&Established(req.metadataOnly)&&req.metadataOnly.KnownPart()->value)
        return finish(PreparationPath::MetadataOnly);
    if(req.consumerTransform&&comparison.delta.Size()>0)
    {
        const auto& rule=*req.consumerTransform;bool matches=!rule.operation.Empty()&&rule.version>0&&rule.precision==req.precision&&
            CompleteDescriptor(rule.output)&&SemanticEqual(rule.output,req.output)&&rule.capabilities==req.capabilities&&
            SemanticEqual(rule.mapping,req.mapping)&&rule.steps.Size()==comparison.delta.Size()&&ValidLineage(rule.steps,false,reader);
        for(std::size_t i=0;matches&&i<rule.steps.Size();++i)matches=SameTransform(*rule.steps.Get(i),*comparison.delta.Get(i),reader);
        if(matches&&HasCapabilities(facts.capabilities.KnownPart()->value,req.capabilities))return finish(PreparationPath::ConsumerTransform);
    }
    // Structural planning never selects a content-bearing PreparedView. Exact
    // content planning remains a separate fresh operation, even with known data.
    if(structuralOnly)prepared={};
    if(prepared.size()>32)return out;
    // Input order cannot choose among compatible prepared outputs. Use the exact owner key order.
    const C::PreparedView* chosen=nullptr;
    for(const auto& item:prepared)
    {
        ++out.evidence.preparedLookups;
        const auto compatible=EvaluatePreparedViewCompatibility(item,*out.key,*source,*frame,req.previous,req.pairRequired,reader);
        if(!compatible.compatible){out.evidence.lastMismatch=compatible.mismatch;continue;}
        if(!chosen||KeyLess(item.prepared->header.record,chosen->header.record))chosen=item.prepared;
    }
    if(chosen)
    {
        C::Symbol type;type.Assign(C::PreparedView::WireName);out.prepared=C::ContractRef<C::ContractId::C12>{type,chosen->header.record,chosen->header.revision};
        out.access=chosen->requiredAccess;return finish(PreparationPath::ExistingPreparedView);
    }
    // A descriptor/precision/capability mismatch is not itself an executable recipe.
    if(out.delta.Size()==0)return out;
    out.allocation=BuildAllocationRequirementKey(*frame,source->device,req.output,req.capabilities,req.allocationClass);
    if(!out.allocation)return out;++out.evidence.allocationKeys;++out.evidence.conversions;
    return finish(PreparationPath::NewConversion);
}
template<class Reader> PreparationDecision DeriveStructuralRepresentationPlan(const C::CanonicalFrameContext& context,
    const C::QualificationCertificate& certificate,const SemanticProfile& profile,const RepresentationRequirement& req,
    const SourceRepresentationFacts& facts,const Reader& reader)
{return DeriveRepresentationPlan(context,certificate,profile,req,facts,{},reader,true);}
// Separate context-owner publication after pure selection. Intern must compare complete
// typed values and retain immutable publications; it must not allocate GPU resources.
template<class Store> std::optional<C::ImmutableRecord<C::RepresentationPlan>> PublishRepresentationPlan(
    const PreparationDecision& decision,const C::RecordHeader& header,const C::EvidenceRef& evidence,Store& store)
{
    if(!decision.key||decision.path==PreparationPath::Unavailable||decision.path==PreparationPath::NoWork||
       !ValidValues(header)||C::CheckHeader(header,C::ContractId::C12)!=C::Error::None||header.owner!=C::OwnerDomain::Context||
       !Lifecycle::ValidEvidence(evidence))return {};
    const auto& key=*decision.key;C::RepresentationPlan plan;plan.header=header;plan.source=decision.sourceIdentity;
    plan.profile=key.profile;plan.purpose=key.purpose;plan.raster=store.Publish(key.mapping,C::OwnerDomain::Context);
    if(!PublishList(decision.delta,store,plan.transforms))return {};
    plan.precision=C::OptionalFact<C::Symbol>::FromKnown(key.precision,evidence);plan.requiredCapabilities=key.capabilities;
    plan.output=key.output;plan.keys.semantic=store.Intern(key.semantic);plan.keys.plan=store.Intern(key);
    if(decision.allocation)plan.keys.allocation=C::OptionalFact<C::RecordKey>::FromKnown(store.Intern(*decision.allocation),evidence);
    plan.keys.content=plan.source;const auto& g=key.semantic.representation.identity;
    plan.keys.generation=C::OptionalFact<C::RepresentationGeneration>::FromKnown({g.nameSpace,g.issuer,g.value},evidence);
    plan.accessRequirements=decision.access;plan.retirementContract=decision.access.retirementContract;
    if(!ValidValues(plan))return {};return C::ImmutableRecord<C::RepresentationPlan>{plan};
}
} // namespace Neurotic::Context
