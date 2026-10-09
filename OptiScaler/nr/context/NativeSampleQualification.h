#pragma once
#include "ContextMetadata.h"

namespace Neurotic::Context
{
template<class Reader> const C::NativeSampleIdentityV1* ResolveNativeSample(const C::FrameIdentity& frame,const Reader& reader)
{
    if(!frame.nativeSample||frame.nativeSample->owner!=C::OwnerDomain::Provider||
        frame.nativeSample->schemaVersion!=C::SchemaVersion{1,0})return nullptr;
    const auto* sample=ResolveMetadata(*frame.nativeSample,reader);
    if(!sample||!Established(frame.sessionId)||!Established(frame.renderStreamId)||!Established(frame.viewId)||
        frame.sessionId.KnownPart()->value!=sample->session||frame.renderStreamId.KnownPart()->value!=sample->stream||
        frame.viewId.KnownPart()->value!=sample->view)return nullptr;
    return sample;
}
// Only callers explicitly qualifying Native observations use this domain.
// The generic final-frame SameScope/FramePair validators remain unchanged.
template<class Reader> bool SameObservationScope(const C::FrameIdentity& a,const C::FrameIdentity& b,const Reader& reader,
                                                 bool requireBaseFrame=true)
{
    if(!a.nativeSample&&!b.nativeSample)return SameScope(a,b,requireBaseFrame);
    const auto* left=ResolveNativeSample(a,reader);const auto* right=ResolveNativeSample(b,reader);
    return left&&right&&a.nativeSample==b.nativeSample&&left->SameSample(*right)&&left->callback==right->callback;
}
inline C::NativeSampleFreshnessV1 NativeFreshness(const C::MetadataRef<C::NativeSampleIdentityV1>& reference,
                                                const C::NativeSampleIdentityV1& sample)
{
    C::NativeSampleFreshnessV1 result;
    result.sample.owner=reference.owner;result.sample.recordType.Assign(C::NativeSampleIdentityV1::WireName);
    result.sample.schemaVersion={reference.schemaVersion.major,reference.schemaVersion.minor};
    result.sample.record=reference.record;result.sample.revision=reference.revision;result.callback=sample.callback;
    return result;
}
// Raw NGX guides keep their native units and scales. This establishes their
// current source sample, never a canonical UV conversion or a real-frame pair.
template<class Reader> bool NativeMotionMatches(const C::MotionDescription& motion,const C::FrameIdentity& frame,const Reader& reader)
{
    return ResolveNativeSample(frame,reader)&&ValidValues(motion)&&motion.nativeSample==frame.nativeSample&&
        !motion.framePair.current.IsKnown()&&!motion.framePair.previous.IsKnown()&&
        Established(motion.units)&&Established(motion.direction)&&Established(motion.jitterConvention)&&
        motion.direction.KnownPart()->value==C::MotionDirection::CurrentToPrevious&&
        ValidRaster(motion.currentRaster)&&ValidRaster(motion.previousRaster);
}
template<class Reader> bool NativeEvidenceFresh(const C::EvidenceVector& evidence,const C::FrameIdentity& frame,const Reader& reader)
{
    const auto* sample=ResolveNativeSample(frame,reader);
    return sample&&evidence.nativeFreshness&&!evidence.ageInRealFrames.IsKnown()&&ValidValues(*evidence.nativeFreshness)&&
        *evidence.nativeFreshness==NativeFreshness(*frame.nativeSample,*sample);
}
}
