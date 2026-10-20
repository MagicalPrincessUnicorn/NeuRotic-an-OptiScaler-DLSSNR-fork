#include "../OptiScaler/dlssnr/VulkanNrFfxFg.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
int main(){
 VkNrRecordingOwner r;auto cb=reinterpret_cast<VkCommandBuffer>(2);auto queue=reinterpret_cast<VkQueue>(3);
 r.OnAllocate(reinterpret_cast<VkDevice>(1),reinterpret_cast<VkCommandPool>(4),cb,0);r.OnBegin(cb,0,VK_SUCCESS);auto use=*r.Reserve(cb,1);VulkanNrPreFg fg(r);
 VkNrContribution c;c.producer=use;c.providerGeneration=9;c.contentRevision=42;c.image=reinterpret_cast<VkImage>(5);c.view=reinterpret_cast<VkImageView>(6);
 c.frame.deviceGeneration=1;c.frame.routeEpoch=1;c.frame.queue=queue;c.frame.temporal=VkNrTemporalMetadata{};c.frame.temporal->frameToken=10;
 c.subresource={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.layout=VK_IMAGE_LAYOUT_GENERAL;c.chain.requested=c.chain.completed=1;c.chain.deliverable=true;c.chain.outputVersion=42;c.chain.recording.outputWriteRecorded=true;
 Check(fg.Publish(c),"authentic final color published");VulkanNrFfxFg ffx(r,fg);
 VkNrFfxObservation o;o.consumer={9,10,0,queue,cb,c.image,42,c.subresource,c.layout,1};o.use=use;o.callbackContext=11;o.apiVersion=0x010104;o.succeeded=true;
 Check(!ffx.ObservePrepare(o),"missing versioned release contract refuses preparation");o.releaseContractObserved=true;
 Check(ffx.ObservePrepare(o),"qualified prepare association");auto wrong=o;wrong.consumer.frameToken=11;
 Check(!ffx.ObserveDispatch(wrong),"dispatch frame differs from prepare");auto id=ffx.ObserveDispatch(o);Check(bool(id),"same frame and callback bind");
 Check(!ffx.ObserveCompose(o),"callback entry alone not consumer release");o.releaseObserved=true;Check(ffx.ObserveCompose(o),"actual release settles consumer");
 r.OnEnd(cb,VK_SUCCESS);auto uses=*r.SnapshotUses(cb);auto s=*r.PrepareSubmission({queue,1,uses});r.SubmissionReturned(s,VK_SUCCESS);r.CheckpointReturned(s,VK_SUCCESS);r.ObserveFence(s,VK_SUCCESS);r.OnReset(cb,VK_SUCCESS);
 fg.ObserveSubmissions();ffx.RetireCompleted();Check(fg.ConsumerCount()==0,"actual GPU completion and recording retirement required");
 std::puts("older FFX Vulkan prepare/dispatch/callback association and distinct consumer release verified");
}
