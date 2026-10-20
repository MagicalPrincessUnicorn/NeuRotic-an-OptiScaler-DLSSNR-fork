#include "NrConfigFixture.h"
#include "dlssnr/VulkanNrNativeObservation.h"
#include "inputs/VulkanNativeFeatures.h"
#include <cstdio>
#include <cstring>
struct Parameters final:NVSDK_NGX_Parameter {
 NVSDK_NGX_Resource_VK output{};
 Parameters(){output.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;output.ReadWrite=true;
  auto& i=output.Resource.ImageViewInfo;i.Image=reinterpret_cast<VkImage>(1);i.ImageView=reinterpret_cast<VkImageView>(2);
  i.Width=3840;i.Height=2160;i.Format=VK_FORMAT_R16G16B16A16_SFLOAT;i.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};}
 void Set(const char*,unsigned long long)override{} void Set(const char*,float)override{}
 void Set(const char*,double)override{} void Set(const char*,unsigned int)override{}
 void Set(const char*,int)override{} void Set(const char*,ID3D11Resource*)override{}
 void Set(const char*,ID3D12Resource*)override{} void Set(const char*,void*)override{}
 NVSDK_NGX_Result Get(const char*,unsigned long long*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char* key,float* v)const override{
  if(!std::strcmp(key,NVSDK_NGX_Parameter_Jitter_Offset_X)||!std::strcmp(key,NVSDK_NGX_Parameter_Jitter_Offset_Y)){*v=0;return NVSDK_NGX_Result_Success;}
  if(!std::strcmp(key,NVSDK_NGX_Parameter_MV_Scale_X)||!std::strcmp(key,NVSDK_NGX_Parameter_MV_Scale_Y)||!std::strcmp(key,NVSDK_NGX_Parameter_DLSS_Pre_Exposure)){*v=1;return NVSDK_NGX_Result_Success;}
  return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char*,double*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char*,unsigned int*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char*,int*)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char*,ID3D11Resource**)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char*,ID3D12Resource**)const override{return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 NVSDK_NGX_Result Get(const char* key,void** v)const override{if(!std::strcmp(key,NVSDK_NGX_Parameter_Output)){*v=const_cast<NVSDK_NGX_Resource_VK*>(&output);return NVSDK_NGX_Result_Success;}return NVSDK_NGX_Result_FAIL_InvalidParameter;}
 void Reset()override{}
};
namespace DlssNr
{
namespace {
bool SameTemporal(const std::optional<VkNrTemporalMetadata>& a,const std::optional<VkNrTemporalMetadata>& b,
                  bool livePresentGuides)
{
    if(a.has_value()!=b.has_value())return false;
    // Present allocates model images from output/work sizes, not guide rasters.
    // Current guide rectangles/scales are validated and written on each evaluation.
    // Treating dynamic resolution as model creation state caused repeated blank
    // preparation frames even with a fixed Automatic/Manual processing size.
    const bool sameSampling=!a||(a->color==b->color&&a->depth==b->depth&&a->motion==b->motion&&
        a->motionScaleX==b->motionScaleX&&a->motionScaleY==b->motionScaleY);
    return !a||((sameSampling||(livePresentGuides&&ValidVkNrTemporal(*a)&&ValidVkNrTemporal(*b)))&&
        a->featureFlags==b->featureFlags&&a->depthInverted==b->depthInverted&&a->featureGeneration==b->featureGeneration&&a->providerFrameKnown==b->providerFrameKnown&&a->providerGeneration==b->providerGeneration&&a->viewport==b->viewport);
}
}
bool CompatibleVkNrGeneration(const VkNrGenerationKey& a,const VkNrGenerationKey& b)
{
    const auto& x=a.frame;const auto& y=b.frame;
    return x.route==y.route&&x.placement==y.placement&&x.deviceGeneration==y.deviceGeneration&&
        x.routeEpoch==y.routeEpoch&&x.swapchainGeneration==y.swapchainGeneration&&
        x.queue==y.queue&&x.queueFamily==y.queueFamily&&x.resumeGeneration==y.resumeGeneration&&x.guideGeneration==y.guideGeneration&&
        x.inputInterruptionEpoch==y.inputInterruptionEpoch&&
        x.output.width==y.output.width&&x.output.height==y.output.height&&
        x.work.width==y.work.width&&x.work.height==y.work.height&&x.representation==y.representation&&
        SameTemporal(x.temporal,y.temporal,x.route==VkNrRoute::Present&&y.route==VkNrRoute::Present&&
            x.placement==VkNrPlacement::AfterSR&&y.placement==VkNrPlacement::AfterSR&&
            !a.hold&&!b.hold&&!a.privateDlaa&&!b.privateDlaa)&&a.colorFormat==b.colorFormat&&a.depthFormat==b.depthFormat&&
        a.motionFormat==b.motionFormat&&a.targetFormat==b.targetFormat&&a.gameHdr==b.gameHdr&&
        a.hold==b.hold&&a.creationFlags==b.creationFlags&&a.passIndex==b.passIndex&&a.privateDlaa==b.privateDlaa&&
        a.srOutput.width==b.srOutput.width&&a.srOutput.height==b.srOutput.height&&
        a.srQuality==b.srQuality&&a.preSrEpoch==b.preSrEpoch;
}
bool VkNrGenerationKey::operator==(const VkNrGenerationKey& b) const
{ return CompatibleVkNrGeneration(*this,b)&&upFilter==b.upFilter&&downFilter==b.downFilter&&tuning==b.tuning; }

}

