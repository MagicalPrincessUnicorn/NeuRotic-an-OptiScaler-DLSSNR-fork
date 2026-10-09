#pragma once
#include "RepresentationCacheKeys.h"
#include <nr/contracts/NativeStageDelivery.h>

namespace Neurotic::Context
{
template<class Reader> const C::ResourceView* ResolveNativeOutput(const C::NativeOutputContentV1& content,const Reader& reader)
{
    if(!ValidValues(content))return nullptr;
    const auto* view=ResolveMetadata(content.view,reader);
    return view&&CompleteContent(*view)?view:nullptr;
}
template<class Reader> bool SameNativeOutputTarget(const C::NativeOutputContentV1& a,const C::NativeOutputContentV1& b,const Reader& reader)
{
    const auto* left=ResolveNativeOutput(a,reader);const auto* right=ResolveNativeOutput(b,reader);
    return left&&right&&a.semanticDomain==b.semanticDomain&&SameResourceStructure(left->identity,right->identity)&&
        SameFact(left->device,right->device)&&SameFact(left->adapter,right->adapter)&&
        SameFact(left->mip,right->mip)&&SameFact(left->arrayLayer,right->arrayLayer)&&SameFact(left->plane,right->plane)&&
        SemanticEqual(left->descriptor,right->descriptor)&&SemanticEqual(left->raster,right->raster);
}
template<class Reader> bool SameNativeOutputContent(const C::NativeOutputContentV1& a,const C::NativeOutputContentV1& b,const Reader& reader)
{
    const auto* left=ResolveNativeOutput(a,reader);const auto* right=ResolveNativeOutput(b,reader);
    return left&&right&&SameNativeOutputTarget(a,b,reader)&&SameContent(left->identity.contentRevision,right->identity.contentRevision)&&
        a.recording==b.recording&&a.producerOrdinal==b.producerOrdinal;
}
}
