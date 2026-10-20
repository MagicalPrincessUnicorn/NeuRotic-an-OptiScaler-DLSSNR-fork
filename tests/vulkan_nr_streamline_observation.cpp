#include "../OptiScaler/dlssnr/VulkanNrStreamline.h"
#include <cstdio>
using namespace DlssNr;
static unsigned failures;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);++failures;}}
static void RetainedConsumer(unsigned transition){
 VkNrRecordingOwner recordings;auto device=reinterpret_cast<VkDevice>(1);auto cb=reinterpret_cast<VkCommandBuffer>(2);auto queue=reinterpret_cast<VkQueue>(3);
 recordings.OnAllocate(device,reinterpret_cast<VkCommandPool>(4),cb,0);recordings.OnBegin(cb,0,VK_SUCCESS);
 const auto use=*recordings.Reserve(cb,1);VulkanNrPreFg fg(recordings);VulkanNrStreamline adapter(recordings,fg);adapter.Provider(8,true);
 VkNrContribution c;c.producer=use;c.providerGeneration=8;c.viewport=0;c.contentRevision=4;c.image=reinterpret_cast<VkImage>(5);
 c.view=reinterpret_cast<VkImageView>(6);c.subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.layout=VK_IMAGE_LAYOUT_GENERAL;
 c.frame.deviceGeneration=1;c.frame.queue=queue;c.frame.routeEpoch=1;c.frame.temporal=VkNrTemporalMetadata{};c.frame.temporal->frameToken=10;
 c.chain.requested=c.chain.completed=1;c.chain.deliverable=true;c.chain.outputVersion=4;c.chain.recording.outputWriteRecorded=true;
 Check(fg.Publish(c),"fixture publishes real-frame contribution");
 VkNrFgConsumer consumer{8,10,0,queue,cb,c.image,4,c.subresource,c.layout,1};
 const auto id=fg.Bind(consumer);if(!id){Check(false,"fixture binds consumer");return;}fg.ConsumerAccepted(*id);
 sl::DLSSGOptions off;off.mode=sl::DLSSGMode::eOff;
 if(transition==1)adapter.Provider(9,true);
 else if(transition==2)adapter.FeatureLoaded(false,true);
 else adapter.Options(0,off,true);
 Check(!fg.Bind(consumer),"Off/provider transition revokes publication");
 recordings.OnEnd(cb,VK_SUCCESS);auto uses=*recordings.SnapshotUses(cb);
 auto submit=*recordings.PrepareSubmission({queue,1,uses});recordings.SubmissionReturned(submit,VK_SUCCESS);recordings.CheckpointReturned(submit,VK_SUCCESS);
 fg.ConsumerSubmitted(*id,submit);recordings.ObserveFence(submit,VK_SUCCESS);recordings.OnReset(cb,VK_SUCCESS);fg.RetireCompleted();
 Check(fg.ConsumerCount()==1&&!recordings.SafeToWriteAfter(use),"Off/provider transition must retain unreleased asynchronous consumer after GPU completion");
 fg.ConsumerReleased(*id);fg.RetireCompleted();
 Check(fg.ConsumerCount()==0&&recordings.Reusable(use),"authentic consumer release plus completion allows retirement");
}
int main(){
 VkNrRecordingOwner recordings;VulkanNrPreFg fg(recordings);VulkanNrStreamline adapter(recordings,fg);adapter.Provider(1,true);
 sl::DLSSGOptions options;sl::DLSSGState state;state.status=sl::DLSSGStatus::eOk;state.numFramesToGenerateMax=1;
 options.mode=sl::DLSSGMode::eOff;adapter.Options(0,options,true);
 Check(adapter.Activity()==VkNrFgActivity::Off,"successful Off options alone establish Off without polling state");
 state.structVersion=1;adapter.State(0,state,true);
 Check(adapter.Activity()==VkNrFgActivity::Off,"valid state v1 does not erase successful Off observation");
 state.structVersion=4;
 for(auto mode:{sl::DLSSGMode::eAuto,sl::DLSSGMode::eDynamic}){
  options.mode=mode;adapter.Options(0,options,true);adapter.State(0,state,true);
  Check(adapter.Activity()==VkNrFgActivity::Unknown,"Auto and Dynamic do not establish fixed active output");
 }
 options.mode=sl::DLSSGMode::eOn;adapter.Options(0,options,true);adapter.State(0,state,false);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"failed compatible state makes active observation uncertain");
 adapter.State(0,state,true);Check(adapter.Activity()==VkNrFgActivity::On,"successful fixed options and state establish observed On");
 options.mode=sl::DLSSGMode::eOff;options.onErrorCallback=[](const sl::APIError&){};
 adapter.Options(0,options,true);Check(adapter.Activity()==VkNrFgActivity::Off,"valid callback-bearing Off remains observable");
 adapter.State(0,state,false);Check(adapter.Activity()==VkNrFgActivity::Unknown,"later failed state invalidates current certainty");
 adapter.Options(0,options,true);Check(adapter.Activity()==VkNrFgActivity::Off,"fresh successful Off supersedes earlier failed state");
 for(uint32_t viewport=1;viewport<64;++viewport)adapter.Options(viewport,options,true);
 options.mode=sl::DLSSGMode::eOn;adapter.Options(64,options,true);
 Check(adapter.Activity()==VkNrFgActivity::Unknown,"bounded observer overflow cannot fabricate global Off");
 RetainedConsumer(0);RetainedConsumer(1);RetainedConsumer(2);
 std::printf("Streamline observation/lifetime: %u failure(s)\n",failures);return failures?1:0;
}
