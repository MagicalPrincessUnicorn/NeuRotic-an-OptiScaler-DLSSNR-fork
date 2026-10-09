#pragma once
#include "ContextMetadata.h"
#include "NativeSampleQualification.h"
#include "NativeJitterBasis.h"
#include "../contracts/C02_Context.h"

namespace Neurotic::Context
{
// CTX owns no store or identity registry. Reader/publisher must keep every input and
// newly published CPU body immutable and alive for the entire returned revision.
struct ContextRequest
{
    C::RecordHeader header;
    C::MetadataRef<C::FrameIdentity> frame;
    C::MetadataRef<C::GenerationVector> generations;
    C::MetadataRef<C::RasterDescription> renderRaster,outputRaster;
    C::MetadataRef<C::EvidenceVector> continuity;
    C::BoundaryDescription boundary;
};
struct ContextResult
{
    std::optional<C::ImmutableRecord<C::CanonicalFrameContext>> context;
    C::BoundedList<std::pair<C::RecordKey,C::UnknownFact>,64> rejected;
    C::UnknownFact refusal=Missing("CTX.MalformedObservation",C::UnknownReason::Malformed);
};
template<class T,std::size_t N,class Publisher>
bool PublishList(const C::BoundedList<T,N>& values,Publisher& publisher,C::MetadataList<T,N>& result)
{
    result={};
    if (!values.Size()) return true;
    auto reference=publisher.Publish(values);
    if (!Lifecycle::ValidMetadataDescriptor(reference) || reference.owner!=C::OwnerDomain::Context) return false;
    result.count=static_cast<std::uint32_t>(values.Size());result.backing=reference;return true;
}
inline bool SameBoundary(const C::BoundaryDescription& a,const C::BoundaryDescription& b)
{
    if (!SameFact(a.kind,b.kind) || !SameFact(a.episode,b.episode)) return false;
    const auto consistent=[](const auto& x,const auto& y){return !x.IsKnown() || !y.IsKnown() || SameFact(x,y);};
    return consistent(a.hudIncluded,b.hudIncluded) && consistent(a.toneMapped,b.toneMapped) &&
           consistent(a.afterRequiredHostRendering,b.afterRequiredHostRendering);
}
// Native callback identity is its own observation domain. An absent real-frame
// episode is not fabricated or compared as a known fact. Callers also prove the
// exact source sample with SameObservationScope; ordinary frame rules stay strict.
template<class Reader>bool SameObservationBoundary(const C::BoundaryDescription& a,const C::BoundaryDescription& b,
    const C::FrameIdentity& frame,const Reader& reader)
{
    if(!frame.nativeSample)return SameBoundary(a,b);
    if(!ResolveNativeSample(frame,reader)||!SameFact(a.kind,b.kind))return false;
    if(a.episode.IsKnown()||b.episode.IsKnown()||frame.episodeId.IsKnown())
        return SameBoundary(a,b)&&SameFact(frame.episodeId,a.episode);
    const auto absent=[](const auto& value){return value.UnknownPart()&&*value.UnknownPart()==C::UnknownFact{};};
    if(!absent(a.episode)||!absent(b.episode)||!absent(frame.episodeId))return false;
    const auto consistent=[](const auto& x,const auto& y){return !x.IsKnown()||!y.IsKnown()||SameFact(x,y);};
    return consistent(a.hudIncluded,b.hudIncluded)&&consistent(a.toneMapped,b.toneMapped)&&
        consistent(a.afterRequiredHostRendering,b.afterRequiredHostRendering);
}
struct AdmittedCandidate
{
    const C::AcquisitionCandidate* candidate=nullptr;
    const C::EvidenceVector* evidence=nullptr;
};
// NFC's derived evidence ledger. Original C01 bodies remain immutable. Only a
// Context publication may supply a current qualification for an exact C01 ref.
struct SourceQualification
{
    C::ContractRef<C::ContractId::C01> source;
    C::MetadataRef<C::EvidenceVector> evidence;
};
template<class Store>const C::EvidenceVector* SourceEvidence(const C::AcquisitionCandidate& source,
    const Store& store,std::span<const SourceQualification> qualifications={})
{
    const C::EvidenceVector* qualified=nullptr;
    for(const auto& item:qualifications)if(item.source.record==source.header.record)
    {
        if(qualified||item.source.recordType.View()!=C::AcquisitionCandidate::WireName||
           item.source.revision!=source.header.revision||item.evidence.owner!=C::OwnerDomain::Context)return nullptr;
        qualified=ResolveMetadata(item.evidence,store);if(!qualified)return nullptr;
    }
    return qualified?qualified:ResolveMetadata(source.evidence,store);
}
template<class T> bool RecordContradiction(const ResolvedFact<T>& resolved,std::string_view field,
                                          C::BoundedList<C::Contradiction,8>& contradictions)
{
    if (!resolved.contradiction || resolved.sources.Size()<2) return true;
    C::Contradiction conflict;
    if (!conflict.field.Assign(field)) return false;
    conflict.reason=Missing("CTX.ContradictoryEvidence",C::UnknownReason::ContradictoryEvidence).reason;
    for (const auto& key:resolved.sources)
    {
        bool exists=false;for (const auto& prior:conflict.claims) if (prior==key) exists=true;
        if (!exists && !conflict.claims.Push(key)) return false;
    }
    return conflict.claims.Size()<2 || contradictions.Push(conflict);
}
template<class T> ResolvedFact<T> ResolveMember(std::span<const AdmittedCandidate> candidates,
                                                C::OptionalFact<T> C::AcquisitionCandidate::* member)
{
    std::array<FactCandidate<T>,64> values{};std::size_t count=0;
    for (const auto& item:candidates)
        values[count++]={item.candidate->header.record,item.candidate->*member,item.evidence};
    return ResolveEvidence<T>(std::span(values.data(),count));
}
template<class Store>
C::OptionalFact<C::Vec2> InterpretJitter(const C::AcquisitionCandidate& candidate,const C::FrameIdentity& frame,
                                       const C::RasterDescription& targetRaster,const Store& store)
{
    if (!candidate.jitter.IsKnown()) return candidate.jitter;
    if (!Established(candidate.jitter) || !Finite(candidate.jitter.KnownPart()->value))
        return Reject<C::Vec2>("CTX.MalformedObservation");
    const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
    if (!ResolveList(candidate.claims,store,claims) || !claims) return Reject<C::Vec2>("CTX.UnknownRequiredFact");
    const C::Symbol* units=nullptr;const C::Symbol* basis=nullptr;
    for (const auto& claim:*claims) if (claim.field.View()=="jitter.units" || claim.field.View()=="jitter.basis")
    {
        if (!Established(claim.raw) || (claim.effective.IsKnown() && !SameFact(claim.raw,claim.effective)))
            return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
        const auto* symbol=std::get_if<C::Symbol>(&claim.raw.KnownPart()->value);
        auto*& destination=claim.field.View()=="jitter.units"?units:basis;
        if (!symbol || destination) return Reject<C::Vec2>("CTX.MalformedObservation");
        destination=symbol;
    }
    if (!units || !basis) return Reject<C::Vec2>("CTX.UnknownRequiredFact");
    const C::RasterDescription* sourceRaster=nullptr;
    if(basis->View()=="payload.raster")
    {
        const auto* view=NativeJitterResource(candidate,frame,store);
        if(!view||units->View()!="Pixels")return Reject<C::Vec2>("CTX.InvalidRasterBasis");
        sourceRaster=&view->raster;
    }
    else if(basis->View()=="motion.currentRaster")
    {
        const auto* description=ResolveOptional(candidate.motion,store);
        if(!description)return Reject<C::Vec2>("CTX.InvalidRasterBasis");
        if(description->framePair.current.IsKnown()&&
           Lifecycle::CompareTypedIdentity(description->framePair.current,frame.baseRealFrameId)!=Lifecycle::IdentityStatus::Ok)
            return Reject<C::Vec2>("CTX.InvalidFramePair");
        sourceRaster=&description->currentRaster;
    }
    if(!sourceRaster||!ValidRaster(*sourceRaster)||!ValidRaster(targetRaster))return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    auto canonicalRaster=*sourceRaster;canonicalRaster.axisSigns=targetRaster.axisSigns;
    if (!SemanticEqual(canonicalRaster,targetRaster) || targetRaster.axisSigns.KnownPart()->value!=C::Vec2{1,1})
        return Reject<C::Vec2>("CTX.InvalidRasterBasis");
    C::MotionUnits declared;
    if (!C::ParseEnum(units->View(),declared)) return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
    auto value=candidate.jitter.KnownPart()->value;
    const auto signs=sourceRaster->axisSigns.KnownPart()->value;
    value.x*=signs.x;value.y*=signs.y;
    if (declared==C::MotionUnits::Pixels)
    {const auto rect=sourceRaster->active.KnownPart()->value;value.x/=rect.width;value.y/=rect.height;}
    else if (declared==C::MotionUnits::NDC) {value.x*=.5;value.y*=.5;}
    else if (declared!=C::MotionUnits::ActiveRasterUV) return Reject<C::Vec2>("CTX.UnsupportedSemanticType");
    if (!Finite(value)) return Reject<C::Vec2>("CTX.MalformedObservation");
    return C::OptionalFact<C::Vec2>::FromKnown(value,candidate.jitter.KnownPart()->evidence);
}
template<class T,class Store>
bool ResolveDescription(std::span<const AdmittedCandidate> candidates,
                        C::OptionalFact<C::MetadataRef<T>> C::AcquisitionCandidate::* member,
                        std::string_view name,Store& store,C::OptionalFact<C::MetadataRef<T>>& output,
                        C::BoundedList<C::Contradiction,8>& contradictions)
{
    // Callback-scoped references to immutable CPU bodies; do not copy 64 motion
    // descriptors (nearly 1 MiB) onto the caller's render thread stack.
    std::array<FactCandidate<const T*>,64> values{};
    std::array<const C::AcquisitionCandidate*,64> subjects{};std::size_t count=0;
    // An owner-published descriptor contradiction cannot be erased by a different
    // candidate's Known body. Ordinary missing optional descriptors remain local.
    for (const auto& item:candidates) if (const auto* unknown=(item.candidate->*member).UnknownPart())
        if (unknown->reason.code==C::UnknownReason::ContradictoryEvidence)
        {output=C::OptionalFact<C::MetadataRef<T>>::FromUnknown(*unknown);return true;}
    for (const auto& item:candidates)
    {
        const auto& fact=item.candidate->*member;
        if (const auto* body=ResolveOptional(fact,store))
        {
            subjects[count]=item.candidate;
            values[count++]={item.candidate->header.record,C::OptionalFact<const T*>::FromKnown(body,fact.KnownPart()->evidence),item.evidence};
        }
        else if (fact.IsKnown()) {output=Reject<C::MetadataRef<T>>("CTX.MissingValue");return true;}
    }
    if (!count) {output=ResolveMember(candidates,member).value;return true;}
    bool sameSubject=true;
    for (std::size_t i=1;i<count;++i)
    {
        const auto resource=[&](const C::AcquisitionCandidate& candidate)->const C::ResourceView* {
            if (!Established(candidate.payload)) return nullptr;
            const auto* reference=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value);
            return reference?ResolveMetadata(*reference,store):nullptr;
        };
        const auto* first=resource(*subjects[0]);const auto* other=resource(*subjects[i]);
        if (!first || !other || !SameResourceStructure(first->identity,other->identity) ||
            !SameContent(first->identity.contentRevision,other->identity.contentRevision) || !SemanticEqual(*first,*other)) sameSubject=false;
    }
    if constexpr (std::is_same_v<T,C::ColorDescription> || std::is_same_v<T,C::DepthDescription> ||
                  std::is_same_v<T,C::ExposureDescription>)
    {
        if (!count) {output=Reject<C::MetadataRef<T>>("CTX.MissingValue");return true;}
        T description;
        const bool success=std::apply([&](const auto&... field){return ([&]{
            using Fact=std::remove_cvref_t<decltype(description.*(field.pointer))>;
            using Value=typename Fact::value_type;
            std::array<FactCandidate<Value>,64> facts{};
            for (std::size_t i=0;i<count;++i)
                facts[i]={values[i].key,values[i].value.KnownPart()->value->*(field.pointer),values[i].evidence};
            if (!sameSubject)
            {
                // Without a proven shared subject, retain only facts established by
                // every possible source. Never manufacture a complete descriptor by
                // borrowing complementary members from unrelated resources.
                bool agreed=Established(facts[0].value);
                for (std::size_t i=1;i<count;++i)
                    if (!Established(facts[i].value) || !agreed ||
                        !SemanticEqual(facts[0].value.KnownPart()->value,facts[i].value.KnownPart()->value)) agreed=false;
                if (!agreed) {description.*(field.pointer)=Reject<Value>("CTX.IncompleteBundle");return true;}
            }
            const auto resolved=ResolveEvidence<Value>(std::span(facts.data(),count),[](const Value& a,const Value& b){return SemanticEqual(a,b);});
            description.*(field.pointer)=resolved.value;
            std::array<char,96> label{};
            if (name.size()+1+field.name.size()>label.size()) return false;
            std::copy(name.begin(),name.end(),label.begin());label[name.size()]='.';
            std::copy(field.name.begin(),field.name.end(),label.begin()+name.size()+1);
            return RecordContradiction(resolved,std::string_view(label.data(),name.size()+1+field.name.size()),contradictions);
        }() && ...);},T::Fields());
        if (!success) return false;
        const auto reference=store.Publish(description);
        if (!Lifecycle::ValidMetadataDescriptor(reference) || reference.owner!=C::OwnerDomain::Context) return false;
        output=C::OptionalFact<C::MetadataRef<T>>::FromKnown(reference,C::EvidenceRef{reference.record});return true;
    }
    else
    {
        // The motion frame-pair/raster/unit bundle must stay coherent as a whole.
        auto resolved=ResolveEvidence<const T*>(std::span(values.data(),count),[](const T* a,const T* b){return a && b && SemanticEqual(*a,*b);});
        if (!RecordContradiction(resolved,name,contradictions)) return false;
        if (!resolved.value.IsKnown()) {output=C::OptionalFact<C::MetadataRef<T>>::FromUnknown(*resolved.value.UnknownPart());return true;}
        const auto reference=store.Publish(*resolved.value.KnownPart()->value);
        if (!Lifecycle::ValidMetadataDescriptor(reference) || reference.owner!=C::OwnerDomain::Context) return false;
        output=C::OptionalFact<C::MetadataRef<T>>::FromKnown(reference,C::EvidenceRef{reference.record});return true;
    }
}
template<class T>
bool ResolveClaimField(std::span<const AdmittedCandidate> candidates,
                       std::span<const C::BoundedList<C::SemanticClaim,16>* const> lists,const C::Symbol& name,
                       C::OptionalFact<T> C::SemanticClaim::* member,C::OptionalFact<T>& output,
                       C::BoundedList<C::Contradiction,8>& contradictions)
{
    std::array<FactCandidate<T>,64> facts{};std::size_t count=0;
    for (std::size_t i=0;i<candidates.size();++i) if (lists[i])
        for (const auto& claim:*lists[i]) if (claim.field==name)
            facts[count++]={candidates[i].candidate->header.record,claim.*member,candidates[i].evidence};
    const auto resolved=ResolveEvidence<T>(std::span(facts.data(),count));
    output=resolved.value;return RecordContradiction(resolved,name.View(),contradictions);
}

