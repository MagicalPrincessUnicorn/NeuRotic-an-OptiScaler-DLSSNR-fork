#include "../OptiScaler/dlssnr/VulkanPresentGuides.h"
#include <cstdio>
using namespace DlssNr;
namespace GuidePacketFixture {
static int failures=0;
static void Check(bool value,const char* message){if(!value){++failures;std::fprintf(stderr,"FAIL: %s\n",message);}}
template<class G> void Complete(G& guides,const VkNrEvaluationIdentity& id,bool ok){if constexpr(requires{guides.CompleteEvaluation(id,ok);})guides.CompleteEvaluation(id,ok);}
template<class G> auto Select(G& guides,const VkNrFrameContract& f,VkImage output,VkCommandBuffer cb){
 if constexpr(requires{guides.SelectRecording(f,cb,output);})return guides.SelectRecording(f,cb,output);
 else return guides.SelectRecording(f,cb);
}
template<class I> void Sharing(I& images){if constexpr(requires{images.concurrentFamilies;})images.concurrentFamilies={0,1};}
static NVSDK_NGX_Resource_VK Image(uintptr_t handle){NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;r.Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(handle);return r;}
int Run(){
 VkNrRecordingOwner owner;auto device=reinterpret_cast<VkDevice>(1);auto pool=reinterpret_cast<VkCommandPool>(2);
 auto producer=reinterpret_cast<VkCommandBuffer>(3),consumer=reinterpret_cast<VkCommandBuffer>(4),otherFamily=reinterpret_cast<VkCommandBuffer>(5);
 auto queueA=reinterpret_cast<VkQueue>(6),queueB=reinterpret_cast<VkQueue>(7);
 for(auto cb:{producer,consumer,otherFamily}){owner.OnAllocate(device,pool,cb,cb==otherFamily?1:0);owner.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);}
 auto use=*owner.ReserveCopy(producer,1);unsigned copies=0,released=0;
 VulkanPresentGuides guides(owner,[&](const VkFrameRequest&,VkNrUseId,uint64_t){++copies;auto images=std::make_shared<VkNrGuideImages>();images->depth=Image(10);images->motion=Image(11);images->bytes=64;Sharing(*images);return VkNrGuideCreation{images,true,true,{}};},[&](VkNrGuideImages&){++released;return true;});
 auto output=Image(12);VkFrameRequest request;request.device=device;request.commandBuffer=producer;request.targetColor=&output;
 auto& f=request.contract;f.deviceGeneration=1;f.queueFamily=0;f.output=f.work={32,32};f.representation={VK_FORMAT_R8G8B8A8_UNORM,VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,1};
 f.evaluation={producer,owner.Incarnation(producer),1,42};f.temporal=VkNrTemporalMetadata{};auto& t=*f.temporal;
 t.color=t.depth=t.motion={0,0,32,32};t.featureGeneration=42;t.providerFrameKnown=true;t.providerGeneration=8;t.frameToken=0;t.viewport=1;
 Check(guides.Capture(request,use)&&copies==1,"exact evaluation captures without queue or acquired-image guess");
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,producer),"copied guides cannot publish before selected SR success");
 auto wrong=f.evaluation;++wrong.invocation;Complete(guides,wrong,true);
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,producer),"success for another evaluation cannot publish this packet");
 Complete(guides,f.evaluation,true);auto local=Select(guides,f,output.Resource.ImageViewInfo.Image,producer);
 Check(bool(local),"successful exact selected evaluation publishes same-recording packet");
 auto changed=f;++changed.temporal->providerGeneration;Check(!Select(guides,changed,output.Resource.ImageViewInfo.Image,producer),"provider generation mismatch refuses guide packet");
 changed=f;++changed.temporal->viewport;Check(!Select(guides,changed,output.Resource.ImageViewInfo.Image,producer),"viewport mismatch refuses guide packet");
 changed=f;++changed.evaluation.invocation;Check(!Select(guides,changed,output.Resource.ImageViewInfo.Image,producer),"invocation mismatch refuses guide packet");
 Check(!Select(guides,f,reinterpret_cast<VkImage>(13),producer),"reused frame token with different selected output refuses guide packet");
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,consumer),"cross-command-buffer packet refuses before producer submission");
 owner.OnEnd(producer,VK_SUCCESS);auto submission=*owner.PrepareSubmission({queueA,1,{use}});owner.SubmissionReturned(submission,VK_SUCCESS);owner.CheckpointReturned(submission,VK_SUCCESS);
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,consumer),"accepted submission alone cannot authorize crossqueue reads");
 owner.ObserveFence(submission,VK_SUCCESS);local.reset();guides.BeginPresent(queueA);guides.RetireCompleted(owner);
 Check(released==0,"successful packet publication survives physical Present and completed producer before consumer selection");
 auto remote=Select(guides,f,output.Resource.ImageViewInfo.Image,consumer);
 Check(bool(remote),"actual completed producer permits exact packet on another recording");
 auto family=Select(guides,f,output.Resource.ImageViewInfo.Image,otherFamily);Check(bool(family),"concurrent private guide allocation permits listed destination family");
 auto consumerUse=*owner.ReserveCopy(consumer,1);if(remote)Check(guides.Bind(*remote,consumerUse,queueB),"actual completion admits same-family different queue without an invented wait");
 if(family)Check(guides.Bind(*family,*owner.ReserveCopy(otherFamily,1),queueB),"listed concurrent family binds after host-observed producer completion");
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,consumer),"accepted consumer consumes publication without permitting duplicate NR selection");
 auto unlisted=reinterpret_cast<VkCommandBuffer>(20);owner.OnAllocate(device,pool,unlisted,2);owner.OnBegin(unlisted,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
 Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,unlisted),"unlisted queue family cannot inherit concurrent rights");
 auto physical=f;physical.evaluation={};physical.temporal.reset();physical.queue=queueB;physical.swapchain=reinterpret_cast<VkSwapchainKHR>(21);physical.acquireGeneration=1;physical.swapchainImageIndex=0;
 Check(!guides.Select(physical,guides.BeginPresent(queueB).get()),"physical Present without exact source association never inherits guides from an interval");
 Complete(guides,f.evaluation,false);Check(!Select(guides,f,output.Resource.ImageViewInfo.Image,consumer),"failed selected evaluation revokes future packet selection");
 guides.PresentObserved();guides.RetireCompleted(owner);Check(released==0,"accepted consumers retain private guides after packet revocation");
 owner.OnReset(consumer,VK_SUCCESS);owner.OnReset(otherFamily,VK_SUCCESS);guides.RetireCompleted(owner);
 Check(released==0,"selected leases pin completed images across selection and consumer binding");
 local.reset();remote.reset();family.reset();guides.RetireCompleted(owner);Check(released==1,"guides retire after selected leases and all accepted recordings release");
 {
  VkNrRecordingOwner bounded;unsigned made=0,freed=0;VulkanPresentGuides packets(bounded,[&](const VkFrameRequest&,VkNrUseId,uint64_t){
   ++made;auto images=std::make_shared<VkNrGuideImages>();images->depth=Image(100+made*2);images->motion=Image(101+made*2);images->bytes=64;return VkNrGuideCreation{images,true,true,{}};
  },[&](VkNrGuideImages&){++freed;return true;});
  auto last=request;auto emit=[&](uint64_t serial){auto cb=reinterpret_cast<VkCommandBuffer>(100+serial);bounded.OnAllocate(device,pool,cb,0);bounded.OnBegin(cb,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
   last.commandBuffer=cb;last.contract.evaluation={cb,bounded.Incarnation(cb),serial,42};last.contract.temporal->frameToken=serial;
   auto captureUse=*bounded.ReserveCopy(cb,1);Check(packets.Capture(last,captureUse),"bounded completed publication pressure recovers without an authentic physical Present consumer");
   Complete(packets,last.contract.evaluation,true);bounded.OnEnd(cb,VK_SUCCESS);auto submitted=*bounded.PrepareSubmission({queueA,1,{captureUse}});
   bounded.SubmissionReturned(submitted,VK_SUCCESS);bounded.CheckpointReturned(submitted,VK_SUCCESS);bounded.ObserveFence(submitted,VK_SUCCESS);
  };
  for(uint64_t frame=1;frame<=20;++frame)emit(frame);
  Check(packets.Count()==8&&made==20&&freed==12,"twenty completed unconsumed packets remain bounded at eight and recover capacity");
  auto destination=reinterpret_cast<VkCommandBuffer>(200);bounded.OnAllocate(device,pool,destination,0);bounded.OnBegin(destination,VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,VK_SUCCESS);
  auto held=Select(packets,last.contract,output.Resource.ImageViewInfo.Image,destination);Check(bool(held),"later authentic consumer can select latest packet after eviction pressure");
  if(held)Check(packets.Bind(*held,*bounded.ReserveCopy(destination,1),queueB),"later consumer keeps actual recording lifetime under pressure");
  const auto heldImages=held?held->images:nullptr;
  for(uint64_t frame=21;frame<=32;++frame)emit(frame);
  Check(held&&held->images==heldImages&&packets.Count()==8,"pressure never retires selected or accepted unfinished consumer images");
  held.reset();bounded.OnReset(destination,VK_SUCCESS);packets.RejectNative("fixture revoke");packets.RetireCompleted(bounded);
  Check(packets.Count()==0&&freed==made,"revoked completed packets and released consumers retire all private allocations");
 }
 if(!failures)std::puts("immutable successful evaluation guide packet, exact public identity and completed producer transport verified");return failures?1:0;
}
}
#ifndef VULKAN_GUIDE_PACKET_EMBEDDED
int main(){return GuidePacketFixture::Run();}
#endif
