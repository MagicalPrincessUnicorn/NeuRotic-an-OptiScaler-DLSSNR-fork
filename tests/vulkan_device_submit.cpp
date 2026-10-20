#include <windows.h>
#include "../OptiScaler/dlssnr/VulkanNrCompletion.h"
#include "../OptiScaler/dlssnr/VulkanNrDeviceSubmitHooks.h"
#include "../OptiScaler/dlssnr/VulkanPresentRegistry.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool value,const char* label){if(!value){std::fprintf(stderr,"FAIL: %s\n",label);std::exit(1);}}
static int calls=0;
static const VkSubmitInfo* expected=nullptr;
static const VkSubmitInfo2* expected2=nullptr;
static VkResult driverResult=VK_SUCCESS;
static VkResult VKAPI_CALL CreateFence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* fence)
{static uintptr_t next=100;*fence=reinterpret_cast<VkFence>(next++);return VK_SUCCESS;}
static void VKAPI_CALL DestroyFence(VkDevice,VkFence,const VkAllocationCallbacks*){}
static VkResult VKAPI_CALL FenceStatus(VkDevice,VkFence){return VK_SUCCESS;}
static VkResult VKAPI_CALL Checkpoint(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence)
{Check(count==1&&info->commandBufferCount==0,"checkpoint has no application commands");return VK_SUCCESS;}
__declspec(noinline) static VkResult VKAPI_CALL DriverSubmit(VkQueue,uint32_t count,const VkSubmitInfo* info,VkFence)
{++calls;Check(count==1&&info==expected,"exact original submission pointer forwarded");return driverResult;}
__declspec(noinline) static VkResult VKAPI_CALL DriverSubmit2(VkQueue,uint32_t count,const VkSubmitInfo2* info,VkFence)
{++calls;Check(count==1&&info==expected2,"exact original Submit2 pointer forwarded");return driverResult;}
__declspec(noinline) static VkResult VKAPI_CALL TranslatingLayer(VkQueue queue,uint32_t count,const VkSubmitInfo* info,VkFence fence)
{Check(count==1&&info==expected,"translation receives original application pointer");return DriverSubmit2(queue,1,expected2,fence);}
int main(){
 auto device=reinterpret_cast<VkDevice>(1);auto queue=reinterpret_cast<VkQueue>(2);
 auto cb=reinterpret_cast<VkCommandBuffer>(3);auto pool=reinterpret_cast<VkCommandPool>(4);
 RegisterVkNrCompletionDevice(device,{CreateFence,DestroyFence,FenceStatus,Checkpoint});
 RegisterVkNrCompletionQueue(device,queue,0);
 auto& owner=VulkanNrRecordings();owner.OnAllocate(device,pool,cb,0);owner.OnBegin(cb,0,VK_SUCCESS);
 auto use=owner.ReserveCopy(cb,VkNrCompletionDeviceGeneration(device));Check(use.has_value(),"guide use reserved");owner.OnEnd(cb,VK_SUCCESS);
 // A cached device address bypasses loader exports and subsequent proc lookups.
 volatile PFN_vkQueueSubmit cached=DriverSubmit;
 Check(VkNrDeviceSubmit::Install(DriverSubmit)==VkNrDeviceSubmit::Result::Installed,"device address hooked");
 Check(VkNrDeviceSubmit::Install(DriverSubmit)==VkNrDeviceSubmit::Result::AlreadyCovered,"same target is not rehooked");
 VkSubmitInfo info{VK_STRUCTURE_TYPE_SUBMIT_INFO};info.commandBufferCount=1;info.pCommandBuffers=&cb;expected=&info;
 Check(cached(queue,1,&info,VK_NULL_HANDLE)==VK_SUCCESS&&calls==1,"cached driver submission succeeds");
 Check(owner.ProducerProof(*use).has_value(),"cached device submission must establish guide proof");
 const auto fresh=[&]{owner.OnBegin(cb,0,VK_SUCCESS);auto next=owner.ReserveCopy(cb,VkNrCompletionDeviceGeneration(device));Check(next.has_value(),"next use reserved");owner.OnEnd(cb,VK_SUCCESS);return *next;};
 Check(VkNrDeviceSubmit2::Install(DriverSubmit2)==VkNrDeviceSubmit2::Result::Installed,"direct Submit2 installed");
 Check(VkNrDeviceSubmit2::Install(DriverSubmit2)==VkNrDeviceSubmit2::Result::AlreadyCovered,"core/KHR shared address uses one trampoline");
 VkCommandBufferSubmitInfo cbInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};cbInfo.commandBuffer=cb;
 VkSubmitInfo2 info2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};info2.commandBufferInfoCount=1;info2.pCommandBufferInfos=&cbInfo;expected2=&info2;
 volatile PFN_vkQueueSubmit2 cached2=DriverSubmit2;
 auto direct2=fresh();Check(cached2(queue,1,&info2,VK_NULL_HANDLE)==VK_SUCCESS&&owner.ProducerProof(direct2).has_value(),"direct Submit2 proves producer");
 {
  auto& registry=GetVulkanLoaderPresentRegistry();
  registry.DeviceCreated(device,reinterpret_cast<VkPhysicalDevice>(81),true,true,{0});
  registry.QueueObserved(device,queue,0,VK_QUEUE_GRAPHICS_BIT);
  const auto swap=reinterpret_cast<VkSwapchainKHR>(82);const auto acquire=reinterpret_cast<VkSemaphore>(83);
  VkSwapchainCreateInfoKHR create{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
  create.imageExtent={64,64};create.imageArrayLayers=1;create.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
  registry.SwapchainCreated(device,swap,create,create.imageUsage,true,{reinterpret_cast<VkImage>(84)},{0});
  VkSemaphoreSubmitInfo wait{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};wait.semaphore=acquire;wait.stageMask=VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
  info2.commandBufferInfoCount=0;info2.waitSemaphoreInfoCount=1;info2.pWaitSemaphoreInfos=&wait;
  for(uint64_t ignoredValue:{0ull,777ull}){
   wait.value=ignoredValue;registry.ImageAcquired(device,swap,0,acquire);
   const auto proof=registry.AcquireUse(swap,0);
   Check(cached2(queue,1,&info2,VK_NULL_HANDLE)==VK_SUCCESS,"binary Submit2 wait preserves exact application submission");
   PollVkNrCompletions();
   Check(owner.GpuComplete(proof),"binary acquisition Submit2 ignores its value field when matching waits");
  }
  info2.commandBufferInfoCount=1;info2.waitSemaphoreInfoCount=0;info2.pWaitSemaphoreInfos=nullptr;
  registry.DeviceDestroyed(device);
 }
 auto nested=fresh();
 {VkNrObservationScope outer(VkNrObservation::Submit2,reinterpret_cast<uintptr_t>(queue));
  Check(outer.Observe(),"loader observation selected");
  ObserveVkNrQueueSubmit(queue,{&cb,1},[&]{return cached2(queue,1,&info2,VK_NULL_HANDLE);});}
 Check(owner.ProducerProof(nested).has_value(),"loader and direct detour observe only once");
 Check(VkNrDeviceSubmit::Install(TranslatingLayer)==VkNrDeviceSubmit::Result::Installed,"second legacy target keeps separate trampoline");
 volatile PFN_vkQueueSubmit translating=TranslatingLayer;
 auto translated=fresh();Check(translating(queue,1,&info,VK_NULL_HANDLE)==VK_SUCCESS&&owner.ProducerProof(translated).has_value(),"legacy-to-Submit2 layer observes once");
 auto cross=fresh();
 {VkNrObservationScope outer(VkNrObservation::Submit2,reinterpret_cast<uintptr_t>(queue));
  ObserveVkNrQueueSubmit(queue,{&cb,1},[&]{return cached(queue,1,&info,VK_NULL_HANDLE);});}
 Check(owner.ProducerProof(cross).has_value(),"Submit2 wrapper to legacy device call observes once");
 auto failed=fresh();driverResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
 Check(cached(queue,1,&info,VK_NULL_HANDLE)==driverResult&&!owner.ProducerProof(failed),"driver failure forwarded without producer proof");
 driverResult=VK_SUCCESS;
 auto device2=reinterpret_cast<VkDevice>(11);auto queue2=reinterpret_cast<VkQueue>(12);auto cb2=reinterpret_cast<VkCommandBuffer>(13);
 RegisterVkNrCompletionDevice(device2,{CreateFence,DestroyFence,FenceStatus,Checkpoint});RegisterVkNrCompletionQueue(device2,queue2,0);
 Check(VkNrDeviceSubmit::Install(DriverSubmit)==VkNrDeviceSubmit::Result::AlreadyCovered,"second device shares exact code target");
 owner.OnAllocate(device2,pool,cb2,0);owner.OnBegin(cb2,0,VK_SUCCESS);auto use2=owner.ReserveCopy(cb2,VkNrCompletionDeviceGeneration(device2));owner.OnEnd(cb2,VK_SUCCESS);
 info.pCommandBuffers=&cb2;Check(cached(queue2,1,&info,VK_NULL_HANDLE)==VK_SUCCESS&&owner.ProducerProof(*use2).has_value(),"second device uses its own submission identity");
 Check(owner.ProducerProof(*use2)->deviceGeneration==VkNrCompletionDeviceGeneration(device2),"second device identity preserved");
 Check(VkNrDeviceSubmit::Uninstall(),"device hook removed");
 Check(VkNrDeviceSubmit2::Uninstall(),"Submit2 hooks removed");
 Check(DetourTransactionBegin()==NO_ERROR,"unrelated transaction opened");
 Check(VkNrDeviceSubmit::Install(DriverSubmit)==VkNrDeviceSubmit::Result::Failed,"nested transaction fails without retaining a slot");
 Check(DetourTransactionAbort()==NO_ERROR,"unrelated transaction remains owned by caller");
 Check(VkNrDeviceSubmit::Install(DriverSubmit)==VkNrDeviceSubmit::Result::Installed,"fresh installation after detach");
 Check(VkNrDeviceSubmit::Uninstall(),"fresh hook removed");
 std::puts("cached device submissions, aliases, nesting, cross-API translation, failures and device identities verified");
}