template<class Store>
bool ResolveClaims(std::span<const AdmittedCandidate> candidates,Store& store,C::CanonicalFrameContext& context,
                   C::BoundedList<C::Contradiction,8>& contradictions,
                   const C::BoundedList<C::Symbol,16>* projection=nullptr)
{
    C::BoundedList<C::Symbol,16> names;
    std::array<const C::BoundedList<C::SemanticClaim,16>*,64> lists{};
    for (std::size_t i=0;i<candidates.size();++i)
    {
        if (!ResolveList(candidates[i].candidate->claims,store,lists[i])) return false;
        if (lists[i]) for (const auto& claim:*lists[i])
        {
            if(projection&&std::find(projection->begin(),projection->end(),claim.field)==projection->end())continue;
            bool exists=false;for (const auto& name:names) if (name==claim.field) exists=true;
            if (!exists && !names.Push(claim.field)) return false;
            std::size_t duplicates=0;for (const auto& other:*lists[i]) if (other.field==claim.field) ++duplicates;
            if (duplicates!=1) return false;
        }
    }
    std::sort(names.begin(),names.end(),[](const auto& a,const auto& b){return a.View()<b.View();});
    C::BoundedList<C::SemanticClaim,16> fields;
    for (const auto& name:names)
    {
        C::SemanticClaim claim;claim.field=name;
        const std::span<const C::BoundedList<C::SemanticClaim,16>* const> view(lists.data(),candidates.size());
        if (!ResolveClaimField(candidates,view,name,&C::SemanticClaim::raw,claim.raw,contradictions) ||
            !ResolveClaimField(candidates,view,name,&C::SemanticClaim::effective,claim.effective,contradictions) ||
            !ResolveClaimField(candidates,view,name,&C::SemanticClaim::overrideSource,claim.overrideSource,contradictions) ||
            !fields.Push(claim)) return false;
    }
    return PublishList(fields,store,context.fields);
}
template<class Store>
bool ResolveMasks(std::span<const AdmittedCandidate> candidates,Store& store,C::MetadataRef<C::MaskSet>& output,
                  C::BoundedList<C::Contradiction,8>& contradictions)
{
    C::MaskSet masks;
    const bool success=std::apply([&](const auto&... field){return ([&]{
        using Fact=std::remove_cvref_t<decltype(masks.*(field.pointer))>;
        using T=typename Fact::value_type;
        std::array<FactCandidate<T>,64> facts{};std::size_t count=0;
        for (const auto& item:candidates)
        {
            const auto* body=ResolveMetadata(item.candidate->masks,store);if (!body) return false;
            facts[count++]={item.candidate->header.record,body->*(field.pointer),item.evidence};
        }
        const auto resolved=ResolveEvidence<T>(std::span(facts.data(),count),[](const T& a,const T& b){return SemanticEqual(a,b);});
        masks.*(field.pointer)=resolved.value;return RecordContradiction(resolved,field.name,contradictions);
    }() && ...);},C::MaskSet::Fields());
    if (!success) return false;
    output=store.Publish(masks);
    return Lifecycle::ValidMetadataDescriptor(output) && output.owner==C::OwnerDomain::Context;
}
template<class T>
bool ResolveContextMember(std::span<const AdmittedCandidate> selected,C::OptionalFact<T> C::AcquisitionCandidate::* member,
                          std::string_view field,C::OptionalFact<T>& output,C::BoundedList<C::Contradiction,8>& contradictions)
{
    const auto resolved=ResolveMember(selected,member);output=resolved.value;
    return RecordContradiction(resolved,field,contradictions);
}
template<class Store>
bool ResolveContextJitter(std::span<const AdmittedCandidate> selected,const C::FrameIdentity& frame,
                          const C::RasterDescription& raster,Store& store,C::OptionalFact<C::Vec2>& output,
                          C::BoundedList<C::Contradiction,8>& contradictions)
{
    std::array<FactCandidate<C::Vec2>,64> facts{};std::size_t count=0;
    for (const auto& item:selected)
        facts[count++]={item.candidate->header.record,InterpretJitter(*item.candidate,frame,raster,store),item.evidence};
    const auto resolved=ResolveEvidence<C::Vec2>(std::span(facts.data(),count));output=resolved.value;
    return RecordContradiction(resolved,"jitter",contradictions);
}

