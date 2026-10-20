#include "../OptiScaler/dlssnr/VulkanNrPreFg.h"
#include "../OptiScaler/dlssnr/VulkanNrFinalColor.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
 VkNrRecordingOwner recordings;auto device=reinterpret_cast<VkDevice>(1);auto cb=reinterpret_cast<VkCommandBuffer>(2);auto queue=reinterpret_cast<VkQueue>(3);
 recordings.OnAllocate(device,reinterpret_cast<VkCommandPool>(4),cb,0);recordings.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 auto use=*recordings.Reserve(cb,1);VulkanNrPreFg owner(recordings);
 VkNrFinalColorSource source;source.selectedSrSucceeded=true;source.observation.use=use;source.observation.gameInput=reinterpret_cast<VkImage>(99);
 source.observation.frame.temporal=VkNrTemporalMetadata{};auto& temporal=*source.observation.frame.temporal;
 source.observation.frame.evaluation={cb,recordings.Incarnation(cb),1,42};temporal.featureGeneration=42;temporal.providerFrameKnown=true;temporal.providerGeneration=8;temporal.frameToken=0;temporal.viewport=1;
 Check(VkNrFinalColorAdmits(source,recordings,cb,8,0,1,source.observation.gameInput,source.observation.frame.evaluation),"authentic selected real frame zero permits the earlier final-color seam");
 Check(!VkNrFinalColorAdmits(source,recordings,cb,8,1,1,source.observation.gameInput,source.observation.frame.evaluation),"generated or unrelated public frame never records another NR chain");
 source.performance=true;source.observation.requestedPasses=4;source.observation.completedPasses=3;
 Check(!VkNrFinalColorAdmits(source,recordings,cb,8,0,1,source.observation.gameInput,source.observation.frame.evaluation),"Performance final color needs the entire genuine pre-SR chain");
 source.observation.completedPasses=4;Check(VkNrFinalColorAdmits(source,recordings,cb,8,0,1,source.observation.gameInput,source.observation.frame.evaluation),"whole Performance chain can flow through successful SR into final color");
 VkNrFinalColorAttempt attempt;Check(attempt.Claim(use)&&!attempt.Claim(use),"one model-chain attempt per authentic selected real frame");
 VkNrContribution c;c.producer=use;c.providerGeneration=8;c.viewport=1;c.contentRevision=4;c.image=reinterpret_cast<VkImage>(5);
 c.view=reinterpret_cast<VkImageView>(6);c.subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.layout=VK_IMAGE_LAYOUT_GENERAL;
 c.frame.deviceGeneration=1;c.frame.queue=queue;c.frame.routeEpoch=1;c.frame.temporal=VkNrTemporalMetadata{};c.frame.temporal->frameToken=10;
 c.chain.requested=4;c.chain.completed=3;c.chain.deliverable=false;
 Check(!owner.Publish(c),"partial chain not publishable");c.chain.completed=4;c.chain.deliverable=true;c.chain.outputVersion=4;c.chain.recording.outputWriteRecorded=true;
 c.frame.queue=VK_NULL_HANDLE;c.frame.queueFamily=0;
 Check(!owner.Publish(c),"family-only Native recording cannot authorize a queue-specific FG handoff");c.frame.queue=queue;
 Check(owner.Publish(c),"complete real frame publication");
 VkNrFgConsumer consumer;consumer.providerGeneration=8;consumer.frameToken=10;consumer.viewport=1;consumer.queue=queue;consumer.commandBuffer=cb;
 consumer.input=c.image;consumer.capturedContentRevision=3;consumer.subresource=c.subresource;consumer.layout=c.layout;consumer.maxGenerated=2;
 Check(!owner.Bind(consumer),"late copy after tag cannot revise captured input");consumer.capturedContentRevision=4;
 consumer.frameToken=11;Check(!owner.Bind(consumer),"exact frame token");consumer.frameToken=10;
 auto bound=owner.Bind(consumer);Check(bool(bound),"same recording has ordered real-frame input");
 Check(!owner.ContinueAccepted(*bound,consumer),"unaccepted provider call cannot authorize retained children");
 owner.ConsumerAccepted(*bound);
 auto child=owner.ContinueAccepted(*bound,consumer);Check(bool(child),"successful original consumer authorizes immutable child input");
 owner.ConsumerReleased(*child);
 owner.RevokeNewContributions(2);
 Check(!owner.ContinueAccepted(*bound,consumer),"a changed route epoch stops new retained children");Check(!owner.Bind(consumer),"route disable revokes new consumers");
 Check(!owner.Generated(*bound,2,2)&&owner.Generated(*bound,2,0)&&owner.Generated(*bound,2,0),"generated child distinct and repeated callback idempotent");
 Check(owner.PrimaryContributions()==1,"generated callbacks never primary NR");
 recordings.OnEnd(cb,VK_SUCCESS);auto uses=*recordings.SnapshotUses(cb);auto submit=*recordings.PrepareSubmission({queue,1,uses});recordings.SubmissionReturned(submit,VK_SUCCESS);recordings.CheckpointReturned(submit,VK_SUCCESS);
 owner.ConsumerSubmitted(*bound,submit);recordings.ObserveFence(submit,VK_SUCCESS);recordings.OnReset(cb,VK_SUCCESS);
 owner.RetireCompleted();Check(owner.ConsumerCount()==1&&!recordings.Reusable(use)&&!recordings.SafeToWriteAfter(use),"producer fence and recording reset do not release consumer");
 owner.ConsumerReleased(*bound);owner.RetireCompleted();Check(owner.ConsumerCount()==0&&recordings.Reusable(use),"actual release and consumer work retirement unblock reuse");
 Check(!VkNrFgKnownOff(VkNrFgActivity::Unknown)&&VkNrFgKnownOff(VkNrFgActivity::Off),"unknown FG never off");
 std::puts("real-frame publication, content revision, consumer obligations and generated-child separation verified");
}
