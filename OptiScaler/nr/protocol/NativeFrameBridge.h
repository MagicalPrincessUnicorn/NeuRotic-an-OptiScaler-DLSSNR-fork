#pragma once
#include "NativeTemporalBindingMapper.h"
#ifdef _MSC_VER
#pragma warning(push)
#pragma warning(disable:4324) // Existing shader constants intentionally have 256-byte alignment.
#endif
#include <shaders/dlssnr/DlssNr_Common.h>
#ifdef _MSC_VER
#pragma warning(pop)
#endif

namespace Neurotic::Protocol
{
// Pure projection of already-qualified current values. No raw parameter reads,
// prior-frame state or resource discovery. Exposure is resolved separately by
// the current C03 owner and starts explicitly null on every invocation.
template<class Reader>std::optional<DlssNrFrameInfo> BuildNativeFrameInfo(const NativeBindingMap& map,
    const RecipeProduct& product,const Reader& reader)
{
    if(map.recipe!=product.recipe.header.record||map.evaluation!=product.recipe.evaluation||map.placement!=product.recipe.placement)return {};
    const auto* context=S::ResolveMetadata(product.inputContext,reader);
    const auto* color=context?S::ResolveOptional(context->color,reader):nullptr;
    const auto* raster=context?S::ResolveMetadata(context->renderRaster,reader):nullptr;
    if(!color||!S::Established(color->domain)||color->domain.KnownPart()->value==C::ColorDomain::Control||
       !raster||!S::Established(raster->active))return {};
    DlssNrFrameInfo frame{};
    frame.ColourIsLinearHdr=color->domain.KnownPart()->value!=C::ColorDomain::EncodedDisplay;
    // The same qualified domain must select the C12 shader branch.
    if(frame.ColourIsLinearHdr&&S::Established(color->transfer)&&color->transfer.KnownPart()->value.View()!="Linear")return {};
    frame.RenderSubrectWidth=raster->active.KnownPart()->value.width;
    frame.RenderSubrectHeight=raster->active.KnownPart()->value.height;
    if(!frame.RenderSubrectWidth||!frame.RenderSubrectHeight)return {};
    const auto command=[&](std::string_view key)->const LegacyCommand*{
        const LegacyCommand* found=nullptr;for(const auto& c:map.evaluate)if(c.key.View()==key){if(found)return nullptr;found=&c;}return found;};
    const auto number=[&](std::string_view key,float& value){
        const auto* c=command(key);if(!c||c->kind!=CommandKind::Float||!std::isfinite(c->number))return false;value=c->number;return true;};
    const auto integer=[&](std::string_view key,unsigned int& value){
        const auto* c=command(key);if(!c||c->kind!=CommandKind::UInt)return false;value=c->integer;return true;};
    unsigned int reset=0,inverted=0;
    if(!integer("DLSSNR.Reset",reset)||reset>1||!integer("DLSSNR.DepthInverted",inverted)||inverted>1||
       !number("DLSSNR.MVecScaleX",frame.MvScaleX)||!number("DLSSNR.MVecScaleY",frame.MvScaleY)||
       !number("Jitter.Offset.X",frame.JitterX)||!number("Jitter.Offset.Y",frame.JitterY)||
       !integer("DLSSNR.DepthSubrectBaseX",frame.DepthSubrectX)||!integer("DLSSNR.DepthSubrectBaseY",frame.DepthSubrectY)||
       !integer("DLSSNR.DepthSubrectWidth",frame.DepthSubrectWidth)||!integer("DLSSNR.DepthSubrectHeight",frame.DepthSubrectHeight)||
       !integer("DLSSNR.MVecSubrectBaseX",frame.MotionSubrectX)||!integer("DLSSNR.MVecSubrectBaseY",frame.MotionSubrectY)||
       !integer("DLSSNR.MVecSubrectWidth",frame.MotionSubrectWidth)||!integer("DLSSNR.MVecSubrectHeight",frame.MotionSubrectHeight)||
       !frame.DepthSubrectWidth||!frame.DepthSubrectHeight||!frame.MotionSubrectWidth||!frame.MotionSubrectHeight||
       !std::isfinite(map.preExposure)||map.preExposure<=0)return {};
    frame.Reset=reset!=0;frame.DepthInverted=inverted!=0;frame.PreExposure=map.preExposure;
    return frame;
}
}