template<class Store>
ContextResult BuildCanonicalFrameContext(const C::ObservationSet& observations,
                                          std::span<const C::AcquisitionCandidate> supplied,
                                          const ContextRequest& request,Store& store,
                                          const C::BoundedList<C::RecordKey,64>* selectedKeys=nullptr,
                                          const C::BoundedList<C::Symbol,16>* claimProjection=nullptr,
                                          std::span<const SourceQualification> qualifications={})
{
    ContextResult result;
    if (supplied.size()>64 || !ValidValues(observations) || !ValidValues(request.header) ||
        request.header.contract!=C::ContractId::C02 || request.header.owner!=C::OwnerDomain::Context ||
        !ValidValues(request.boundary)) return result;
    const auto* frame=ResolveMetadata(request.frame,store);
    if(!frame||!SameObservationBoundary(observations.boundary,request.boundary,*frame,store))return result;
    if (!frame || (Established(frame->episodeId) && Established(request.boundary.episode) && !SameFact(frame->episodeId,request.boundary.episode)))
    {result.refusal=Missing("CTX.AmbiguousAssociation",C::UnknownReason::AssociationUnproven);return result;}
    if (!ResolveMetadata(request.generations,store) || !ResolveMetadata(request.renderRaster,store) ||
        !ResolveMetadata(request.outputRaster,store) || !ResolveMetadata(request.continuity,store)) return result;
    const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* references=nullptr;
    if (!ResolveList(observations.candidates,store,references)) return result;
    std::array<AdmittedCandidate,64> admitted{};std::size_t count=0;
    if (references) for (const auto& reference:*references)
    {
        if (selectedKeys)
        {
            bool included=false;for (const auto& key:*selectedKeys) if (key==reference.record) included=true;
            if (!included) continue;
        }
        const auto reject=[&](std::string_view reason){result.rejected.Push({reference.record,Missing(reason)});};
        if (reference.recordType.View()!=C::AcquisitionCandidate::WireName) {reject("CTX.MalformedObservation");continue;}
        const C::AcquisitionCandidate* candidate=nullptr;
        bool ambiguous=false;
        for (const auto& value:supplied) if (value.header.record==reference.record)
        {
            if (candidate || value.header.revision!=reference.revision) ambiguous=true;
            candidate=&value;
        }
        if (ambiguous || !candidate || !ValidValues(*candidate)) {reject("CTX.MalformedObservation");continue;}
        const auto* candidateFrame=ResolveMetadata(candidate->frame,store);
        const auto* evidence=SourceEvidence(*candidate,store,qualifications);
        if (!candidateFrame || !evidence || !ResolveMetadata(candidate->masks,store)) {reject("CTX.MissingValue");continue;}
        // Observation context shares the established session/stream/view/episode. Exact
        // base-frame and age requirements belong to each consumer's C04 qualification.
        if (!SameObservationScope(*frame,*candidateFrame,store,false) ||
            !SameObservationBoundary(request.boundary,candidate->boundary,*frame,store) ||
            !SameObservationBoundary(request.boundary,candidate->boundary,*candidateFrame,store))
        {reject("CTX.IncompatibleScope");continue;}
        for (std::size_t i=0;i<count;++i) if (admitted[i].candidate->header.record==candidate->header.record) ambiguous=true;
        if (ambiguous) {reject("CTX.MalformedObservation");continue;}
        admitted[count++]={candidate,evidence};
    }
    std::sort(admitted.begin(),admitted.begin()+count,[](const auto& a,const auto& b){return KeyLess(a.candidate->header.record,b.candidate->header.record);});
    const std::span<const AdmittedCandidate> selected(admitted.data(),count);
    C::CanonicalFrameContext context;
    context.header=request.header;context.frame=request.frame;context.boundary=request.boundary;
    context.generations=request.generations;context.renderRaster=request.renderRaster;context.outputRaster=request.outputRaster;
    context.continuity=request.continuity;
    C::BoundedList<C::Contradiction,8> contradictions;
    const auto* renderRaster=ResolveMetadata(request.renderRaster,store);if (!renderRaster) return result;
    if (!ResolveContextJitter(selected,*frame,*renderRaster,store,context.jitter,contradictions) ||
        !ResolveContextMember(selected,&C::AcquisitionCandidate::rrActive,"rrActive",context.rrActive,contradictions) ||
        !ResolveContextMember(selected,&C::AcquisitionCandidate::fgActive,"fgActive",context.fgActive,contradictions)) return result;
    if (!ResolveDescription(selected,&C::AcquisitionCandidate::color,"color",store,context.color,contradictions) ||
        !ResolveDescription(selected,&C::AcquisitionCandidate::depth,"depth",store,context.depth,contradictions) ||
        !ResolveDescription(selected,&C::AcquisitionCandidate::motion,"motion",store,context.motion,contradictions) ||
        !ResolveDescription(selected,&C::AcquisitionCandidate::exposure,"exposure",store,context.exposure,contradictions)) return result;
    C::BoundedList<C::ContractRef<C::ContractId::C01>,64> coherent;
    C::BoundedList<C::AccessRequirements,16> access;
    C::BoundedList<C::GuideDescription,8> guides;
    for (const auto& item:selected)
    {
        C::Symbol type;type.Assign(C::AcquisitionCandidate::WireName);
        if (!coherent.Push({type,item.candidate->header.record,item.candidate->header.revision}) ||
            !access.Push(item.candidate->descriptiveLifetime)) return result;
        if (Established(item.candidate->payload))
            if (const auto* resourceRef=std::get_if<C::MetadataRef<C::ResourceView>>(&item.candidate->payload.KnownPart()->value))
            {
                const auto* resource=ResolveMetadata(*resourceRef,store);
                if (!resource) {result.rejected.Push({item.candidate->header.record,Missing("CTX.MissingValue")});continue;}
                C::GuideDescription guide;guide.kind=item.candidate->semantic;guide.raster=resource->raster;
                guide.evidence=*item.evidence;
                guide.candidate=C::OptionalFact<C::ContractRef<C::ContractId::C01>>::FromKnown(
                    {type,item.candidate->header.record,item.candidate->header.revision},item.candidate->payload.KnownPart()->evidence);
                // Source C01 carries the typed semantic body and original resource publication.
                guide.semanticDescription=C::OptionalFact<C::RecordReference>::FromKnown(
                    {C::ContractId::C01,type,item.candidate->header.record,item.candidate->header.revision},
                    item.candidate->payload.KnownPart()->evidence);
                if (!guides.Push(guide)) return result;
            }
    }
    if (!ResolveClaims(selected,store,context,contradictions,claimProjection) || !ResolveMasks(selected,store,context.masks,contradictions)) return result;
    if (!PublishList(coherent,store,context.coherentCandidates) || !PublishList(access,store,context.accessRequirements) ||
        !PublishList(guides,store,context.guides) || !PublishList(contradictions,store,context.contradictions) || !ValidValues(context)) return result;
    result.context.emplace(context);return result;
}
} // namespace Neurotic::Context
