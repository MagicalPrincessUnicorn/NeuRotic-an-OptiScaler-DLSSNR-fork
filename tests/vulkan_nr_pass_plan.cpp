#include "../OptiScaler/dlssnr/VulkanNrPassPlan.h"
#include "fixtures/vulkan_nr_pass_settings.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
    VkPassTestSettings cfg;VkNrFrameContract frame;frame.output=frame.work={3840,2160};
    frame.representation.format=VK_FORMAT_R16G16B16A16_SFLOAT;
    cfg.DlssNrBasicMultipass.value.advanced=true;
    Check(BuildVkNrPassPlan(cfg,frame).passes.size()==1,"off preserves established one pass");
    frame.placement=VkNrPlacement::AfterRR;frame.work={1280,720};cfg.DlssNrRunBeforeSr=true;
    auto rrMatch=BuildVkNrPassPlan(cfg,frame);
    Check(rrMatch.passes[0].frame.work.width==1280&&rrMatch.passes[0].frame.work.height==720,
        "Native RR pass zero preserves resolved Match Native model raster");
    cfg.DlssNrRunBeforeSr=false;frame.placement=VkNrPlacement::AfterSR;frame.work=frame.output;
    cfg.DlssNrMultipassEnabled=true;
    for(auto count:{1u,2u,4u,10u}){
        cfg.DlssNrPasses=count;auto plan=BuildVkNrPassPlan(cfg,frame);
        Check(plan.passes.size()==count,"one/two/four/ten actual generation keys");
        for(uint32_t i=0;i<count;++i)Check(plan.passes[i].passIndex==i,"independent pass/history identity");
    }
    cfg.DlssNrSecondLayerIntensity=0.2f;cfg.DlssNrSecondLayerWorkingScale=0.5f;
    cfg.DlssNrExtraLayers[0].preset=2u;cfg.DlssNrExtraLayers[0].workingScale=2.0f;
    auto advanced=BuildVkNrPassPlan(cfg,frame);
    Check(advanced.passes[0].tuning!=advanced.passes[1].tuning,"Advanced tuning remains independent");
    Check(advanced.passes[1].frame.work.width==1920&&advanced.passes[2].frame.work.width==7680,"independent per-pass rasters");
    cfg.DlssNrBasicMultipass.value={false,10,0.5f,4,3.5f,2.5f};
    BasicMultipass::Derive(cfg);auto basic=BuildVkNrPassPlan(cfg,frame);
    Check(basic.passes.size()==4,"shared Basic fractional totals demand four passes");
    for(const auto& pass:basic.passes)Check(pass.frame.work.width==1920,"Basic shared work size");
    Check(basic.passes[3].tuning.intensity==0.5f,"Basic fractional last model strength");
    frame.route=VkNrRoute::Present;frame.work={960,536};
    auto presentBasic=BuildVkNrPassPlan(cfg,frame);
    for(const auto& pass:presentBasic.passes)Check(pass.frame.work.width==960&&pass.frame.work.height==536,
        "Basic children use actual first-pass Present raster");
    Check(!Multipass::CanDeliver(10,2,true)&&Multipass::CanDeliver(10,2,false),"partial chain never pre-FG deliverable");
    cfg.DlssNrApplyModel=false;BasicMultipass::Derive(cfg);
    Check(!Multipass::AppliesModel(cfg),"global visibility applies to all child switches");
    std::puts("shared Basic/Advanced counts, tuning, rasters, history identity and complete-chain policy verified");
}
