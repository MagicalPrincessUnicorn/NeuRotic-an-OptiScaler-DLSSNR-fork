#pragma once
#include "NativeResourceRegistry.h"
#include "NativeControlledSceneProducer.h"
#include <dlssnr/NativeNgxCallCapture.h>
#include <dlssnr/NativeNgxCreationParameters.h>

namespace Neurotic::Lifecycle
{
// API-supported source claims, before C01 publication. NFC must still qualify
// every declaration. These coordinate grids assert no previous image identity.
struct NativeNgxSourceDeclarations
{
    std::array<const C::ResourceView*,DlssNr::NativeNgxCallCapture::ResourceNames().size()> views{};
    C::ColorDescription color;
    C::DepthDescription depth;
    C::MotionDescription motion;
    std::array<std::optional<C::ColorDescription>,2> producerColors;
};
inline std::optional<NativeNgxSourceDeclarations> DeclareNativeNgxSource(
    const DlssNr::NativeNgxCreationParameters& creation,const DlssNr::NativeNgxCallCapture& call,
    NativeResourceRegistry& resources,
    const std::array<const C::ResourceView*,DlssNr::NativeNgxCallCapture::ResourceNames().size()>& physical,
    const C::MetadataRef<C::NativeSampleIdentityV1>& sample,const C::EvidenceRef& evidence,
    const NativeProducerColorBinding* producer=nullptr,const C::NativeSampleIdentityV1* identity=nullptr)
{
    // Named NGX flag bits from the checked-in SDK. Reserved/future bits do not
    // acquire semantics merely because the current bit subset looks familiar.
    constexpr unsigned hdr=1u,lowResolutionMotion=2u,jitteredMotion=4u,invertedDepth=8u,knownFlags=0xefu;
    const auto flags=creation.Value("DLSS.Feature.Create.Flags"),width=creation.Value("Width"),height=creation.Value("Height");
    const auto outWidth=creation.Value("OutWidth"),outHeight=creation.Value("OutHeight");
    if(!creation.Captured()||!flags||(*flags&~knownFlags)||!width||!height||!outWidth||!outHeight||
       !*width||!*height||!*outWidth||!*outHeight||!Lifecycle::ValidEvidence(evidence))return {};
    unsigned renderWidth=0,renderHeight=0,cx=0,cy=0,dx=0,dy=0,mx=0,my=0;
    if(call.Get("DLSS.Render.Subrect.Dimensions.Width",&renderWidth)!=0||
       call.Get("DLSS.Render.Subrect.Dimensions.Height",&renderHeight)!=0||!renderWidth||!renderHeight||
       renderWidth>*width||renderHeight>*height||
       call.Get("DLSS.Input.Color.Subrect.Base.X",&cx)!=0||call.Get("DLSS.Input.Color.Subrect.Base.Y",&cy)!=0||
       call.Get("DLSS.Input.Depth.Subrect.Base.X",&dx)!=0||call.Get("DLSS.Input.Depth.Subrect.Base.Y",&dy)!=0||
       call.Get("DLSS.Input.MV.Subrect.Base.X",&mx)!=0||call.Get("DLSS.Input.MV.Subrect.Base.Y",&my)!=0)return {};
    const auto* depth=call.Description("Depth");const auto* motion=call.Description("MotionVectors");
    if(!depth||!motion||(depth->Format!=DXGI_FORMAT_R32_FLOAT&&depth->Format!=DXGI_FORMAT_R16_FLOAT)||
       (motion->Format!=DXGI_FORMAT_R16G16_FLOAT&&motion->Format!=DXGI_FORMAT_R32G32_FLOAT))return {};
    const bool low=(*flags&lowResolutionMotion)!=0;
    const std::array<C::Rectangle,4> rectangles={C::Rectangle{cx,cy,renderWidth,renderHeight},
        C::Rectangle{0,0,*outWidth,*outHeight},C::Rectangle{dx,dy,renderWidth,renderHeight},
        C::Rectangle{mx,my,low?renderWidth:*outWidth,low?renderHeight:*outHeight}};
    for(std::size_t i=0;i<rectangles.size();++i)
    {
        if(!physical[i]||!Context::CompleteResourceStructure(*physical[i]))return {};
        const auto size=physical[i]->descriptor.allocation.KnownPart()->value;const auto r=rectangles[i];
        if(r.x>size.width||r.y>size.height||r.width>size.width-r.x||r.height>size.height-r.y)return {};
        // This slice owns only the full original caller output. No unobserved
        // output subrect offset is inferred from allocation slack.
        if(i==1&&(size.width!=*outWidth||size.height!=*outHeight))return {};
    }
    NativeNgxSourceDeclarations result;result.views=physical;
    for(std::size_t i=0;i<rectangles.size();++i)
    {result.views[i]=resources.ProjectRectangle(physical[i],rectangles[i]);if(!result.views[i])return {};}
    const auto known=[&]<class T>(const T& value){return C::OptionalFact<T>::FromKnown(value,evidence);};
    if(!(*flags&hdr))result.color.domain=known(C::ColorDomain::EncodedDisplay);
    if(producer&&identity)
        for(unsigned i=0;i<2;++i)result.producerColors[i]=producer->ColorFor(*identity,call,*result.views[i],i==1,evidence);
    result.depth.kind=known(C::DepthKind::Device);result.depth.reversed=known(bool(*flags&invertedDepth));
    result.motion.units=known(C::MotionUnits::Pixels);result.motion.direction=known(C::MotionDirection::CurrentToPrevious);
    result.motion.jitterConvention=known((*flags&jitteredMotion)?C::JitterConvention::Jittered:C::JitterConvention::Unjittered);
    float sx=0,sy=0;
    if(call.Get("MV.Scale.X",&sx)==0&&call.Get("MV.Scale.Y",&sy)==0)result.motion.scale=known(C::Vec2{sx,sy});
    // Raw jitter remains in its own C01 pixel declaration. NFC owns its
    // normalization; neither current nor previous canonical jitter is asserted.
    result.motion.nativeSample=sample;result.motion.currentRaster=result.views[3]->raster;
    // NGX defines both vector endpoints in this declared displacement grid.
    // A coordinate basis does not establish a previous sample/resource/frame.
    result.motion.previousRaster=result.motion.currentRaster;
    return result;
}
}
