#include "../OptiScaler/dlssnr/VulkanNrStreamline.h"
#include "fixtures/vulkan_nr_test_params.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
struct Token:sl::FrameToken {uint32_t value=0; operator uint32_t() const override{return value;}};
template<class T> static void ObserveLoad(T& adapter,bool loaded,bool ok) {
 if constexpr(requires { adapter.FeatureLoaded(loaded,ok); }) adapter.FeatureLoaded(loaded,ok);
}
int main(){
 VkNrRecordingOwner recordings;auto cb=reinterpret_cast<VkCommandBuffer>(2);auto queue=reinterpret_cast<VkQueue>(3);
 recordings.OnAllocate(reinterpret_cast<VkDevice>(1),reinterpret_cast<VkCommandPool>(4),cb,0);recordings.OnBegin(cb,0,VK_SUCCESS);
 VulkanNrPreFg fg(recordings);VkNrRecordedOutput output;output.use=*recordings.Reserve(cb,1);
 output.frame.deviceGeneration=1;output.frame.routeEpoch=1;output.frame.queue=queue;output.frame.output={64,64};
 output.frame.temporal=VkNrTemporalMetadata{};output.frame.temporal->providerFrameKnown=true;output.frame.temporal->providerGeneration=8;
 output.frame.temporal->viewport=0;output.frame.temporal->frameToken=0;
 output.gameInput=reinterpret_cast<VkImage>(5);output.contentRevision=42;output.requestedPasses=output.completedPasses=2;
 output.resource.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;auto& image=output.resource.Resource.ImageViewInfo;
 image.Image=reinterpret_cast<VkImage>(6);image.ImageView=reinterpret_cast<VkImageView>(7);image.Format=VK_FORMAT_R8G8B8A8_UNORM;
 image.Width=image.Height=64;image.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};output.memory=reinterpret_cast<VkDeviceMemory>(9);
 VulkanNrStreamline adapter(recordings,fg,[&](VkCommandBuffer){return std::optional(output);});adapter.Provider(8,true);
 ObserveLoad(adapter,false,false);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"failed feature-unload observation grants no Off state");
 ObserveLoad(adapter,false,true);
 Check(adapter.Activity()==VkNrFgActivity::Off,"successful public FG feature unload proves Off without missing options/state");
 ObserveLoad(adapter,true,true);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"feature reload requires fresh options/state");
 sl::DLSSGOptions options;options.mode=sl::DLSSGMode::eOn;options.numFramesToGenerate=2;options.enableUserInterfaceRecomposition=sl::eFalse;
 options.queueParallelismMode=sl::DLSSGQueueParallelismMode::eBlockPresentingClientQueue;
 sl::DLSSGState state;state.status=sl::DLSSGStatus::eOk;state.numFramesToGenerateMax=1;
 adapter.Options(0,options,true);adapter.State(0,state,true);
 ObserveLoad(adapter,true,true);
 Check(adapter.Activity()==VkNrFgActivity::On,"repeated successful loaded observation preserves current options");
 options.mode=sl::DLSSGMode::eOff;adapter.Options(0,options,true);adapter.State(0,state,false);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"failed observed provider state cannot grant Present FG-off admission");
 options.mode=sl::DLSSGMode::eOn;adapter.Options(0,options,true);adapter.State(0,state,true);
 sl::Resource source;source.native=reinterpret_cast<void*>(output.gameInput);source.width=source.height=64;source.nativeFormat=image.Format;
 sl::ResourceTag tag(&source,sl::kBufferTypeHUDLessColor,sl::eValidUntilPresent);Token token;
 Check(!adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb)).accepted,"fixed ratio bounded by observed provider");
 state.numFramesToGenerateMax=2;adapter.State(0,state,true);
 Check(!adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},nullptr).accepted,"null command buffer no fake copy");
 token.value=1;Check(!adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb)).accepted,"same viewport wrong token refused");token.value=0;
 Check(!adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb)).accepted,"replayable public tag recording cannot replace FG inputs");
 recordings.OnReset(cb,VK_SUCCESS);recordings.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 output.use=*recordings.Reserve(cb,1);
 auto decision=adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb));
 Check(decision.accepted&&decision.consumer&&decision.tags[0].lifecycle==sl::eOnlyValidNow,"qualified volatile replacement");
 auto captured=decision.tags[0].resource->native;Check(captured==reinterpret_cast<void*>(image.Image),"original call copies revised image");
 adapter.TagsReturned(decision,sl::Result::eOk);auto binding=fg.Binding(*decision.consumer);Check(binding&&binding->contribution.contentRevision==42,"captured content revision matches real output");
 auto primary=fg.PrimaryContributions();Check(fg.Generated(*decision.consumer,2,0)&&fg.Generated(*decision.consumer,2,1)&&primary==fg.PrimaryContributions(),"generated callbacks never evaluate NR");
 recordings.OnWork(cb);Check(!adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb)).accepted,"work after source invalidates association");
 // The earlier producer runs only after successful fixed provider qualification.
 output.use=*recordings.Reserve(cb,1);output.commandSerial=recordings.CommandSerial(cb);
 unsigned prepared=0;adapter.LookupOutput({});adapter.PrepareOutput([&](VkCommandBuffer command,uint64_t provider,uint64_t frame,uint32_t viewport,VkNrTaggedColor tagged){
  Check(command==cb&&provider==8&&frame==0&&viewport==0&&tagged.image==output.gameInput,"public tag identity reaches earlier producer");++prepared;return std::optional(output);});
 auto early=adapter.BeforeTags(&token,sl::ViewportHandle(0),{&tag,1},reinterpret_cast<sl::CommandBuffer*>(cb));
 Check(early.accepted&&prepared==1,"qualified earlier final-color producer feeds original volatile tag");adapter.TagsReturned(early,sl::Result::eOk);
 recordings.OnEnd(cb,VK_SUCCESS);auto copyUses=*recordings.SnapshotUses(cb);
 auto copySubmission=*recordings.PrepareSubmission({queue,1,copyUses});recordings.SubmissionReturned(copySubmission,VK_SUCCESS);recordings.CheckpointReturned(copySubmission,VK_SUCCESS);
 fg.ObserveSubmissions();recordings.OnReset(cb,VK_SUCCESS);fg.RetireCompleted();
 Check(fg.ConsumerCount()==2&&!recordings.Reusable(output.use),"CPU tag return and recording reset cannot retire an unfinished volatile GPU copy");
 recordings.ObserveFence(copySubmission,VK_SUCCESS);fg.RetireCompleted();
 Check(fg.ConsumerCount()==0&&recordings.Reusable(output.use),"actual same-command-buffer copy completion plus recording release retires source leases");
 // The actual NGX ABI has no inferred frame counter fallback.
 auto& nativeRecordings=VulkanNrRecordings();nativeRecordings.OnAllocate(reinterpret_cast<VkDevice>(1),reinterpret_cast<VkCommandPool>(4),cb,0);nativeRecordings.OnBegin(cb,0,VK_SUCCESS);
 output.use=*nativeRecordings.Reserve(cb,1);output.commandSerial=0;output.frame.temporal->providerGeneration=0;
 unsigned models=0;
 SetVkNrNgxOutputLookup([&](VkCommandBuffer){++models;return std::optional(output);},
  [&](VkCommandBuffer,uint64_t,uint64_t,uint32_t,VkNrTaggedColor){++models;return std::optional(output);});
 StrictParams params;NVSDK_NGX_Handle feature{123};params.Set("DLSSG.MultiFrameCountMax",2u);
 ObserveVkNrNgxFgCreate(reinterpret_cast<VkDevice>(1),&feature,&params,NVSDK_NGX_Result_Success);
 auto original=output.resource;original.Resource.ImageViewInfo.Image=output.gameInput;
 params.Set("DLSSG.HUDLess",static_cast<void*>(&original));params.Set("DLSSG.BackbufferFrameID",0ull);
 params.Set("DLSSG.MultiFrameCount",2u);params.Set("DLSSG.MultiFrameIndex",1u);
 params.Set("DLSSG.HUDLessSubrectBaseX",0u);params.Set("DLSSG.HUDLessSubrectBaseY",0u);params.Set("DLSSG.HUDLessSubrectWidth",64u);params.Set("DLSSG.HUDLessSubrectHeight",64u);
 {VkNrNgxFgScope input(cb,&feature,&params);void* revised=nullptr;params.Get("DLSSG.HUDLess",&revised);
  Check(revised==&original,"private native FG preserves original input without a documented input-release contract");input.Returned(NVSDK_NGX_Result_Success);}
 void* restored=nullptr;params.Get("DLSSG.HUDLess",&restored);Check(restored==&original&&models==0,"native FG pass-through invokes no NR and restores parameters");
 nativeRecordings.OnWork(cb);params.Set("DLSSG.MultiFrameIndex",2u);
 {VkNrNgxFgScope child(cb,&feature,&params);params.Get("DLSSG.HUDLess",&restored);
  Check(restored==&original,"private native MFG children preserve original input without a release contract");child.Returned(NVSDK_NGX_Result_Success);}
 params.Set("DLSSG.BackbufferFrameID",1ull);{VkNrNgxFgScope wrong(cb,&feature,&params);params.Get("DLSSG.HUDLess",&restored);Check(restored==&original,"native FG wrong authentic frame remains unchanged");wrong.Returned(NVSDK_NGX_Result_Success);}
 // Simulate a provider invalidating/reusing handle storage during original ReleaseFeature.
 const auto releasedId=feature.Id;feature.Id=999;Check(releasedId!=feature.Id,"provider invalidated original storage");
 ObserveVkNrNgxFgRelease(releasedId,NVSDK_NGX_Result_Success);
 Check(VkNrNgxFgFeatureCount()==0,"release uses identity captured before provider invalidates handle storage");
 ObserveLoad(adapter,false,true);
 Check(adapter.Activity()==VkNrFgActivity::Off,"successful unload supersedes previous active options");
 ObserveLoad(adapter,true,false);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"failed reload revokes previous unloaded observation");
 ObserveLoad(adapter,false,true);adapter.Provider(9,true);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"new provider cannot inherit previous unloaded observation");
 std::puts("Streamline Vulkan volatile tags, authentic token zero, provider ceilings and consumer ownership verified");
}
