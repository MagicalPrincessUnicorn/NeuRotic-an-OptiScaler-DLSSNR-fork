#include "../OptiScaler/dlssnr/VulkanNrStreamline.h"
#include "../OptiScaler/dlssnr/VulkanNrFinalColor.h"
#include "../OptiScaler/dlssnr/VulkanNrCapabilities.h"
#include "fixtures/vulkan_nr_test_params.h"
#include <cstdio>
using namespace DlssNr;
static int failures=0;
static void Check(bool value,const char* message){if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
template<class S> bool Admits(const S& source,VkNrRecordingOwner& owner,VkCommandBuffer cb,const VkNrEvaluationIdentity& expected){
 if constexpr(requires{VkNrFinalColorAdmits(source,owner,cb,8,0,1,source.observation.gameInput,expected);})
  return VkNrFinalColorAdmits(source,owner,cb,8,0,1,source.observation.gameInput,expected);
 else return VkNrFinalColorAdmits(source,owner,cb,8,0,1,source.observation.gameInput);
}
int main(){
 StrictParams params;params.Set("DLSSG.BackbufferFrameID",0ull);
 VkNrTemporalMetadata temporal;temporal.featureGeneration=42;temporal.frameToken=27;
 AuthenticateVkNrNgxFrame(&params,temporal);
 Check(!temporal.providerFrameKnown&&temporal.frameToken==27&&temporal.featureGeneration==42,"private scalar cannot replace local evaluation identity or grant provider rights");
 auto cb=reinterpret_cast<VkCommandBuffer>(2);auto device=reinterpret_cast<VkDevice>(1);auto queue=reinterpret_cast<VkQueue>(3);
 {VkNrStreamlineSourceScope publicSource(VkNrPublicFrame{8,0,1,cb});
  VkNrStreamlineSourceScope::Authenticate(cb,temporal);AuthenticateVkNrNgxFrame(&params,temporal);
  Check(temporal.providerFrameKnown&&temporal.providerGeneration==8&&temporal.frameToken==0&&temporal.viewport==1,"genuine public token zero survives private observation");}
 auto capabilities=BuildVkNrCapabilities({});
 Check(!capabilities.nativeFg.available&&!capabilities.nativeFg.pending&&capabilities.nativeFg.reason.find("release")!=std::string::npos,"native FG reports missing adapter contract, not pending frame observation");
 VkNrRecordingOwner recordings;recordings.OnAllocate(device,reinterpret_cast<VkCommandPool>(4),cb,0);
 recordings.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 auto use=*recordings.Reserve(cb,1);VkNrEvaluationIdentity evaluation{cb,recordings.Incarnation(cb),7,42};
 VkNrFinalColorSource source;source.selectedSrSucceeded=true;source.observation.use=use;
 source.observation.gameInput=reinterpret_cast<VkImage>(5);source.observation.frame.evaluation=evaluation;source.observation.frame.temporal=temporal;
 Check(Admits(source,recordings,cb,evaluation),"exact selected public evaluation admits with no guessed queue");
 auto wrong=evaluation;++wrong.invocation;Check(!Admits(source,recordings,cb,wrong),"same frame and command buffer cannot substitute a different selected invocation");
 ++source.observation.frame.evaluation.featureGeneration;Check(!Admits(source,recordings,cb,evaluation),"different feature incarnation cannot authorize final color");source.observation.frame.evaluation=evaluation;
 ++source.observation.frame.evaluation.incarnation;Check(!Admits(source,recordings,cb,evaluation),"different command incarnation cannot authorize final color");source.observation.frame.evaluation=evaluation;
 source.observation.frame.evaluation={};Check(!Admits(source,recordings,cb,evaluation),"missing selected evaluation identity is refused");source.observation.frame.evaluation=evaluation;
 VulkanNrPreFg fg(recordings);VkNrContribution c;c.producer=use;c.providerGeneration=8;c.viewport=1;c.contentRevision=9;
 c.image=reinterpret_cast<VkImage>(6);c.view=reinterpret_cast<VkImageView>(7);c.subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.layout=VK_IMAGE_LAYOUT_GENERAL;
 c.frame=source.observation.frame;c.frame.deviceGeneration=1;c.frame.queueFamily=0;
 c.chain.requested=c.chain.completed=2;c.chain.deliverable=true;c.chain.outputVersion=9;c.chain.recording.outputWriteRecorded=true;
 Check(fg.Publish(c),"authentic selected same-recording publication needs no inferred queue");
 VkNrFgConsumer consumer{8,0,1,VK_NULL_HANDLE,cb,c.image,9,c.subresource,c.layout,2};
 auto bound=fg.Bind(consumer);Check(bool(bound),"same recording consumer binds before actual queue is known");
 auto other=reinterpret_cast<VkCommandBuffer>(8);recordings.OnAllocate(device,reinterpret_cast<VkCommandPool>(4),other,0);recordings.OnBegin(other,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 consumer.commandBuffer=other;Check(!fg.Bind(consumer),"null queue never grants an unsubmitted cross-command-buffer handoff");
 if(bound){fg.ConsumerAccepted(*bound);fg.ConsumerReleased(*bound);recordings.OnEnd(cb,VK_SUCCESS);
  auto submission=*recordings.PrepareSubmission({queue,1,*recordings.SnapshotUses(cb)});recordings.SubmissionReturned(submission,VK_SUCCESS);recordings.CheckpointReturned(submission,VK_SUCCESS);
  fg.ObserveSubmissions();recordings.OnReset(cb,VK_SUCCESS);fg.RetireCompleted();Check(fg.ConsumerCount()==1,"null-queue recording retains input while actual submitted GPU copy is incomplete");
  recordings.ObserveFence(submission,VK_SUCCESS);fg.RetireCompleted();Check(fg.ConsumerCount()==0,"actual observed completion and recording release retire deferred queue binding");}
 recordings.OnBegin(cb,0,VK_SUCCESS);source.observation.use=*recordings.Reserve(cb,1);evaluation.incarnation=recordings.Incarnation(cb);source.observation.frame.evaluation=evaluation;
 Check(!Admits(source,recordings,cb,evaluation),"replayable profile is refused before any final-color preparation");
 recordings.OnReset(cb,VK_SUCCESS);recordings.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 source.observation.use=*recordings.Reserve(cb,1);evaluation.incarnation=recordings.Incarnation(cb);source.observation.frame.evaluation=evaluation;
 recordings.OnUnsupportedBindings(cb);
 Check(!Admits(source,recordings,cb,evaluation),"unrestorable command bindings refuse before final-color preparation or GPU mutation");
 if(!failures)std::puts("private capability isolation, exact final-color evaluation and deferred actual queue ownership verified");return failures?1:0;
}
