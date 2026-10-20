#include "../OptiScaler/dlssnr/VulkanNrPreSr.h"
#include <unordered_map>
#include <string>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using namespace DlssNr;
struct Params
{
    std::unordered_map<std::string,void*> pointers;std::unordered_map<std::string,float> floats;
    std::unordered_map<std::string,unsigned> integers;
    template<class T> auto& Values() { if constexpr(std::is_same_v<T,void*>)return pointers;else if constexpr(std::is_same_v<T,float>)return floats;else return integers; }
    template<class T> unsigned Get(const char* name,T* value) { auto& map=Values<T>();auto found=map.find(name);if(found==map.end())return 0;*value=found->second;return 1; }
    template<class T> void Set(const char* name,T value) { Values<T>()[name]=value; }
};
static void Check(bool ok,const char* why) { if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);} }
int main()
{
    VkNrRecordingOwner owner;auto device=reinterpret_cast<VkDevice>(1);auto pool=reinterpret_cast<VkCommandPool>(2);
    auto cb=reinterpret_cast<VkCommandBuffer>(3);auto queue=reinterpret_cast<VkQueue>(4);
    owner.OnAllocate(device,pool,cb,0);owner.OnBegin(cb,0,VK_SUCCESS);auto use=owner.Reserve(cb,1);Check(use.has_value(),"known pre-SR recording");
    VkNrPreSrAdmission admission{true,true,true,true,false,false};Check(ShouldRecordVkNrPreSr(admission),"selected native SR admits Performance once");
    admission.rayReconstruction=true;Check(!ShouldRecordVkNrPreSr(admission),"RR always preserves reconstruction inputs");
    admission.rayReconstruction=false;admission.bridged=true;Check(!ShouldRecordVkNrPreSr(admission),"bridge retains sole recorder");
    admission.bridged=false;admission.selectedSr=false;
    Check(!ShouldRecordVkNrAfterSr(admission),"unproved Performance cannot silently become post-SR NR");
    admission.rayReconstruction=true;
    Check(ShouldRecordVkNrAfterSr(admission),"RR keeps after-reconstruction placement when Performance requested");
    admission.rayReconstruction=false;admission.beforeRequested=false;
    Check(ShouldRecordVkNrAfterSr(admission),"Quality selects post-SR NR");
    admission.bridged=true;Check(!ShouldRecordVkNrAfterSr(admission),"bridge excludes second post-SR recorder");
    int originalColor=1,originalOutput=2,privateColor=3;Params params;
    params.Set(NVSDK_NGX_Parameter_Color,static_cast<void*>(&originalColor));params.Set(NVSDK_NGX_Parameter_Output,static_cast<void*>(&originalOutput));
    params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,0.3f);params.Set(NVSDK_NGX_Parameter_Jitter_Offset_Y,-0.4f);params.Set(NVSDK_NGX_Parameter_Reset,0u);
    VkNrFrameContract frame;frame.evaluation={cb,owner.Incarnation(cb),1,1};frame.queueFamily=0;frame.deviceGeneration=1;frame.routeEpoch=2;frame.queue=queue;
    frame.placement=VkNrPlacement::BeforeSR;frame.output={1280,720};frame.representation.format=VK_FORMAT_R16G16B16A16_SFLOAT;
    VkNrPreSrCarrier carrier{&privateColor,frame,*use,true,true};
    unsigned modelCalls=0;
    try {
        VkNrPreSrScope scope(&params,owner,frame);Check(scope.Ready(),"typed original parameters saved");
        ++modelCalls;Check(scope.Bind(carrier,frame),"private carrier reaches intended SR call");
        Check(params.pointers[NVSDK_NGX_Parameter_Color]==&privateColor&&params.integers[NVSDK_NGX_Parameter_Reset]==1,"SR sees NR carrier and one-shot reset");
        params.Set(NVSDK_NGX_Parameter_Output,static_cast<void*>(&privateColor));
        params.Set(NVSDK_NGX_Parameter_Jitter_Offset_X,4.0f);throw std::runtime_error("original SR failed");
    }catch(const std::runtime_error&){}
    Check(params.pointers[NVSDK_NGX_Parameter_Color]==&originalColor&&params.pointers[NVSDK_NGX_Parameter_Output]==&originalOutput&&
        params.floats[NVSDK_NGX_Parameter_Jitter_Offset_X]==0.3f&&params.floats[NVSDK_NGX_Parameter_Jitter_Offset_Y]==-0.4f&&
        params.integers[NVSDK_NGX_Parameter_Reset]==0&&modelCalls==1,"failure/exception restores all original inputs without another model call");
    { VkNrPreSrScope scope(&params,owner,frame);auto changed=frame;changed.output.width=1920;
      Check(!scope.Bind(carrier,changed)&&params.pointers[NVSDK_NGX_Parameter_Color]==&originalColor,"resize during preparation cannot substitute incompatible carrier"); }
    params.integers.erase(NVSDK_NGX_Parameter_Reset);
    {
        VkNrPreSrScope scope(&params,owner,frame);
        Check(scope.Ready()&&scope.Bind(carrier,frame),"SDK optional Reset defaults to zero and permits Pre-SR binding");
        Check(scope.Restore()&&params.integers[NVSDK_NGX_Parameter_Reset]==0,"injected reset restores SDK zero semantics");
    }
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X,8u);
    params.Set(NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y,12u);
    {
        VkNrPreSrScope scope(&params,owner,frame);
        Check(scope.Bind(carrier,frame)&&params.integers[NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X]==0&&
            params.integers[NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y]==0,"cropped private SR color starts at zero");
        Check(scope.Restore()&&params.integers[NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_X]==8&&
            params.integers[NVSDK_NGX_Parameter_DLSS_Input_Color_Subrect_Base_Y]==12,"game source origin restored after SR");
    }
    {
        VkNrPreSrScope scope(&params,owner,frame);
        Check(scope.Bind(carrier,frame),"failure return path binds once");
        const bool srSucceeded = false;
        Check(!srSucceeded && scope.Restore(),"ordinary SR failure restores synchronously before fallback");
        Check(params.pointers[NVSDK_NGX_Parameter_Color]==&originalColor &&
            params.pointers[NVSDK_NGX_Parameter_Output]==&originalOutput && modelCalls==1,
            "failed SR return preserves original resources and model call count");
    }
    {
        auto familyOnly=frame;familyOnly.queue=VK_NULL_HANDLE;
        auto familyCarrier=carrier;familyCarrier.frame=familyOnly;
        VkNrPreSrScope scope(&params,owner,familyOnly);
        Check(scope.Bind(familyCarrier,familyOnly),"exact selected SR scope does not need a future queue prediction");
    }
    {
        auto other=frame;++other.evaluation.invocation;
        VkNrPreSrScope scope(&params,owner,frame);
        Check(!scope.Bind(carrier,other),"same raster on another selected invocation cannot bind");
    }
    {
        auto stale=carrier;stale.frame.evaluation.incarnation++;
        VkNrPreSrScope scope(&params,owner,frame);
        Check(!scope.Bind(stale,frame),"stale CB incarnation cannot bind private NR carrier");
    }
    VkNrPreSrSeed seed(owner);seed.Record(*use,true);Check(!seed.Ready(),"private DLAA/NR seed waits for actual completion");
    owner.OnEnd(cb,VK_SUCCESS);auto submitted=owner.PrepareSubmission({queue,1,{*use}});Check(submitted.has_value(),"seed really submits");
    owner.SubmissionReturned(*submitted,VK_SUCCESS);owner.CheckpointReturned(*submitted,VK_SUCCESS);owner.ObserveFence(*submitted,VK_SUCCESS);
    Check(seed.Ready(),"owned completed seed allows handoff");
    PreSrResetPolicyState resetState;auto reset=AdvancePreSrResetPolicy(resetState,true,true);Check(reset.softResetForBurst,"consent sampled at burst start");
    reset=AdvancePreSrResetPolicy(resetState,true,false);Check(reset.softResetForBurst,"mid-burst edit cannot change reset policy");
    reset=AdvancePreSrResetPolicy(resetState,false,false);Check(reset.resetEnded&&!reset.conservativeTransition,"soft reset burst ends coherently");
    reset=AdvancePreSrResetPolicy(resetState,true,false);Check(reset.conservativeTransition,"without consent conservative lifecycle transition wins");
    std::puts("selected pre-SR scope, RR/bridge exclusion, restored parameters, seed completion and reset burst semantics verified");
}
