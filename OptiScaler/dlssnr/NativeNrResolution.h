#pragma once
#include "DlssNr_PresentResolution.h"

namespace DlssNr::NativeResolution
{
// Saved NativeResolution values are independent of legacy placement/scale keys.
// Unknown values remain saved and refuse; reading this selection never normalizes.
inline constexpr unsigned LegacySelection=0, PostNativeSelection=1;
template<class C> unsigned Selection(const C& cfg)
{
    if constexpr(requires {cfg.DlssNrNativeResolution.value_or_default();})
        return cfg.DlssNrNativeResolution.value_or_default();
    else return LegacySelection;
}
template<class C> void LoadSelection(C& cfg,std::optional<uint32_t> value)
{ cfg.DlssNrNativeResolution.set_from_config(value); }
template<class C> bool PostSelected(const C& cfg)
{ return cfg.DlssNrRoute.value_or_default()==0 && Selection(cfg)==PostNativeSelection; }
template<class C> void SelectPost(C& cfg,bool post)
{
    if constexpr(requires {cfg.DlssNrNativeResolution=0u;})
        cfg.DlssNrNativeResolution=post?PostNativeSelection:LegacySelection;
}
template<class C> const char* SelectionRefusal(const C& cfg)
{
    if(cfg.DlssNrRoute.value_or_default()!=0)return nullptr;
    if(Selection(cfg)>PostNativeSelection)return "Unknown Native resolution preference";
    if(PostSelected(cfg)&&(cfg.DlssNrRunBeforeSr.value_or_default()||
       cfg.DlssNrUiManualResolution.value_or_default()||cfg.DlssNrWorkingScale.value_or_default()!=1.0f))
        return "Match Native Post requires After placement and automatic resolution";
    return nullptr;
}
// Match Native describes NR's private raster. RR still owns its original inputs
// and must complete before this path; a pre-SR scratch dispatch is a different path.
template<class C> bool MatchAfterReconstruction(const C& cfg, bool forcedAfter, bool preSrScratch)
{
    return !preSrScratch && cfg.DlssNrRoute.value_or_default() == 0 &&
        ((PostSelected(cfg)&&!cfg.DlssNrRunBeforeSr.value_or_default()) ||
         (forcedAfter&&cfg.DlssNrRunBeforeSr.value_or_default())) &&
        !cfg.DlssNrUiManualResolution.value_or_default() &&
        cfg.DlssNrWorkingScale.value_or_default() == 1.0f;
}
inline PresentResolution::Size MatchWorkSize(unsigned renderWidth, unsigned renderHeight,
                                            unsigned outputWidth, unsigned outputHeight)
{
    // Only current evaluation subrect dimensions are accepted. Never substitute
    // an old frame, a texture allocation capacity, or an assumed percentage.
    if (renderWidth < 8 || renderHeight < 8 || renderWidth > outputWidth || renderHeight > outputHeight)
        return {0, 0, "Match Native awaits valid current render-input dimensions"};
    return {renderWidth & ~7u, renderHeight & ~7u, nullptr};
}
// The existing After resampler can also minify a model raster larger than SR output.
// Keep the established resource limit, not the legacy RR helper's output-size cap.
inline PresentResolution::Size MatchPostWorkSize(unsigned renderWidth,unsigned renderHeight,
    unsigned outputWidth,unsigned outputHeight)
{
    if(renderWidth<8||renderHeight<8||renderWidth>8192||renderHeight>8192||
       outputWidth<8||outputHeight<8||outputWidth>8192||outputHeight>8192)
        return {0,0,"Match Native Post dimensions exceed the supported 8-to-8192-pixel range"};
    return {renderWidth&~7u,renderHeight&~7u,nullptr};
}
// Actual prepared-resource capability, shared by early typed admission and dispatch.
// Equal observed raster remains valid; a new mode identity alone never refuses it.
inline const char* PreparedRasterRefusal(PresentResolution::Size requested,
    uint64_t preparedWidth,unsigned preparedHeight)
{
    if(requested.reason)return requested.reason;
    if(requested.width!=preparedWidth||requested.height!=preparedHeight)
        return "Typed Native currently requires full-size model resources, as with Manual at 100%";
    return nullptr;
}
// Created before one synchronous SR evaluation, consumed immediately after its
// success, never stored in a frame cache. Target identity rejects redirection.
struct RenderObservation {
    unsigned width=0,height=0;
    uintptr_t outputIdentity=0;
    bool current=false;
};
inline RenderObservation ObserveRender(unsigned width,unsigned height,bool supplied,
    uint64_t colorWidth,unsigned colorHeight,unsigned x,unsigned y,uintptr_t outputIdentity,
    unsigned outputX,unsigned outputY)
{
    return {width,height,outputIdentity,supplied&&outputIdentity&&width>=8&&height>=8&&
        x<=colorWidth&&y<=colorHeight&&width<=colorWidth-x&&height<=colorHeight-y&&
        outputX==0&&outputY==0};
}
inline PresentResolution::Size MatchObservedSize(const RenderObservation* observed,
    uintptr_t outputIdentity,unsigned outputWidth,unsigned outputHeight)
{
    if(!observed||!observed->current||observed->outputIdentity!=outputIdentity)
        return {0,0,"Match Native Post awaits current pre-SR render dimensions"};
    return MatchPostWorkSize(observed->width,observed->height,outputWidth,outputHeight);
}
inline void PublishWorkSize(PresentResolution::Size work, const char*& refusal, bool& resetPending)
{
    refusal = work.reason ? work.reason : "";
    // Recovery needs a real evaluation before it can be reported as running.
    // A temporary metadata gap never permanently fails the model.
    if (work.reason) resetPending = true;
}
}
