#include "../OptiScaler/dlssnr/VulkanNrResolution.h"
#include "../OptiScaler/dlssnr/VulkanNrRenderSize.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
template<class T> struct Value { T value;T value_or_default() const { return value; } };
struct Settings {
    Value<float> DlssNrWorkingScale{0.25f};
    Value<bool> DlssNrRunBeforeSr{false},DlssNrUiManualResolution{false};
    Value<uint32_t> DlssNrRoute{1},DlssNrPresentResolution{PresentResolution::Manual},DlssNrPresentCustomScale{50},
        DlssNrEnhancedResolution{PresentResolution::Manual},DlssNrEnhancedCustomScale{200};
};
static void Check(bool v,const char* why) { if(!v){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
int main()
{
    VkNrRenderSizeObservations observations;
    const VkExtent2D output{3840,2160};
    for(auto native:{VkExtent2D{1920,1080},VkExtent2D{2560,1440}}) {
        observations.Observe(output,{native,7});
        const auto value=observations.Take(output);
        Check(value.value.has_value(),"successful scalar observation without GPU/queue dependencies");
        VkNrFrameContract present;present.route=VkNrRoute::Present;present.output=output;
        for(auto policy:{PresentResolution::Policy{PresentResolution::FollowNative,0},
            PresentResolution::Policy{PresentResolution::FullOutput,0},PresentResolution::Policy{PresentResolution::Manual,25}}) {
            const auto decision=PresentInputDecision::Choose(PresentInput::Policy::AutoGuides,false,true,policy);
            const auto work=ResolveVkNrPresentWorkload(decision,present,{},{},value.value->extent);
            const auto expected=PresentResolution::Resolve(policy,3840,2160,native.width,native.height);
            Check(work.width==expected.width&&work.height==expected.height,"scalar size drives real Full/Match/Manual work resolution");
        }
        Check(!observations.Take(output).value,"Present consumes observation exactly once");
        Check(PresentInputDecision::Choose(PresentInput::Policy::RequireGuides,false,true,{0,0}).input==
            PresentInputDecision::InputClass::Refused,"scalar size does not satisfy RequireGuides");
    }
    observations.Observe(output,{{1920,1080},7});observations.Reject();observations.Observe(output,{{1920,1080},7});
    Check(!observations.Take(output).value,"failure poisons interval despite another successful observation");
    for(int i=0;i<100;++i)observations.Observe(output,{{1920,1080},7});
    const auto duplicate=observations.Take(output);
    Check(duplicate.value&&duplicate.value->extent.width==1920,
        "repeated identical feature/raster observations are one scalar fact, not ambiguity or overflow");
    observations.Observe(output,{{1920,1080},7});observations.Observe(output,{{2560,1440},7});
    Check(!observations.Take(output).value,"same feature with conflicting rasters remains ambiguous");
    observations.Observe(output,{{1920,1080},7});observations.Observe(output,{{1920,1080},8});
    Check(!observations.Take(output).value,"different feature identities cannot masquerade as duplicates");
    observations.Observe(output,{{1920,1080},7});observations.Observe(output,{{2560,1440},8});
    Check(!observations.Take(output).value,"ambiguous observations cannot select arbitrary dimensions");
    observations.Observe({1920,1080},{{1280,720},7});
    Check(!observations.Take(output).value,"different output does not qualify");
    for(int i=0;i<17;++i)observations.Observe(output,{{1920,1080},uint64_t(i+1)});
    Check(!observations.Take(output).value,"bounded observation overflow refuses");
    observations.Observe(output,{{4096,2160},7});
    Check(!observations.Take(output).value,"invalid render extent refuses");
    observations.Observe(output,{{1920,1080},7});observations.Reset();
    Check(!observations.Take(output).value,"lifecycle reset discards observations");
    Settings cfg;VkNrFrameContract frame;frame.route=VkNrRoute::Present;frame.output={3840,2160};
    frame.representation.format=VK_FORMAT_B8G8R8A8_UNORM;
    auto half=ResolveVkNrWorkload(cfg,frame,{});
    Check(half.width==1920&&half.height==1080,"Present Manual changes the actual work raster");
    cfg.DlssNrWorkingScale.value=2;auto stillHalf=ResolveVkNrWorkload(cfg,frame,{});
    Check(stillHalf.width==half.width&&stillHalf.height==half.height,"Native scale cannot override Present policy");
    for(auto percent:{25u,50u,100u,200u}) {
        cfg.DlssNrPresentCustomScale.value=percent;
        const auto actual=ResolveVkNrWorkload(cfg,frame,{});
        const auto oracle=PresentResolution::Resolve({PresentResolution::Manual,percent},3840,2160);
        Check(actual.width==oracle.width&&actual.height==oracle.height&&!actual.reason,"shared rounding/bounds oracle");
    }
    Check(ResolveVkNrWorkload(cfg,frame,{}).width==7680,"200 percent real allocation dimensions");
    cfg.DlssNrPresentResolution.value=PresentResolution::Automatic;
    auto automatic=ResolveVkNrWorkload(cfg,frame,{});Check(automatic.width==3840&&automatic.height==2160,"Automatic is shared full-output policy");
    cfg.DlssNrPresentResolution.value=PresentResolution::FollowNative;
    auto cold=ResolveVkNrWorkload(cfg,frame,{});Check(cold.width==0&&cold.reason,"cold explicit Match needs eligible metadata");
    VkNrTemporalMetadata metadata;metadata.color=metadata.depth=metadata.motion={5,3,1706,960};metadata.featureGeneration=7;metadata.frameToken=11;
    auto match=ResolveVkNrWorkload(cfg,frame,metadata);Check(match.width==1704&&match.height==960,"match aligns only private raster, preserving subrect");
    Check(metadata.color.x==5&&metadata.color.width==1706,"game metadata remains untouched");
    frame.deviceGeneration=1;frame.routeEpoch=2;frame.swapchainGeneration=3;
    frame.queue=reinterpret_cast<VkQueue>(4);frame.guideGeneration=7;frame.resumeGeneration=1;
    VkNrWorkloadAdmission admitted{frame,VkNrResolutionPolicy(cfg,frame),match};
    auto gap=ResolveVkNrWorkload(cfg,frame,{},admitted);
    Check(gap.width==1704&&gap.height==960,"temporary metadata gap retains only same-context admitted raster");
    const auto fallback=PresentInputDecision::Choose(PresentInput::Policy::AutoGuides,false,false,
        {PresentResolution::FollowNative,0});
    Check(ResolveVkNrPresentWorkload(fallback,frame,{},admitted).width==1704,
        "Auto metadata loss uses shared retained workload without reusing any guide images");
    auto incompatible=frame;++incompatible.resumeGeneration;
    Check(ResolveVkNrPresentWorkload(fallback,incompatible,{},admitted).width==3840,
        "Auto context change returns to shared full-output fallback");
    for(auto field:{&VkNrFrameContract::deviceGeneration,&VkNrFrameContract::routeEpoch,
        &VkNrFrameContract::swapchainGeneration,&VkNrFrameContract::resumeGeneration,&VkNrFrameContract::guideGeneration}) {
        auto changed=frame;++(changed.*field);Check(ResolveVkNrWorkload(cfg,changed,{},admitted).reason,"context change revokes retained Match workload");
    }
    auto otherQueue=frame;otherQueue.queue=reinterpret_cast<VkQueue>(5);
    Check(ResolveVkNrWorkload(cfg,otherQueue,{},admitted).reason,"queue change cannot retain old raster");
    cfg.DlssNrRoute.value=2;cfg.DlssNrEnhancedResolution.value=PresentResolution::Manual;
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==7680,"Enhanced has independent workload selector");
    frame.route=VkNrRoute::Native;cfg.DlssNrWorkingScale.value=0.5f;
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==1920,"Native retains its own selected scale");
    frame.placement=VkNrPlacement::AfterRR;frame.output={2560,1440};
    metadata.color=metadata.depth=metadata.motion={0,0,1280,720};
    cfg.DlssNrWorkingScale.value=1;cfg.DlssNrRunBeforeSr.value=true;
    const auto rrMatch=ResolveVkNrWorkload(cfg,frame,metadata);
    Check(rrMatch.width==1280&&rrMatch.height==720,"Native RR Match uses observed render size while preserving AfterRR placement");
    Check(frame.placement==VkNrPlacement::AfterRR,"Match does not move RR ahead of reconstruction");
    Check(ResolveVkNrWorkload(cfg,frame,{}).reason,"Native RR Match refuses missing metadata");
    VkNrWorkloadAdmission rrAdmitted{frame,VkNrResolutionPolicy(cfg,frame),rrMatch};
    Check(ResolveVkNrWorkload(cfg,frame,{},rrAdmitted).reason,
        "Native RR Match never substitutes previous evaluation metadata");
    auto invalid=metadata;invalid.featureGeneration=0;
    Check(ResolveVkNrWorkload(cfg,frame,invalid).reason,"Native RR Match refuses unqualified metadata");
    cfg.DlssNrRunBeforeSr.value=false;
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==2560,"Native RR Full retains output size");
    cfg.DlssNrRunBeforeSr.value=true;cfg.DlssNrUiManualResolution.value=true;
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==2560,"explicit legacy Manual 100 percent is not Match");
    cfg.DlssNrUiManualResolution.value=false;cfg.DlssNrWorkingScale.value=0.5f;
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==1280,"legacy scaled RR uses its manual output percentage");
    cfg.DlssNrWorkingScale.value=1;frame.placement=VkNrPlacement::BeforeSR;frame.output={1280,720};
    Check(ResolveVkNrWorkload(cfg,frame,metadata).width==1280,"RR disabled retains existing BeforeSR Match workload");
    cfg.DlssNrWorkingScale.value=2;frame.output={7680,4320};Check(ResolveVkNrWorkload(cfg,frame,{}).reason,"oversized work refuses before resource creation");
    cfg.DlssNrWorkingScale.value=NAN;Check(ResolveVkNrWorkload(cfg,frame,{}).reason,"non-finite Native scale refuses");
    std::puts("independent Native/Present policies, shared rounding, Match metadata and real workload dimensions verified");
}
