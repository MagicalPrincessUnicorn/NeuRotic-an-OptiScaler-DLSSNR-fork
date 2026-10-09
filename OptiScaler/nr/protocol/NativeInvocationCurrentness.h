#pragma once
#include "RecipeTypes.h"
#include <nr/context/NativeSampleQualification.h>
#include <nr/context/RepresentationCacheKeys.h>

namespace Neurotic::Protocol
{
// A same-callback C01 publication is current only for the exact direct game
// read it describes. It is not a physical write revision or a use grant.
template<class Reader> bool CurrentNativePublication(const RepresentationUse& use,
    const C::CanonicalFrameContext& context,const C::FrameIdentity& frame,const Reader& reader,
    C::Placement placement=C::Placement::NativeBefore)
{
    namespace S=Context;
    if(!frame.nativeSample||!use.sourceCandidate||use.nativeSample!=frame.nativeSample||
       use.currentSource.owner!=C::OwnerDomain::Resource||
       use.sourceCandidate->recordType.View()!=C::AcquisitionCandidate::WireName)return false;
    if constexpr(!requires {reader.ResolveCandidate(*use.sourceCandidate);})return false;
    else
    {
        const auto* candidate=reader.ResolveCandidate(*use.sourceCandidate);
        if(!candidate||!S::ValidValues(*candidate)||
           candidate->header.record!=use.sourceCandidate->record||
           candidate->header.revision!=use.sourceCandidate->revision||
           candidate->header.contract!=C::ContractId::C01||
           !S::Established(candidate->providerIncarnation)||
           !S::Established(candidate->payload))return false;
        const auto* payload=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate->payload.KnownPart()->value);
        const auto* source=payload?S::ResolveMetadata(*payload,reader):nullptr;
        const auto* sourceFrame=S::ResolveMetadata(candidate->frame,reader);
        const auto* evidence=S::ResolveMetadata(candidate->evidence,reader);
        if(!payload||*payload!=use.currentSource||!source||!S::CompleteResourceStructure(*source)||
           !sourceFrame||!S::SameObservationScope(frame,*sourceFrame,reader)||
           !evidence||!S::NativeEvidenceFresh(*evidence,frame,reader)||
           !S::Established(candidate->descriptiveLifetime.callbackScope)||
           candidate->descriptiveLifetime.callbackScope.KnownPart()->value!=evidence->nativeFreshness->callback)return false;
        const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* coherent=nullptr;
        if(!S::ResolveList(context.coherentCandidates,reader,coherent)||!coherent)return false;
        bool member=false;for(const auto& ref:*coherent)if(ref==*use.sourceCandidate)member=true;
        if(!member)return false;
        const auto colorRole=placement==C::Placement::NativeAfter?"Output":"Color";
        const auto role=use.purpose==Purpose::ColorModelInput?colorRole:
            use.purpose==Purpose::OutputModelTarget?colorRole:
            use.purpose==Purpose::DepthGuide?"Depth":
            use.purpose==Purpose::MotionVectorGuide?"MotionVectors":"";
        const auto kind=use.purpose==Purpose::ColorModelInput||use.purpose==Purpose::OutputModelTarget?C::SemanticKind::Color:
            use.purpose==Purpose::DepthGuide?C::SemanticKind::Depth:C::SemanticKind::Motion;
        const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
        if(!*role||candidate->semantic!=kind||!S::ResolveList(candidate->claims,reader,claims)||
           !claims||claims->Size()==0||claims->Get(0)->field.View()!=role)return false;
        return true;
    }
}
// The current callback must carry the private source bootstrap's selected SR
// publication, not merely a resource with some known historical revision.
template<class Uses,class Reader> bool CurrentSelectedSrOutput(const Uses& uses,
    const C::CanonicalFrameContext& context,const C::FrameIdentity& frame,const Reader& reader)
{
    const auto* sample=Context::ResolveNativeSample(frame,reader);
    if(!sample)return false;
    if constexpr(!requires(const C::ResourceView& view){reader.SelectedSrOutput(*sample,view);})return false;
    else
    {
        unsigned colors=0,targets=0;
        for(const auto& use:uses)
            if(use.purpose==Purpose::ColorModelInput||use.purpose==Purpose::OutputModelTarget)
            {
                const auto* view=Context::ResolveMetadata(use.currentSource,reader);
                if(!view||!reader.SelectedSrOutput(*sample,*view)||
                   !CurrentNativePublication(use,context,frame,reader,C::Placement::NativeAfter))return false;
                if(use.purpose==Purpose::ColorModelInput)++colors;else ++targets;
            }
        return colors==1&&targets==1;
    }
}
}
