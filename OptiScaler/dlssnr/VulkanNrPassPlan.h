#pragma once
#include "VulkanNrGeneration.h"
#include "VulkanNrSession.h"
#include "VulkanNrResolution.h"
#include "DlssNr_Multipass.h"
namespace DlssNr {
// Same committed flagship mapping: layer 1, layer 2, then eight independent layers.
// The argument is already an owned, derived render snapshot.
template<class C> C VkNrPassSettingsFor(const C& cfg,uint32_t pass) {
    auto result=cfg;
    if(pass==1) {
        result.DlssNrWorkingScale=cfg.DlssNrSecondLayerWorkingScale.value_or_default();
        result.DlssNrScalingDownscaler=cfg.DlssNrSecondLayerScalingDownscaler.value_or_default();
        result.DlssNrTransfer=cfg.DlssNrSecondLayerTransfer.value_or_default();
        result.DlssNrPreset=cfg.DlssNrSecondLayerPreset.value_or_default();
        result.DlssNrIntensity=cfg.DlssNrSecondLayerIntensity.value_or_default();
        result.DlssNrStyle=cfg.DlssNrSecondLayerStyle.value_or_default();
        result.DlssNrLocalStructure=cfg.DlssNrSecondLayerLocalStructure.value_or_default();
        result.DlssNrLocalTone=cfg.DlssNrSecondLayerLocalTone.value_or_default();
        result.DlssNrSkinStructure=cfg.DlssNrSecondLayerSkinStructure.value_or_default();
        result.DlssNrAutoMask=cfg.DlssNrSecondLayerAutoMask.value_or_default();
        result.DlssNrTransferStrength=cfg.DlssNrSecondLayerTransferStrength.value_or_default();
        result.DlssNrColourStrength=cfg.DlssNrSecondLayerColourStrength.value_or_default();
        result.DlssNrMaxRatio=cfg.DlssNrSecondLayerMaxRatio.value_or_default();
        result.DlssNrReversibleMode=cfg.DlssNrSecondLayerReversibleMode.value_or_default();
        result.DlssNrApplyModel=cfg.DlssNrSecondLayerApplyModel.value_or_default();
    } else if(pass>=2&&pass<10) {
        const auto& layer=cfg.DlssNrExtraLayers[pass-2];
        result.DlssNrWorkingScale=layer.workingScale.value_or_default();
        result.DlssNrScalingDownscaler=layer.scalingDownscaler.value_or_default();
        result.DlssNrTransfer=layer.transfer.value_or_default();
        result.DlssNrPreset=layer.preset.value_or_default();
        result.DlssNrIntensity=layer.intensity.value_or_default();
        result.DlssNrStyle=layer.style.value_or_default();
        result.DlssNrLocalStructure=layer.localStructure.value_or_default();
        result.DlssNrLocalTone=layer.localTone.value_or_default();
        result.DlssNrSkinStructure=layer.skinStructure.value_or_default();
        result.DlssNrAutoMask=layer.autoMask.value_or_default();
        result.DlssNrTransferStrength=layer.transferStrength.value_or_default();
        result.DlssNrColourStrength=layer.colourStrength.value_or_default();
        result.DlssNrMaxRatio=layer.maxRatio.value_or_default();
        result.DlssNrReversibleMode=layer.reversibleMode.value_or_default();
        result.DlssNrApplyModel=layer.applyModel.value_or_default();
    }
    if(!cfg.DlssNrApplyModel.value_or_default())result.DlssNrApplyModel=false;
    return result;
}
struct VkNrPassPlan { std::vector<VkNrGenerationKey> passes;bool requireComplete=false;std::string reason; };
struct VkNrChainResult { uint32_t requested=0,completed=0;VkRecordResult recording;uint64_t outputVersion=0;bool deliverable=false; };
template<class C> VkNrPassPlan BuildVkNrPassPlan(const C& cfg,const VkNrFrameContract& frame) {
    VkNrPassPlan plan;
    plan.requireComplete=frame.route==VkNrRoute::Present||frame.placement==VkNrPlacement::BeforeSR;
    const auto count=Multipass::RequestedCount(cfg);
    for(uint32_t i=0;i<count;++i) {
        const auto pass=VkNrPassSettingsFor(cfg,i);
        VkNrGenerationKey key;key.frame=frame;key.passIndex=i;
        const auto scale=pass.DlssNrWorkingScale.value_or_default();
        if(!std::isfinite(scale)) {plan.passes.clear();plan.reason="Vulkan pass scale is not finite";return plan;}
        const auto size=i>0&&BasicMultipass::Active(cfg) ?
            PresentResolution::Size{plan.passes[0].frame.work.width,plan.passes[0].frame.work.height} :
            i==0&&(frame.route==VkNrRoute::Present||VkNrMatchesRenderAfterRr(cfg,frame)) ?
            PresentResolution::Size{frame.work.width,frame.work.height} :
            PresentResolution::Resolve({PresentResolution::Manual,static_cast<uint32_t>(
                std::clamp(scale,0.25f,2.0f)*100.0f+0.5f)},
                frame.output.width,frame.output.height);
        if(size.reason||!size.width||!size.height) {plan.passes.clear();plan.reason="Vulkan pass working raster unavailable";return plan;}
        key.frame.work={size.width,size.height};
        key.tuning={pass.DlssNrPreset.value_or_default(),pass.DlssNrStyle.value_or_default(),
            pass.DlssNrIntensity.value_or_default(),pass.DlssNrLocalStructure.value_or_default(),
            pass.DlssNrLocalTone.value_or_default(),pass.DlssNrSkinStructure.value_or_default(),
            pass.DlssNrAutoMask.value_or_default()};
        key.upFilter=key.downFilter=static_cast<uint32_t>(pass.DlssNrScalingDownscaler.value_or_default());
        if(!key.tuning.Valid()) {plan.passes.clear();plan.reason="Vulkan pass tuning is invalid";return plan;}
        plan.passes.push_back(key);
    }
    return plan;
}
}
