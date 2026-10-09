#pragma once
#include "NativeNgxSchemaQualification.h"
#include "RepresentationCacheKeys.h"
#include <nr/contracts/C02_Context.h>

namespace Neurotic::Context
{
template<class Reader>const C::ResourceView* NativeJitterResource(const C::AcquisitionCandidate& candidate,
    const C::FrameIdentity& frame,const Reader& reader)
{
    const auto* sample=ResolveNativeSample(frame,reader);
    const auto* sourceFrame=ResolveMetadata(candidate.frame,reader);
    const auto* evidence=ResolveMetadata(candidate.evidence,reader);
    if(!sample||!sourceFrame||!evidence||candidate.semantic!=C::SemanticKind::Jitter||candidate.motion.IsKnown()||
       !SameObservationScope(frame,*sourceFrame,reader)||!NativeEvidenceFresh(*evidence,frame,reader)||
       !Established(evidence->provenance)||evidence->provenance.KnownPart()->value!=C::SourceClass::Native||
       candidate.header.scope!=C::ScopeRef{sample->callback}||!NativeNgxAdapterClaims(candidate,reader)||
       !Established(candidate.descriptiveLifetime.callbackScope)||
       candidate.descriptiveLifetime.callbackScope.KnownPart()->value!=sample->callback||!Established(candidate.payload))return nullptr;
    const auto* reference=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value);
    const auto* view=reference?ResolveMetadata(*reference,reader):nullptr;
    return view&&reference->owner==C::OwnerDomain::Resource&&CompleteResourceStructure(*view)?view:nullptr;
}

// Binding consumes C02's qualified association, not the source's basis string.
// Native pixel controls must reproduce the independently qualified render-UV
// jitter, and that exact guide must be retained in the coherent C01 closure.
template<class Reader>bool QualifiedNativePayloadJitter(const C::CanonicalFrameContext& context,
    double rawX,double rawY,const Reader& reader)
{
    const auto* frame=ResolveMetadata(context.frame,reader);
    const auto* raster=ResolveMetadata(context.renderRaster,reader);
    const C::BoundedList<C::GuideDescription,8>* guides=nullptr;
    const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* sources=nullptr;
    if(context.header.owner!=C::OwnerDomain::Context||!frame||!ResolveNativeSample(*frame,reader)||!raster||!ValidRaster(*raster)||
       !Established(context.jitter)||!Finite(context.jitter.KnownPart()->value)||!Finite(C::Vec2{rawX,rawY})||
       !context.guides.backing||context.guides.backing->owner!=C::OwnerDomain::Context||
       !context.coherentCandidates.backing||context.coherentCandidates.backing->owner!=C::OwnerDomain::Context||
       !ResolveList(context.guides,reader,guides)||!guides||
       !ResolveList(context.coherentCandidates,reader,sources)||!sources)return false;
    const auto active=raster->active.KnownPart()->value;
    if(raster->axisSigns.KnownPart()->value!=C::Vec2{1,1}||
       context.jitter.KnownPart()->value!=C::Vec2{rawX/active.width,rawY/active.height})return false;
    unsigned matched=0;
    for(const auto& guide:*guides)if(guide.kind==C::SemanticKind::Jitter)
    {
        if(!SemanticEqual(guide.raster,*raster)||!NativeEvidenceFresh(guide.evidence,*frame,reader)||
           !Established(guide.evidence.provenance)||guide.evidence.provenance.KnownPart()->value!=C::SourceClass::Native||
           !Established(guide.evidence.semanticCertainty)||guide.evidence.semanticCertainty.KnownPart()->value!=C::SemanticCertainty::Qualified||
           !Established(guide.candidate)||!Established(guide.semanticDescription))return false;
        const auto& source=guide.candidate.KnownPart()->value;const auto& meaning=guide.semanticDescription.KnownPart()->value;
        if(source.recordType.View()!=C::AcquisitionCandidate::WireName||meaning.contract!=C::ContractId::C01||
           meaning.recordType!=source.recordType||meaning.record!=source.record||meaning.revision!=source.revision)return false;
        unsigned retained=0;for(const auto& item:*sources)if(item==source)++retained;
        if(retained!=1)return false;++matched;
    }
    return matched==1;
}
}
