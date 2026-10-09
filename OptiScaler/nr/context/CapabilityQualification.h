#pragma once
#include "ContextBuilder.h"
#include "NativeNgxSchemaQualification.h"
#include "../contracts/C04_Capability.h"

namespace Neurotic::Context
{
struct FieldRequirement {C::Symbol field;bool required=true;};
struct SourceSchemaRule {C::Symbol schema;C::VersionNumber version;SourceSchemaDomain domain=SourceSchemaDomain::Provider;};
// Consumer-owned, versioned policy input. These are construction values, not a second
// public contract or an executable route. A publication key must never be repurposed.
struct SemanticProfile
{
    C::ProfileKey key;
    C::Symbol purpose,rule;
    std::uint32_t ruleVersion=1;
    C::RecordKey publication;
    C::BoundedList<FieldRequirement,16> requirements;
    C::BoundedList<SourceSchemaRule,8> sourceSchemas;
    C::BoundedList<C::SourceClass,8> acceptedEvidence;
    C::OptionalFact<std::uint64_t> maximumAge;
    // Zero retains real-frame units; version one requires exact Native callback
    // freshness and forbids a real-frame maximum age.
    std::uint32_t nativeSampleDomainVersion=0;
    C::BoundaryKind boundary=C::BoundaryKind::Acquisition;
    C::BoundedList<Lifecycle::GenerationAxis,32> generationAxes;
    bool requireExactContent=false;
};
struct CapabilityResult
{
    std::optional<C::ImmutableRecord<C::QualificationCertificate>> certificate;
    C::BoundedList<std::pair<C::RecordKey,C::UnknownFact>,64> rejected;
    C::UnknownFact refusal=Missing("CTX.UnsupportedConsumerPurpose",C::UnknownReason::UnsupportedSchema);
};
inline bool NativeNgxAdapterProfile(const SemanticProfile& profile)
{
    if(profile.nativeSampleDomainVersion!=1||profile.purpose.View()!="RENDER.NativeSemantics"||
       profile.boundary!=C::BoundaryKind::BeforeUpscale||profile.acceptedEvidence.Size()!=1||
       *profile.acceptedEvidence.Get(0)!=C::SourceClass::Native||profile.sourceSchemas.Size()!=1)return false;
    const auto& rule=*profile.sourceSchemas.Get(0);
    return rule.domain==SourceSchemaDomain::NativeNgxAdapter&&rule.schema.View()=="NGX"&&rule.version==C::VersionNumber{1,0};
}
inline bool ValidProfile(const SemanticProfile& profile)
{
    if (!ValidValues(profile.key) || profile.purpose.Empty() || profile.rule.View()!="CTX.Semantics" ||
        profile.ruleVersion!=1 || profile.publication.Check()!=C::Error::None ||
        profile.nativeSampleDomainVersion>1 ||
        (profile.nativeSampleDomainVersion==0?!Established(profile.maximumAge):
            (profile.maximumAge.IsKnown()||profile.purpose.View()!="RENDER.NativeSemantics")) ||
        profile.acceptedEvidence.Size()==0 ||
        profile.sourceSchemas.Size()==0 || profile.requirements.Size()==0 || C::EnumName(profile.boundary).empty()) return false;
    for (std::size_t i=0;i<profile.requirements.Size();++i)
    {
        if (profile.requirements.Get(i)->field.Empty()) return false;
        for (std::size_t j=0;j<i;++j) if (profile.requirements.Get(i)->field==profile.requirements.Get(j)->field) return false;
    }
    for (const auto& schema:profile.sourceSchemas)
        if (schema.schema.Empty() || !schema.version.major ||
            (schema.domain!=SourceSchemaDomain::Provider&&
             (schema.domain!=SourceSchemaDomain::NativeNgxAdapter||!NativeNgxAdapterProfile(profile)))) return false;
    for (const auto source:profile.acceptedEvidence) if (C::EnumName(source).empty()) return false;
    for (std::size_t i=0;i<profile.generationAxes.Size();++i)
    {
        if (!Lifecycle::ValidAxis(*profile.generationAxes.Get(i))) return false;
        for (std::size_t j=0;j<i;++j) if (*profile.generationAxes.Get(i)==*profile.generationAxes.Get(j)) return false;
    }
    return true;
}
template<class Store> bool SelectGenerations(const C::CanonicalFrameContext& context,const SemanticProfile& profile,
                                            const Store& store,C::GenerationVector& selected)
{
    const auto* available=ResolveMetadata(context.generations,store);if (!available) return false;
    for (const auto& axis:profile.generationAxes)
    {
        bool found=false;
        for (const auto& token:available->Entries()) if (Lifecycle::SameAxis(axis,token))
        {if (selected.Insert(token)!=C::Error::None) return false;found=true;break;}
        if (!found) return false;
    }
    return true;
}
// This checks publication bindings only. It is not an admission cache: source freshness
// and disappearance must be checked through EvaluateSemanticCapability for current use.
template<class Store> bool CertificateMatches(const C::QualificationCertificate& certificate,
                                              const C::CanonicalFrameContext& context,const SemanticProfile& profile,
                                              const Store& store)
{
    if (!ValidProfile(profile) || !ValidValues(certificate) || !ValidValues(context) ||
        certificate.header.owner!=C::OwnerDomain::Context || certificate.header.scope!=context.header.scope ||
        certificate.context.recordType.View()!=C::CanonicalFrameContext::WireName ||
        certificate.context.record!=context.header.record || certificate.context.revision!=context.header.revision ||
        certificate.profile!=profile.key || certificate.purpose!=profile.purpose ||
        certificate.evidenceVersion!=profile.ruleVersion) return false;
    const auto* frame=ResolveMetadata(context.frame,store);if(!frame)return false;
    if(profile.nativeSampleDomainVersion==1)
    {
        C::EvidenceVector freshness;freshness.nativeFreshness=certificate.nativeFreshness;
        freshness.ageInRealFrames=certificate.maximumAge;
        if(!NativeEvidenceFresh(freshness,*frame,store))return false;
    }
    else if(frame->nativeSample||certificate.nativeFreshness||!SameFact(certificate.maximumAge,profile.maximumAge))return false;
    const auto* signature=ResolveMetadata(certificate.signature,store);
    if (!signature || signature->schema!=profile.rule || signature->version!=profile.ruleVersion) return false;
    const C::BoundedList<C::RecordKey,32>* dependencies=nullptr;
    if (!ResolveList(signature->structuralDependencies,store,dependencies) || !dependencies || dependencies->Size()!=1 ||
        *dependencies->Get(0)!=profile.publication) return false;
    const auto* generations=ResolveMetadata(signature->generations,store);C::GenerationVector selected;
    return generations && SelectGenerations(context,profile,store,selected) && *generations==selected;
}
template<class Store> bool FieldEstablished(std::string_view field,const C::CanonicalFrameContext& context,
                                           const Store& store,bool& supported)
{
    supported=true;
    if (field=="jitter") return Established(context.jitter) && Finite(context.jitter.KnownPart()->value);
    if (field=="rrActive") return Established(context.rrActive);
    if (field=="fgActive") return Established(context.fgActive);
    if (field=="color.domain" || field=="color.scene-linear" || field=="color.alpha")
    {
        const auto* color=ResolveOptional(context.color,store);if (!color) return false;
        if (field=="color.alpha") return Established(color->alpha) && color->alpha.KnownPart()->value!=C::AlphaMeaning::Unspecified;
        return Established(color->domain) && (field=="color.domain" || color->domain.KnownPart()->value==C::ColorDomain::SceneLinear);
    }
    if (field=="depth.device" || field=="depth.view")
    {
        const auto* depth=ResolveOptional(context.depth,store);if (!depth || !Established(depth->kind)) return false;
        if (field=="depth.device") return depth->kind.KnownPart()->value==C::DepthKind::Device;
        if (depth->kind.KnownPart()->value==C::DepthKind::PositiveViewAxis) return true;
        return DeviceToViewDepth(0.5,*depth).IsKnown();
    }
    if (field=="exposure.preExposure" || field=="exposure.effective")
    {
        const auto* exposure=ResolveOptional(context.exposure,store);if (!exposure) return false;
        return field=="exposure.preExposure"?UndoPreExposure(1,*exposure).IsKnown():ApplyExposure(1,*exposure).IsKnown();
    }
    if (field=="motion.native")
    {
        const auto* motion=ResolveOptional(context.motion,store);const auto* frame=ResolveMetadata(context.frame,store);
        return motion&&frame&&NativeMotionMatches(*motion,*frame,store);
    }
    if (field=="motion.canonical")
    {
        const auto* motion=ResolveOptional(context.motion,store);
        const auto* frame=ResolveMetadata(context.frame,store);
        const auto* raster=ResolveMetadata(context.renderRaster,store);
        return motion && frame && raster && ValidRaster(*raster) &&
            Lifecycle::CompareTypedIdentity(motion->framePair.current,frame->baseRealFrameId)==Lifecycle::IdentityStatus::Ok &&
            SemanticEqual(motion->currentRaster,*raster) &&
            Established(motion->currentRaster.axisSigns) && motion->currentRaster.axisSigns.KnownPart()->value==C::Vec2{1,1} &&
            Established(motion->previousRaster.axisSigns) && motion->previousRaster.axisSigns.KnownPart()->value==C::Vec2{1,1} &&
            Established(motion->units) && motion->units.KnownPart()->value==C::MotionUnits::ActiveRasterUV &&
            Established(motion->scale) && motion->scale.KnownPart()->value==C::Vec2{1,1} &&
            Established(motion->jitterConvention) && motion->jitterConvention.KnownPart()->value==C::JitterConvention::Unjittered &&
            MotionToUv({0,0},*motion).IsKnown();
    }
    if (field.starts_with("mask."))
    {
        const auto* masks=ResolveMetadata(context.masks,store);if (!masks) return false;
        const auto* raster=ResolveMetadata(context.renderRaster,store);if (!raster || !Established(raster->active)) return false;
        bool known=false,matched=false;
        std::apply([&](const auto&... f){([&]{if (field.substr(5)==f.name)
        {matched=true;const auto& fact=masks->*(f.pointer);known=Established(fact) && SameFact(fact.KnownPart()->value.coverage,raster->active);}}(),...);},C::MaskSet::Fields());
        supported=matched;return known;
    }
    supported=false;return false;
}
template<class Store>
bool FieldBound(std::string_view field,const C::CanonicalFrameContext& context,
                std::span<const AdmittedCandidate> admitted,Store& store,C::UnknownFact& failure)
{
    const auto bound=[&](const auto& expected,const auto& observed) {
        if (const auto* unknown=expected.UnknownPart()) {failure=*unknown;return false;}
        if (const auto* unknown=observed.UnknownPart()) {failure=*unknown;return false;}
        if (!Established(expected) || !Established(observed) ||
            !SemanticEqual(expected.KnownPart()->value,observed.KnownPart()->value))
        {failure=Missing("CTX.ContradictoryEvidence",C::UnknownReason::ContradictoryEvidence);return false;}
        return true;
    };
    if (field=="jitter")
    {
        const auto* frame=ResolveMetadata(context.frame,store);
        const auto* raster=ResolveMetadata(context.renderRaster,store);if (!frame || !raster) return false;
        std::array<FactCandidate<C::Vec2>,64> values{};std::size_t count=0;
        for (const auto& item:admitted)
            values[count++]={item.candidate->header.record,InterpretJitter(*item.candidate,*frame,*raster,store),item.evidence};
        return bound(context.jitter,ResolveEvidence<C::Vec2>(std::span(values.data(),count)).value);
    }
    if (field=="rrActive") return bound(context.rrActive,ResolveMember(admitted,&C::AcquisitionCandidate::rrActive).value);
    if (field=="fgActive") return bound(context.fgActive,ResolveMember(admitted,&C::AcquisitionCandidate::fgActive).value);
    // Resolve only the descriptor used by this purpose. An unrelated stale source
    // or an unavailable optional descriptor cannot veto another semantic field.
    C::BoundedList<C::Contradiction,8> conflicts;
    const auto description=[&]<class T>(C::OptionalFact<C::MetadataRef<T>> C::AcquisitionCandidate::* member,
                                        const C::OptionalFact<C::MetadataRef<T>>& expected,auto compare) {
        if (const auto* unknown=expected.UnknownPart()) {failure=*unknown;return false;}
        const auto* original=ResolveOptional(expected,store);if (!original) return false;
        C::OptionalFact<C::MetadataRef<T>> resolved;
        if (!ResolveDescription(admitted,member,field,store,resolved,conflicts)) return false;
        if (const auto* unknown=resolved.UnknownPart()) {failure=*unknown;return false;}
        const auto* actual=ResolveOptional(resolved,store);
        return actual && compare(*original,*actual);
    };
    if (field.starts_with("color.")) return description(&C::AcquisitionCandidate::color,context.color,
        [&](const auto& a,const auto& b){return field=="color.alpha"?bound(a.alpha,b.alpha):bound(a.domain,b.domain);});
    if (field.starts_with("depth.")) return description(&C::AcquisitionCandidate::depth,context.depth,
        [&](const auto& a,const auto& b){
            if (!bound(a.kind,b.kind)) return false;
            return field=="depth.device" || a.kind.KnownPart()->value==C::DepthKind::PositiveViewAxis ||
                (bound(a.projection,b.projection) && bound(a.projectionConvention,b.projectionConvention));
        });
    if (field.starts_with("exposure.")) return description(&C::AcquisitionCandidate::exposure,context.exposure,
        [&](const auto& a,const auto& b){return field=="exposure.effective"?bound(a.effectiveExposure,b.effectiveExposure):
            (bound(a.preExposure,b.preExposure) && bound(a.preExposureRelation,b.preExposureRelation));});
    if (field=="motion.canonical"||field=="motion.native") return description(&C::AcquisitionCandidate::motion,context.motion,
        [&](const auto& a,const auto& b){return SemanticEqual(a,b);});
    if (field.starts_with("mask."))
    {
        const auto* expected=ResolveMetadata(context.masks,store);if (!expected) return false;
        bool matched=false;
        std::apply([&](const auto&... f){([&]{if (field.substr(5)==f.name) {
            using Fact=std::remove_cvref_t<decltype(expected->*(f.pointer))>;using T=typename Fact::value_type;
            std::array<FactCandidate<T>,64> facts{};std::size_t count=0;
            for (const auto& item:admitted) if (const auto* masks=ResolveMetadata(item.candidate->masks,store))
                facts[count++]={item.candidate->header.record,masks->*(f.pointer),item.evidence};
            matched=bound(expected->*(f.pointer),ResolveEvidence<T>(std::span(facts.data(),count),
                [](const T& a,const T& b){return SemanticEqual(a,b);}).value);
        }}(),...);},C::MaskSet::Fields());
        return matched;
    }
    return false;
}

template<class Store>
bool AdmitSource(const C::AcquisitionCandidate& source,const C::FrameIdentity& frame,
                 const C::BoundaryDescription& boundary,const SemanticProfile& profile,
                 const Store& store,AdmittedCandidate& admitted,C::UnknownFact& refusal,
                 std::span<const SourceQualification> qualifications={})
{
    const auto reject=[&](std::string_view reason){refusal=Missing(reason);return false;};
    const auto* evidence=SourceEvidence(source,store,qualifications);const auto* sourceFrame=ResolveMetadata(source.frame,store);
    if (!evidence || !sourceFrame) return reject("CTX.SourceDisappeared");
    if (!SameObservationScope(frame,*sourceFrame,store,false) ||
        !SameObservationBoundary(boundary,source.boundary,frame,store) ||
        !SameObservationBoundary(boundary,source.boundary,*sourceFrame,store)) return reject("CTX.IncompatibleScope");
    if (!Established(source.providerIncarnation)) return reject("CTX.WrongIncarnationOrGeneration");
    const bool adapter=NativeNgxAdapterProfile(profile);
    if (!adapter&&!Established(source.sourceVersion)) return reject("CTX.UnsupportedSchema");
    if(profile.nativeSampleDomainVersion==1)
    {
        if(!NativeEvidenceFresh(*evidence,frame,store)||
            !Established(source.descriptiveLifetime.callbackScope)||
            source.descriptiveLifetime.callbackScope.KnownPart()->value!=evidence->nativeFreshness->callback||
            (profile.requireExactContent&&!Established(source.contentRevision)))return reject("CTX.StaleContent");
    }
    else
    {
        if(frame.nativeSample||sourceFrame->nativeSample||evidence->nativeFreshness)return reject("CTX.IncompatibleScope");
        if (!Established(evidence->ageInRealFrames) || !Established(profile.maximumAge) ||
            evidence->ageInRealFrames.KnownPart()->value>profile.maximumAge.KnownPart()->value ||
            (profile.requireExactContent && (!Established(source.contentRevision) || evidence->ageInRealFrames.KnownPart()->value!=0)))
            return reject("CTX.StaleContent");
        if (!Established(frame.baseRealFrameId) || !Established(sourceFrame->baseRealFrameId)) return reject("CTX.AmbiguousAssociation");
        const auto targetBase=frame.baseRealFrameId.KnownPart()->value.Describe();
        const auto sourceBase=sourceFrame->baseRealFrameId.KnownPart()->value.Describe();
        if (targetBase.nameSpace!=sourceBase.nameSpace || targetBase.issuer!=sourceBase.issuer ||
            (evidence->ageInRealFrames.KnownPart()->value==0 && targetBase.value!=sourceBase.value)) return reject("CTX.IncompatibleScope");
    }
    if (!Established(evidence->provenance) || !Established(evidence->semanticCertainty) ||
        evidence->semanticCertainty.KnownPart()->value!=C::SemanticCertainty::Qualified ||
        !Established(evidence->coverage) || evidence->coverage.KnownPart()->value!=C::CoverageKind::Complete)
        return reject("CTX.UnknownRequiredFact");
    bool accepted=false;for (const auto kind:profile.acceptedEvidence) if (kind==evidence->provenance.KnownPart()->value) accepted=true;
    bool schema=false;for (const auto& rule:profile.sourceSchemas)
        if (rule.schema==source.sourceSchema)
        {
            if(rule.domain==SourceSchemaDomain::Provider&&Established(source.sourceVersion)&&
               rule.version==source.sourceVersion.KnownPart()->value)schema=true;
            if(rule.domain==SourceSchemaDomain::NativeNgxAdapter&&adapter&&NativeNgxAdapterClaims(source,store))schema=true;
        }
    if (!schema) return reject("CTX.UnsupportedSchema");
    if (!accepted) return reject("CTX.UnsupportedConsumerPurpose");
    admitted={&source,evidence};return true;
}
inline bool SourceMentions(std::string_view field,const C::AcquisitionCandidate& source)
{
    if (field=="jitter") return source.jitter.IsKnown();
    if (field=="rrActive") return source.rrActive.IsKnown();
    if (field=="fgActive") return source.fgActive.IsKnown();
    if (field.starts_with("color.")) return source.color.IsKnown();
    if (field.starts_with("depth.")) return source.depth.IsKnown();
    if (field.starts_with("exposure.")) return source.exposure.IsKnown();
    if (field=="motion.canonical"||field=="motion.native") return source.motion.IsKnown();
    return field.starts_with("mask.");
}
// A distinct context revision for a declared semantic purpose, as allowed by CTX
// specification F.4. The caller owns immutable record identity/publication. This
// neither changes an existing observation context nor selects an executable route.
#define NR_CTX_PURPOSE_CONTEXT 1
template<class Store>
ContextResult BuildContextForPurpose(const C::ObservationSet& observations,
                                    std::span<const C::AcquisitionCandidate> sources,
                                    const ContextRequest& request,const SemanticProfile& profile,Store& store,
                                    const C::BoundedList<C::Symbol,16>* claimProjection=nullptr,
                                    std::span<const SourceQualification> qualifications={})
{
    ContextResult refused;if (!ValidProfile(profile) || sources.size()>64) return refused;
    const auto* frame=ResolveMetadata(request.frame,store);if (!frame) return refused;
    C::BoundedList<C::RecordKey,64> keys;
    for (const auto& source:sources)
    {
        AdmittedCandidate admitted;C::UnknownFact reason=Missing("CTX.MalformedObservation");
        if (ValidValues(source) && AdmitSource(source,*frame,request.boundary,profile,store,admitted,reason,qualifications)) keys.Push(source.header.record);
        else refused.rejected.Push({source.header.record,reason});
    }
    auto result=BuildCanonicalFrameContext(observations,sources,request,store,&keys,claimProjection,qualifications);
    for (const auto& rejection:refused.rejected) if (!result.rejected.Push(rejection)) return refused;
    return result;
}

template<class Store>
CapabilityResult EvaluateSemanticCapability(const C::CanonicalFrameContext& context,
                                              std::span<const C::AcquisitionCandidate> sources,
                                              const SemanticProfile& profile,const C::RecordHeader& header,Store& store,
                                              std::span<const SourceQualification> qualifications={})
{
    CapabilityResult result;
    if (!ValidProfile(profile) || !ValidValues(context) || !ValidValues(header) ||
        header.contract!=C::ContractId::C04 || header.owner!=C::OwnerDomain::Context || header.scope!=context.header.scope || sources.size()>64) return result;
    const auto* frame=ResolveMetadata(context.frame,store);
    if (!frame || !SameObservationScope(*frame,*frame,store) ||
        (frame->nativeSample.has_value()!=(profile.nativeSampleDomainVersion==1)) || !Established(context.boundary.kind) ||
        context.boundary.kind.KnownPart()->value!=profile.boundary) return result;
    const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* references=nullptr;
    if (!ResolveList(context.coherentCandidates,store,references) || !references || references->Size()==0) return result;
    // Re-resolve purpose dependencies from exact, currently admissible C01 publications.
    // Freshness is evaluated here, never put into StructuralSignature/GenerationVector.
    std::array<AdmittedCandidate,64> admitted{};std::size_t admittedCount=0;
    for (const auto& ref:*references)
    {
        const C::AcquisitionCandidate* source=nullptr;bool ambiguous=false;
        for (const auto& candidate:sources) if (candidate.header.record==ref.record)
        {if (source || candidate.header.revision!=ref.revision) ambiguous=true;source=&candidate;}
        if (ambiguous || !source || ref.recordType.View()!=C::AcquisitionCandidate::WireName || !ValidValues(*source))
        {result.rejected.Push({ref.record,Missing(source?"CTX.MalformedObservation":"CTX.SourceDisappeared")});continue;}
        C::UnknownFact reason;
        if (AdmitSource(*source,*frame,context.boundary,profile,store,admitted[admittedCount],reason,qualifications)) ++admittedCount;
        else result.rejected.Push({ref.record,reason});
    }
    std::sort(result.rejected.begin(),result.rejected.end(),[](const auto& a,const auto& b){return KeyLess(a.first,b.first);});
    C::QualificationCertificate certificate;certificate.header=header;certificate.context.recordType.Assign(C::CanonicalFrameContext::WireName);
    certificate.context.record=context.header.record;certificate.context.revision=context.header.revision;
    certificate.profile=profile.key;certificate.purpose=profile.purpose;certificate.evidenceVersion=profile.ruleVersion;
    certificate.maximumAge=profile.maximumAge;certificate.acceptedEvidence=profile.acceptedEvidence;
    if(profile.nativeSampleDomainVersion==1)
    {
        const auto* sample=ResolveNativeSample(*frame,store);if(!sample)return result;
        certificate.nativeFreshness=NativeFreshness(*frame->nativeSample,*sample);
    }
    certificate.eligibility=C::Eligibility::Eligible;
    C::BoundedList<C::Symbol,32> missing;C::BoundedList<C::PolicyReason,16> reasons;
    for (const auto& requirement:profile.requirements)
    {
        bool supported=false;C::UnknownFact failure=Missing("CTX.UnknownRequiredFact");
        const bool semantic=FieldEstablished(requirement.field.View(),context,store,supported);
        if (!supported) return result;
        const bool bound=FieldBound(requirement.field.View(),context,std::span(admitted.data(),admittedCount),store,failure);
        const bool established=semantic && bound;
        if (!established)
        {
            // Preserve an owner Unknown first. Otherwise identify a required source
            // that was excluded by this purpose instead of erasing its rejection.
            if (failure.reason.ownerCode==Missing("CTX.UnknownRequiredFact").reason.ownerCode)
                for (const auto& rejection:result.rejected)
                {
                    const C::AcquisitionCandidate* candidate=nullptr;
                    for (const auto& source:sources) if (source.header.record==rejection.first) candidate=&source;
                    if (!candidate || SourceMentions(requirement.field.View(),*candidate)) {failure=rejection.second;break;}
                }
            if (!missing.Push(requirement.field)) return result;
            C::PolicyReason reason;reason.reason=failure.reason;
            reason.rule=profile.rule;reason.ruleVersion=profile.ruleVersion;reason.hard=requirement.required;
            if (!reasons.Push(reason)) return result;
            if (requirement.required) certificate.eligibility=C::Eligibility::Ineligible;
        }
    }
    C::GenerationVector selected;if (!SelectGenerations(context,profile,store,selected)) return result;
    C::StructuralSignature signature;signature.schema=profile.rule;signature.version=profile.ruleVersion;
    signature.generations=store.Publish(selected);C::BoundedList<C::RecordKey,32> dependencies;dependencies.Push(profile.publication);
    if (!PublishList(dependencies,store,signature.structuralDependencies)) return result;
    certificate.signature=store.Publish(signature);certificate.contradictions=context.contradictions;
    if (!PublishList(missing,store,certificate.missingFields) || !PublishList(reasons,store,certificate.reasons) ||
        !CertificateMatches(certificate,context,profile,store)) return result;
    result.certificate.emplace(certificate);return result;
}
} // namespace Neurotic::Context
