#pragma once
#include "VulkanNrFrameContract.h"
#include "DlssNr_PresentResolution.h"
#include "DlssNr_PresentInputDecision.h"
#include <cmath>
#include <string>
namespace DlssNr
{
struct VkNrWorkloadStatus
{
    VkExtent2D requestedOutput{},requestedWork{},appliedOutput{},appliedWork{};
    PresentResolution::Policy policy;
    uint64_t requestedGeneration=0,appliedGeneration=0;
    bool pending=false;
    std::string reason;
};
struct VkNrWorkloadAdmission { VkNrFrameContract frame;PresentResolution::Policy policy;PresentResolution::Size size; };
inline bool SameVkNrWorkloadContext(const VkNrFrameContract& a,const VkNrFrameContract& b)
{
    return a.queue&&a.queue==b.queue&&a.deviceGeneration&&a.deviceGeneration==b.deviceGeneration&&
        a.route==b.route&&a.placement==b.placement&&a.routeEpoch==b.routeEpoch&&
        a.swapchainGeneration&&a.swapchainGeneration==b.swapchainGeneration&&
        a.resumeGeneration==b.resumeGeneration&&a.inputInterruptionEpoch==b.inputInterruptionEpoch&&a.guideGeneration&&a.guideGeneration==b.guideGeneration&&
        a.output.width==b.output.width&&a.output.height==b.output.height&&a.representation==b.representation;
}
inline PresentResolution::Size ResolveVkNrPresentWorkload(const PresentInputDecision::Decision& decision,
    const VkNrFrameContract& frame,std::optional<VkNrTemporalMetadata> metadata,
    std::optional<VkNrWorkloadAdmission> admitted,std::optional<VkExtent2D> observedRenderSize = {})
{
    const auto retained=admitted&&SameVkNrWorkloadContext(frame,admitted->frame)&&
        admitted->policy.mode==PresentResolution::FollowNative ? admitted->size : PresentResolution::Size{};
    return PresentInputDecision::ResolveWorkload(decision,frame.output.width,frame.output.height,
        observedRenderSize ? observedRenderSize->width : metadata ? metadata->color.width : 0,
        observedRenderSize ? observedRenderSize->height : metadata ? metadata->color.height : 0,retained).size;
}
template<class C> bool VkNrMatchesRenderAfterRr(const C& cfg,const VkNrFrameContract& frame)
{
    return frame.route==VkNrRoute::Native&&frame.placement==VkNrPlacement::AfterRR&&
        cfg.DlssNrRunBeforeSr.value_or_default()&&!cfg.DlssNrUiManualResolution.value_or_default()&&
        cfg.DlssNrWorkingScale.value_or_default()==1.0f;
}
template<class C> PresentResolution::Policy VkNrResolutionPolicy(const C& cfg,const VkNrFrameContract& frame)
{
    if(frame.route==VkNrRoute::Present)return PresentResolution::Selected(cfg);
    const float scale=cfg.DlssNrWorkingScale.value_or_default();
    // Match Native requests the render-input raster. RR must still run first,
    // so reduce only NR's private model raster instead of moving before RR.
    // Preserve explicit/legacy manual settings, including Manual at 100%.
    if(VkNrMatchesRenderAfterRr(cfg,frame))
        return {PresentResolution::FollowNative,0};
    if(!std::isfinite(scale))return {PresentResolution::Manual,0};
    return {PresentResolution::Manual,static_cast<uint32_t>(std::clamp(scale,0.25f,2.0f)*100.0f+0.5f)};
}
template<class C> PresentResolution::Size ResolveVkNrWorkload(const C& cfg,const VkNrFrameContract& frame,
    std::optional<VkNrTemporalMetadata> metadata,std::optional<VkNrWorkloadAdmission> admitted = {},
    std::optional<PresentResolution::Policy> effective = {})
{
    const auto policy=effective.value_or(VkNrResolutionPolicy(cfg,frame));
    if(policy.mode!=PresentResolution::FollowNative)
        return PresentResolution::Resolve(policy,frame.output.width,frame.output.height);
    if(metadata&&metadata->featureGeneration&&metadata->frameToken&&ValidVkNrTemporal(*metadata))
        return PresentResolution::Resolve(policy,frame.output.width,frame.output.height,
                                          metadata->color.width,metadata->color.height);
    if(frame.route==VkNrRoute::Present&&admitted&&SameVkNrWorkloadContext(frame,admitted->frame)&&
        admitted->policy.mode==policy.mode&&admitted->policy.scale==policy.scale&&
        !admitted->size.reason&&admitted->size.width>=8&&admitted->size.height>=8&&
        admitted->size.width<=frame.output.width&&admitted->size.height<=frame.output.height)
        return admitted->size;
    return {0,0,"Match native awaits fresh qualified render metadata in this Vulkan context"};
}
}
