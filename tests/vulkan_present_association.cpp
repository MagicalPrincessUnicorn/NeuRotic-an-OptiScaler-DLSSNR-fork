#include "../OptiScaler/dlssnr/VulkanPresentGuides.h"
#include <cstdio>
using namespace DlssNr;
namespace {
int failures=0;
void Check(bool ok,const char* why){if(!ok){++failures;std::fprintf(stderr,"FAIL: %s\n",why);}}
template<class G> void Exercise(){
 if constexpr(!requires{typename G::PresentTag;}){
  Check(false,"public backbuffer tags must connect exact source guides to physical Present");
 }else{
  VkNrRecordingOwner owner;
  auto device=reinterpret_cast<VkDevice>(1);auto pool=reinterpret_cast<VkCommandPool>(2);
  auto producer=reinterpret_cast<VkCommandBuffer>(3),consumer=reinterpret_cast<VkCommandBuffer>(4);
  auto queue=reinterpret_cast<VkQueue>(5);auto image=reinterpret_cast<VkImage>(6);
  for(auto cb:{producer,consumer}){owner.OnAllocate(device,pool,cb,0);owner.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);}
  auto makeImage=[](uintptr_t h){NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;r.Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(h);return r;};
  unsigned released=0;
  G guides(owner,[&](const VkFrameRequest&,VkNrUseId,uint64_t){auto i=std::make_shared<VkNrGuideImages>();i->depth=makeImage(7);i->motion=makeImage(8);i->bytes=64;return VkNrGuideCreation{i,true,true,{}};},[&](VkNrGuideImages&){++released;return true;});
  auto output=makeImage(9);VkFrameRequest request;request.device=device;request.commandBuffer=producer;request.targetColor=&output;
  auto& f=request.contract;f.deviceGeneration=1;f.queueFamily=0;f.output=f.work={32,32};f.representation={VK_FORMAT_R16G16B16A16_SFLOAT,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1};
  f.evaluation={producer,owner.Incarnation(producer),1,42};f.temporal=VkNrTemporalMetadata{};
  auto& t=*f.temporal;t.color=t.depth=t.motion={0,0,32,32};t.featureGeneration=42;t.providerFrameKnown=true;t.providerGeneration=8;t.frameToken=0;t.viewport=1;
  VkNrFrameContract present;present.deviceGeneration=1;present.swapchainGeneration=2;present.swapchain=reinterpret_cast<VkSwapchainKHR>(10);
  present.swapchainImageIndex=0;present.acquireGeneration=3;present.queue=queue;present.queueFamily=0;present.output={32,32};
  present.representation={VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_COLOR_SPACE_HDR10_ST2084_EXT,2};
  typename G::PresentTag tag;tag.providerGeneration=8;tag.frameToken=0;tag.viewport=1;tag.target=present;tag.image=image;
  Check(guides.ObservePresentTag(tag),"tag may arrive before selected SR evaluation");
  auto use=*owner.ReserveCopy(producer,1);Check(guides.Capture(request,use),"capture authenticated guide packet");
  auto snapshot=guides.BeginPresent(queue);
  Check(!guides.Select(present,snapshot.get(),image),"tag cannot publish unsuccessful evaluation");
  guides.CompleteEvaluation(f.evaluation,true);
  Check(!guides.Select(present,snapshot.get(),image),"successful SR without actual completion cannot permit private guide reads");
  owner.OnEnd(producer,VK_SUCCESS);auto submission=*owner.PrepareSubmission({queue,1,{use}});
  owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
  Check(!guides.Select(present,snapshot.get(),image),"submission return is not producer completion");
  auto submittedEntry=guides.BeginPresent(queue);
  auto ordered=guides.Select(present,submittedEntry.get(),image);
  Check(ordered&&!ordered->completedProducer,"exact earlier accepted submission on consumer queue admits ordered guide transport without inventing completion");
  auto otherQueue=present;otherQueue.queue=reinterpret_cast<VkQueue>(99);
  Check(!guides.Select(otherQueue,submittedEntry.get(),image),"pending producer on another queue cannot use same-queue transport");
  if(ordered){ordered->consumerCommand=consumer;Check(!guides.Bind(*ordered,*owner.ReserveCopy(consumer,1),otherQueue.queue),"ordered transport must refuse consumer queue substitution");}
  owner.ObserveFence(submission,VK_SUCCESS);
  Check(!guides.Select(present,snapshot.get(),image),"an evaluation published after Present entry cannot enter its frozen source selection");
  snapshot=guides.BeginPresent(queue);
  auto lease=guides.Select(present,snapshot.get(),image);
  Check(bool(lease),"actual public frame including zero connects completed guides across tonemapped format change");
  Check(lease&&lease->contract.evaluation==f.evaluation&&lease->selectionHold,"physical selection retains exact source evaluation and lifetime");
  auto recreated=tag;
  for(uint64_t generation=10;generation<90;++generation){
   recreated.target.swapchainGeneration=generation;
   recreated.target.swapchain=reinterpret_cast<VkSwapchainKHR>(1000+generation);
   recreated.target.acquireGeneration=generation;
   recreated.image=reinterpret_cast<VkImage>(2000+generation);
   Check(guides.ObservePresentTag(recreated),"bounded tag pressure admits newly recreated swapchains beyond 64 observations");
  }
  Check(guides.Select(recreated.target,guides.BeginPresent(queue).get(),recreated.image).has_value(),"newest exact swapchain remains usable after scalar tag eviction");
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"oldest live tag association expires under bounded metadata pressure");
  Check(guides.Select(present,snapshot.get(),image).has_value(),"scalar tag eviction cannot alter an already frozen Present or its guide lifetime");
  guides.RevokePresentTags(8,1);Check(guides.ObservePresentTag(tag),"original still-live swapchain can publish a fresh explicit tag after pressure");
  auto wrong=present;++wrong.acquireGeneration;Check(!guides.Select(wrong,snapshot.get(),image),"later acquisition cannot revive prior frame tag");
  wrong=present;++wrong.swapchainGeneration;Check(!guides.Select(wrong,snapshot.get(),image),"recreated swapchain rejects old image association");
  wrong=present;++wrong.deviceGeneration;Check(!guides.Select(wrong,snapshot.get(),image),"new device rejects old image association");
  wrong=present;++wrong.queueFamily;Check(!guides.Select(wrong,snapshot.get(),image),"private exclusive guides cannot advertise a different consumer queue family");
  Check(!guides.Select(present,snapshot.get(),reinterpret_cast<VkImage>(11)),"different physical image refuses despite equal extent");
  guides.RevokePresentTags(8,1);
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"null or failed tag revokes future selection");
  Check(guides.Select(present,snapshot.get(),image).has_value(),"immutable Present entry survives later CPU tag changes");
  auto changed=tag;++changed.frameToken;Check(guides.ObservePresentTag(changed),"new frame tag observed");
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"different public frame does not borrow last guide packet");
  Check(guides.ObservePresentTag(tag),"tag arriving after source success also associates");
  Check(guides.Select(present,guides.BeginPresent(queue).get(),image).has_value(),"successful source is available to later tag");
  auto competing=tag;++competing.viewport;Check(guides.ObservePresentTag(competing),"second viewport may independently tag same backbuffer");
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"conflicting viewport cannot disappear merely because its source has no guide packet");
  guides.RevokePresentTags(8,competing.viewport);
  auto other=producer;other=reinterpret_cast<VkCommandBuffer>(12);owner.OnAllocate(device,pool,other,0);owner.OnBegin(other,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
  auto second=request;auto secondOutput=makeImage(13);second.targetColor=&secondOutput;second.commandBuffer=other;second.contract.evaluation={other,owner.Incarnation(other),2,42};
  Check(guides.Capture(second,*owner.ReserveCopy(other,1)),"second public-frame evaluation observed");guides.CompleteEvaluation(second.contract.evaluation,true);
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"multiple evaluations in one public frame are ambiguous");
  tag.selectedOutput=output.Resource.ImageViewInfo.Image;Check(guides.ObservePresentTag(tag),"explicit selected scaling output supplied by matching public tags");
  auto exact=guides.Select(present,guides.BeginPresent(queue).get(),image);Check(bool(exact),"explicit selected output disambiguates two source evaluations");
  auto frozenExact=guides.BeginPresent(queue);
  auto third=request;auto thirdCommand=reinterpret_cast<VkCommandBuffer>(14);owner.OnAllocate(device,pool,thirdCommand,0);owner.OnBegin(thirdCommand,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
  third.commandBuffer=thirdCommand;third.contract.evaluation={thirdCommand,owner.Incarnation(thirdCommand),3,42};
  Check(guides.Capture(third,*owner.ReserveCopy(thirdCommand,1)),"another invocation can target the same scaling output");
  guides.CompleteEvaluation(third.contract.evaluation,true);
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"selected output cannot distinguish two evaluations writing that same image");
  Check(guides.Select(present,frozenExact.get(),image).has_value(),"later CPU source evaluation cannot alter immutable entry selection");
  if(exact){exact->consumerCommand=consumer;Check(guides.Bind(*exact,*owner.ReserveCopy(consumer,1),queue),"actual physical consumer records completed guide binding");}
  Check(!guides.Select(present,guides.BeginPresent(queue).get(),image),"accepted binding consumes publication");
  lease.reset();exact.reset();ordered.reset();submittedEntry.reset();snapshot.reset();frozenExact.reset();owner.OnReset(consumer,VK_SUCCESS);owner.OnReset(other,VK_SUCCESS);owner.OnReset(thirdCommand,VK_SUCCESS);guides.RejectNative("fixture complete");guides.PresentObserved();guides.RetireCompleted(owner);
  Check(released==3,"association metadata does not retain retired private images");
  {
   unsigned transportReleased=0;
   G transport(owner,[&](const VkFrameRequest&,VkNrUseId,uint64_t){auto i=std::make_shared<VkNrGuideImages>();i->depth=makeImage(20);i->motion=makeImage(21);i->bytes=64;return VkNrGuideCreation{i,true,true,{}};},[&](VkNrGuideImages&){++transportReleased;return true;});
   auto pendingCommand=reinterpret_cast<VkCommandBuffer>(22);
   owner.OnAllocate(device,pool,pendingCommand,0);owner.OnBegin(pendingCommand,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
   owner.OnBegin(consumer,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
   auto pendingRequest=request;pendingRequest.commandBuffer=pendingCommand;pendingRequest.contract.evaluation={pendingCommand,owner.Incarnation(pendingCommand),4,42};
   auto pendingUse=*owner.ReserveCopy(pendingCommand,1);Check(transport.Capture(pendingRequest,pendingUse),"capture source for real pending transport");
   transport.CompleteEvaluation(pendingRequest.contract.evaluation,true);owner.OnEnd(pendingCommand,VK_SUCCESS);
   auto pendingSubmit=*owner.PrepareSubmission({queue,1,{pendingUse}});owner.SubmissionReturned(pendingSubmit,VK_SUCCESS);owner.CheckpointReturned(pendingSubmit,VK_SUCCESS);
   Check(transport.ObservePresentTag(tag),"associate pending transport tag");
   auto entry=transport.BeginPresent(queue);auto pending=transport.Select(present,entry.get(),image);
   Check(pending&&pending->orderedProducer&&!pending->completedProducer&&!owner.GpuComplete(pendingUse),"pending transport explicitly records ordering without false completion");
   if(pending){pending->consumerCommand=consumer;Check(transport.Bind(*pending,*owner.ReserveCopy(consumer,1),queue),"pending same-queue guide consumer binds to real earlier accepted source");}
   entry.reset();pending.reset();transport.PresentObserved();owner.OnReset(consumer,VK_SUCCESS);transport.RetireCompleted(owner);
   Check(transportReleased==0,"consumer reset cannot free still-running source guide writes");
   owner.ObserveFence(pendingSubmit,VK_SUCCESS);transport.RetireCompleted(owner);
   Check(transportReleased==1,"ordered source retires only after actual completion and consumer release");
  }
 }
}
}
int main(){Exercise<VulkanPresentGuides>();if(!failures)std::puts("public tagged Present association, acquisition isolation, completion and guide lifetime verified");return failures?1:0;}
