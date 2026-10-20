#include "../OptiScaler/dlssnr/VulkanNrCapabilities.h"
#include "../OptiScaler/dlssnr/VulkanPresentSubmission.h"
#include "../OptiScaler/dlssnr/VulkanPresentRegistry.h"
#include "../OptiScaler/dlssnr/VulkanNrPreFg.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL: %s\n",s);std::exit(1);}}
template<class Registry>
static bool ExactAcquireConsumed(Registry& registry,VkSwapchainKHR swap,uint32_t index,uint64_t generation){
 if constexpr(requires {registry.AcquireConsumed(swap,index,generation);})
  return registry.AcquireConsumed(swap,index,generation);
 else
  return registry.AcquireConsumed(swap,index); // exposes the legacy stale-proof bug before the overload exists
}
int main(){
 auto caps=BuildVkNrCapabilities({});Check(caps.multipass.available&&caps.hdr.available,"retired HDR and multipass opt-in never gate construction");
 Check(caps.performanceCapture.available,"Performance capture adapter is reachable pending actual provider qualification");
 VkNrCapabilityFacts off;off.enabled=false;Check(BuildVkNrCapabilities(off).capture.available,"explicit NR-off diagnostic capture is reachable");
 auto& owner=VulkanNrRecordings();VkNrObservationScope a(VkNrObservation::Present,1);VkNrObservationScope b(VkNrObservation::Present,1);
 Check(a.Observe()&&!b.Observe(),"proc alias wrappers execute Present once");
 VkPresentSlotState slot;slot.Submitted();slot.OriginalPresentReturned(VK_SUCCESS);
 Check(!slot.CanRecord(true,true,false),"acquire return alone not consumption");Check(slot.CanRecord(true,true,true),"actual acquire wait permits signal reuse");
 auto device=reinterpret_cast<VkDevice>(1);auto queue=reinterpret_cast<VkQueue>(2);auto cb=reinterpret_cast<VkCommandBuffer>(3);
 owner.OnAllocate(device,reinterpret_cast<VkCommandPool>(4),cb,0);owner.OnBegin(cb,0,VK_SUCCESS);owner.OnEnd(cb,VK_SUCCESS);
 for(uint32_t count:{2u,3u,4u}){
  VulkanPresentRegistry registry;registry.DeviceCreated(device,reinterpret_cast<VkPhysicalDevice>(5),true,true,{0});registry.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
  std::vector<VkImage> images;for(uint32_t i=0;i<count;++i)images.push_back(reinterpret_cast<VkImage>(uintptr_t(20+i)));
  auto swap=reinterpret_cast<VkSwapchainKHR>(10);VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,images,{0});auto semaphore=reinterpret_cast<VkSemaphore>(30);uint32_t index=count-1;
  registry.ImageAcquired(device,swap,index,semaphore);VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
  auto snapshot=registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);Check(snapshot&&snapshot->capabilities.imageCount==count,"actual returned image count");
  Check(!registry.AcquireConsumed(swap,index),"delayed acquire semaphore remains unconsumed");
  auto uses=registry.PrepareAcquireWait(queue,{&semaphore,1},1);auto proof=registry.AcquireUse(swap,index);Check(bool(proof),"actual submitted wait dependency reserved");
  auto submitted=*owner.PrepareSubmission({queue,1,uses});owner.SubmissionReturned(submitted,VK_SUCCESS);owner.CheckpointReturned(submitted,VK_SUCCESS);
  Check(!registry.AcquireConsumed(swap,index),"queue acceptance not GPU consumption");owner.ObserveFence(submitted,VK_SUCCESS);Check(registry.AcquireConsumed(swap,index),"owned checkpoint proves acquire wait completion");
  Check(ExactAcquireConsumed(registry,swap,index,snapshot->request.acquireGeneration),"exact completed acquire generation qualifies");
  Check(!ExactAcquireConsumed(registry,swap,index,snapshot->request.acquireGeneration+1),"current completion cannot prove another acquisition generation");
  Check(!ExactAcquireConsumed(registry,swap,index,0),"unknown acquisition generation cannot qualify reuse");
 }
 owner.OnReset(cb,VK_SUCCESS);
 // Bounded succession history must always retain the immediate explicit parent,
 // even after a long session with more replacements than the history window.
 {
  VulkanPresentRegistry registry;registry.DeviceCreated(device,reinterpret_cast<VkPhysicalDevice>(5),true,true,{0});registry.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
  VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;ci.imageExtent={64,64};ci.imageArrayLayers=1;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  uint64_t priorGeneration=0;VkSwapchainKHR prior{};uint32_t index=0;
  for(uint32_t replacement=0;replacement<160;++replacement){
   auto swap=reinterpret_cast<VkSwapchainKHR>(uintptr_t(1000+replacement));ci.oldSwapchain=prior;
   registry.SwapchainCreated(device,swap,ci,ci.imageUsage,true,{reinterpret_cast<VkImage>(20)},{0});registry.ImageAcquired(device,swap,index);
   VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
   auto observed=*registry.SnapshotForPresent(queue,pi,true,1,PresentInput::Policy::ImageOnly,false);
   if(priorGeneration)Check(!observed.predecessorGenerations.empty()&&observed.predecessorGenerations.back()==priorGeneration,"long replacement chain preserves the actual immediate predecessor");
   Check(observed.predecessorGenerations.size()<=128,"succession metadata remains bounded");
   registry.SwapchainDestroyed(prior);prior=swap;priorGeneration=observed.request.swapchainGeneration;
  }
 }
 owner.OnBegin(cb,0,VK_SUCCESS);owner.OnUnsupportedBindings(cb);Check(!owner.Reserve(cb,1),"unsupported binding model refuses injection before writes");owner.OnReset(cb,VK_SUCCESS);owner.OnBegin(cb,0,VK_SUCCESS);Check(bool(owner.Reserve(cb,1)),"new recording clears unsupported binding observation");
 std::puts("capability controls, proc aliases, actual image counts and acquire-wait consumption verified");
}