using namespace DlssNr;
unsigned checks=0,failed=0;
void Check(bool value,const char* name){++checks;failed+=!value;std::printf("%s %s\n",value?"PASS":"FAIL",name);}
VkRecordResult Recorded(const VkNrNativeObservation& observation,uint64_t generation=5) {
 VkRecordResult result;result.status=VkRecordStatus::Complete;
 result.modelRecorded=result.outputWriteRecorded=true;result.completedPasses=1;
 auto frame=observation.frame;frame.deviceGeneration=9;frame.routeEpoch=4;
 result.nativeRecording=VkNrNativeRecordingReceipt{frame,{31},generation};return result;
}
int main(){
 TestConfig config;config.SetDlssNrEnabled(true);config.DlssNrRoute=0u;config.DlssNrNativeResolution=1u;
 config.DlssNrRunBeforeSr=false;config.DlssNrRenderingMode=0;config.DlssNrWorkingScale=1.f;
 config.DlssNrUiManualResolution=false;
 const NrConfigSnapshot<TestConfig> a(config);Parameters params;
 const auto device=reinterpret_cast<VkDevice>(8);VulkanNativeFeatures::Registry registry;
 const auto identity=registry.Register(42,1,reinterpret_cast<uintptr_t>(device));
 auto current=[&registry,identity]{const auto now=registry.Current(42);return now.generation==identity.generation&&now.device==identity.device&&now.type==identity.type;};
 VkNrTemporalMetadata temporal;temporal.color=temporal.depth=temporal.motion={0,0,1600,900};
 temporal.featureGeneration=identity.generation;temporal.frameToken=11;
 VkNrEvaluationIdentity evaluation{reinterpret_cast<VkCommandBuffer>(3),1,11,identity.generation};
 auto capture=[&](const auto& cfg,bool rr,uint64_t invocation){auto e=evaluation;e.invocation=invocation;
  return CaptureVkNrNativeObservation(&params,&temporal,cfg,rr,e,device,2,current);};
 const auto oa=capture(a,false,11);
 Check(oa.workload.requestedWork.width==1600&&oa.workload.requestedWork.height==896&&oa.workload.reason.empty(),"production capture resolves authentic MatchPost evaluation and temporal raster");
 auto missing=oa.frame;missing.evaluation={};Check(ResolveVkNrWorkload(a,missing,temporal).reason,"missing actual evaluation still refuses");
 auto stale=temporal;stale.frameToken++;Check(ResolveVkNrWorkload(a,oa.frame,stale).reason,"mismatched temporal still refuses");
 struct LegacySettings{} legacy;Check(NativeResolution::Selection(legacy)==NativeResolution::LegacySelection,"legacy no-field selection remains valid");
 Check(NativeResolution::Selection(a)==NativeResolution::PostNativeSelection,"real selected Post field remains valid");
 VkNrNativeRawStatus raw;VkNrNativeSelectedStatus<NrConfigSnapshot<TestConfig>> selected;
 auto success=Recorded(oa);
 Check(!selected.observation&&selected.observations==0,"capture does not publish a selected result");
 Check(raw.Publish(oa,true,2)&&raw.observations==1,"first actual raw terminal increments progress once");
 Check(selected.Publish(oa,a,success,false,false,true,true,2)&&selected.accepted,"actual paired terminal is delivered without requiring finalOutput");
 Check(selected.Matches(a,9,4,5,2),"matching actual settings device route generation and input lifetime are eligible");
 Check(!success.finalOutput,"eligibility did not fabricate or require optional finalOutput");
 Check(selected.workload.requestedWork.width==1600&&selected.workload.appliedWork.width==1600,"selected workload pairs exact request and actual recording");
 config.DlssNrWhitePointScale=1.25f;const NrConfigSnapshot<TestConfig> b(config);
 auto ob=capture(b,false,12);
 Check(selected.settings->SameConfiguration(a)&&selected.observation->frame.evaluation==oa.frame.evaluation,"capture B preserves selected A settings and evaluation");
 Check(!selected.Matches(b,9,4,5,2),"uncommitted settings B cannot relabel selected A as current");
 VkNrWorkloadStatus ambient;ambient.requestedWork={64,64};ambient.appliedWork={128,128};
 Check(selected.workload.appliedWork.width!=ambient.appliedWork.width&&selected.workload.requestedWork.width==1600,"inflight recorder workload cannot replace selected terminal workload");
 auto oldCreation=Recorded(oa).nativeRecording->frame;oldCreation.settingsRevision=1;
 auto rb=Recorded(ob);rb.nativeRecording->frame.settingsRevision=99;
 VkNrGenerationKey oldKey,newKey;oldKey.frame=oldCreation;newKey.frame=rb.nativeRecording->frame;
 Check(oldKey==newKey,"actual generation equality preserves structural reuse across evaluation and observation revision");
 Check(selected.Publish(ob,b,rb,false,false,true,true,2)&&selected.Matches(b,9,4,5,2),"current B receipt remains eligible on reused generation with older creation revision");
 Check(!selected.Publish(oa,a,success,false,false,true,true,2)&&selected.observation->frame.evaluation.invocation==12,"late older selected completion cannot overwrite newer B");
 Check(!raw.Publish(oa,true,2)&&raw.observations==1,"repeated or older raw completion does not double count");
 Check(raw.Publish(ob,true,2)&&raw.observations==2,"second distinct raw terminal advances beyond local capture count one");
 const auto selectedCount=selected.observations;const auto selectedWork=selected.workload.requestedWork;
 temporal.color=temporal.depth=temporal.motion={0,0,1280,720};temporal.frameToken=13;
 params.output.Resource.ImageViewInfo.Width=2560;params.output.Resource.ImageViewInfo.Height=1440;
 auto duplicate=capture(b,true,13);
 Check(raw.Publish(duplicate,true,2)&&raw.observations==3&&raw.observation->render.width==1280,"RR duplicate advances only genuine raw source diagnostics");
 Check(!selected.Publish(duplicate,b,{},false,false,true,false,2)&&selected.observations==selectedCount&&selected.accepted&&
   !selected.observation->rayReconstruction&&selected.workload.requestedWork.width==selectedWork.width,"successful duplicate or auxiliary preserves all selected content and progress");
 NrPreflightSignals::FreshObservation selectedFresh;
 selectedFresh.Update(4,selectedCount-1,100,true);
 Check(selectedFresh.Update(4,selectedCount,200,true),"real selected progress establishes display freshness");
 Check(!selectedFresh.Update(4,selectedCount,2300,true),"duplicate-only raw activity cannot keep selected delivery fresh");
 config.SetDlssNrEnabled(false);const NrConfigSnapshot<TestConfig> disabled(config);auto off=capture(disabled,false,14);
 Check(raw.Publish(off,true,2)&&raw.observations==4,"Native NR off preserves actual source-key progress");
 Check(!selected.Publish(off,disabled,{},false,false,true,true,2)&&selected.observations==selectedCount,"Native NR off does not publish NR result or progress");
 config.SetDlssNrEnabled(true);config.DlssNrRoute=1u;const NrConfigSnapshot<TestConfig> present(config);auto op=capture(present,false,15);
 Check(raw.Publish(op,false,2)&&raw.observations==5&&!raw.srSucceeded,"Present SR failure still reports genuinely supplied raw inputs");
 Check(!selected.Publish(op,present,{},false,false,false,false,2)&&selected.accepted,"Present failure does not manufacture a Native delivery or history result");
 config.DlssNrRoute=0u;const NrConfigSnapshot<TestConfig> native(config);auto fail=capture(native,false,16);
 Check(selected.Publish(fail,native,Recorded(fail),false,false,false,false,2)&&!selected.accepted&&selected.workload.pending&&
    selected.workload.reason=="Selected Vulkan SR evaluation failed","actual SR failure invalidates even when NR not selected");
 auto recover=capture(native,false,17);
 Check(selected.Publish(recover,native,Recorded(recover),false,false,true,true,2)&&selected.accepted&&selected.workload.reason.empty(),"genuine successful selected result clears prior waiting failure");
 auto mismatched=capture(native,false,18);auto wrong=Recorded(mismatched);wrong.nativeRecording->frame.evaluation.invocation++;
 Check(selected.Publish(mismatched,native,wrong,false,false,true,true,2)&&!selected.accepted,"unrelated recorder result cannot qualify selected success");
 auto absent=capture(native,false,19);auto noReceipt=Recorded(absent);noReceipt.nativeRecording.reset();
 Check(selected.Publish(absent,native,noReceipt,false,false,true,true,2)&&!selected.accepted,"missing actual recording receipt is distinct from absent optional finalOutput");
 config.DlssNrRunBeforeSr=true;config.DlssNrNativeResolution=0u;const NrConfigSnapshot<TestConfig> before(config);auto pre=capture(before,false,20);
 Check(selected.Publish(pre,before,Recorded(pre),true,false,true,true,2)&&!selected.accepted,"unbound required pre-SR carrier remains refused");
 auto preOk=capture(before,false,21);
 Check(selected.Publish(preOk,before,Recorded(preOk),true,true,true,true,2)&&selected.accepted,"bound genuine pre-SR recording remains accepted");
 for(const auto* label:{"throw", "uninitialized", "failed restoration"}) {
  const auto next=selected.latestInvocation+1;auto invalid=capture(before,false,next);
  Check(selected.Publish(invalid,before,Recorded(invalid),true,true,false,true,2)&&!selected.accepted,label);
 }
 auto latest=capture(before,false,25);selected.Publish(latest,before,Recorded(latest),true,true,true,true,2);
 Check(!selected.Matches(before,10,4,5,2),"actual device generation transition revokes selected eligibility");
 Check(!selected.Matches(before,9,5,5,2),"actual route lifecycle transition revokes selected eligibility");
 Check(!selected.Matches(before,9,4,6,2),"actual applied model generation transition revokes selected eligibility");
 Check(!selected.Matches(before,9,4,5,3)&&!raw.Current(3),"actual input lifetime transition revokes raw and selected observations");
 config.state.RequestNewSession();const NrConfigSnapshot<TestConfig> resumed(config);
 Check(!selected.Matches(resumed,9,4,5,2),"resume generation cannot relabel retained selected success");
 const auto privateCfg=before.ForPrivateDiagnostic();auto privateObs=capture(privateCfg,false,26);
 Check(!selected.Publish(privateObs,privateCfg,Recorded(privateObs),true,true,true,true,2),"private diagnostic work never becomes ordinary Native delivery");
 registry.Release(42,true);
 Check(!raw.Current(2)&&!selected.Matches(before,9,4,5,2),"actual game feature release retires raw and selected observations without model allocation");
 registry.Register(42,1,reinterpret_cast<uintptr_t>(device));
 Check(!raw.Current(2),"same feature handle reused in a new registry generation cannot revive old observation");
 raw.InvalidateDevice(device);Check(!raw.observation&&raw.observations==0,"device destruction clears raw observations even without any Native model owner");
 Check(!raw.Publish(privateObs,true,2),"retired actual source cannot publish a late raw completion");
 std::printf("RESULT checks=%u failed=%u CPU_ONLY=true NO_DEVICE=true\n",checks,failed);return failed?1:0;
}
