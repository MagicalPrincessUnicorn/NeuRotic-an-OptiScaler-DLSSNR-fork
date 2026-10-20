#include "PhysicalSwapchain.h"
#include <algorithm>
#include <thread>

namespace DlssNr::NativeFg {
namespace {constexpr uint64_t FenceTimeout=5000000000ull;
bool Accepted(VkResult r){return r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR;}
bool Enqueued(VkResult r){return Accepted(r)||r==VK_ERROR_OUT_OF_DATE_KHR||r==VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT||r==VK_ERROR_SURFACE_LOST_KHR;}
struct QueueScope {LeaveFn leave;QueueScope(EnterFn e,LeaveFn l):leave(l){if(e)e(true);}~QueueScope(){if(leave)leave(true);}};
}
VkResult PhysicalSwapchain::Initialize(const BridgeCreate& input,PFN_vkGetDeviceProcAddr get,EnterFn enter,LeaveFn leave){
 if(!get||!input.device||!input.queue)return VK_ERROR_INITIALIZATION_FAILED;
 device=input.device;queue=input.queue;family=input.family;width=input.info.imageExtent.width;height=input.info.imageExtent.height;
 presentMode=input.info.presentMode;
 enterQueue=enter;leaveQueue=leave;
#define LOAD(field,name) field=reinterpret_cast<decltype(field)>(get(device,"vk" #name));if(!field)return VK_ERROR_EXTENSION_NOT_PRESENT
 LOAD(create,CreateSwapchainKHR);LOAD(destroy,DestroySwapchainKHR);LOAD(getImages,GetSwapchainImagesKHR);
 LOAD(acquire,AcquireNextImageKHR);LOAD(release,ReleaseSwapchainImagesEXT);LOAD(present,QueuePresentKHR);
 LOAD(createPool,CreateCommandPool);LOAD(destroyPool,DestroyCommandPool);LOAD(allocate,AllocateCommandBuffers);LOAD(resetPool,ResetCommandPool);
 LOAD(begin,BeginCommandBuffer);LOAD(end,EndCommandBuffer);LOAD(barrier,CmdPipelineBarrier);LOAD(submit,QueueSubmit);
 LOAD(createSemaphore,CreateSemaphore);LOAD(destroySemaphore,DestroySemaphore);
 LOAD(createFence,CreateFence);LOAD(destroyFence,DestroyFence);LOAD(resetFences,ResetFences);LOAD(waitFences,WaitForFences);
#undef LOAD
 // Allocate all private resources before creating the physical swapchain, so
 // allocation failure cannot retire the caller's old swapchain.
 VkCommandPoolCreateInfo pci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pci.queueFamilyIndex=family;
 auto r=createPool(device,&pci,nullptr,&pool);if(r!=VK_SUCCESS)return r;
 VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
 if((r=allocate(device,&ai,&command))!=VK_SUCCESS)return r;
 VkSemaphoreCreateInfo sci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
 VkFenceCreateInfo fci{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
 for(auto* f:{&acquireFence,&submitFence})if((r=createFence(device,&fci,nullptr,f))!=VK_SUCCESS)return r;
 for(auto& p:presents){
  if((r=createFence(device,&fci,nullptr,&p.fence))!=VK_SUCCESS)return r;
  if((r=createSemaphore(device,&sci,nullptr,&p.ready))!=VK_SUCCESS)return r;
 }
 creationAttempted=true;
 if((r=create(device,&input.info,nullptr,&swap))!=VK_SUCCESS)return r;
 uint32_t count=0;if((r=getImages(device,swap,&count,nullptr))!=VK_SUCCESS||count<2||count>256){unsafe=true;return r==VK_SUCCESS?VK_ERROR_INITIALIZATION_FAILED:r;}
 images.resize(count);if((r=getImages(device,swap,&count,images.data()))!=VK_SUCCESS){unsafe=true;return r;}
 images.resize(count);return VK_SUCCESS;
}
bool PhysicalSwapchain::Wait(VkFence f,bool& pending){
 if(!pending)return true;
 waitKind=f==acquireFence?1:2;lastWait=waitFences(device,1,&f,VK_TRUE,FenceTimeout);
 if(lastWait!=VK_SUCCESS){unsafe=true;return false;}
 pending=false;return true;
}
bool PhysicalSwapchain::PollPresents(){
 bool complete=true;
 for(auto& p:presents)if(p.pending){
  const auto r=waitFences(device,1,&p.fence,VK_TRUE,0);lastWait=r;waitKind=3;
  if(r==VK_TIMEOUT){complete=false;continue;}
  if(r!=VK_SUCCESS){unsafe=true;return false;}
  if(p.accepted){if(p.generated)++generatedRetired;else ++realRetired;}
  p.pending=false;
  if(resetFences(device,1,&p.fence)!=VK_SUCCESS){unsafe=true;return false;}
 }
 return complete;
}
PhysicalSwapchain::PresentTicket* PhysicalSwapchain::FreePresent(const PresentTicket* except){
 for(auto& p:presents)if(!p.pending && &p!=except)return &p;return nullptr;
}
bool PhysicalSwapchain::IdleGpu(){return !unsafe&&Wait(acquireFence,acquirePending)&&Wait(submitFence,submitPending);}
bool PhysicalSwapchain::Drain(){return IdleGpu()&&PollPresents();}
bool PhysicalSwapchain::Bind(VkQueue q,unsigned f){if(!q||f!=family||!IdleGpu())return false;if(q!=queue){queue=q;lastReal={};}return true;}
VkResult PhysicalSwapchain::Images(unsigned* count,VkImage* out){return getImages(device,swap,count,out);}
VkResult PhysicalSwapchain::Acquire(uint64_t t,VkSemaphore s,VkFence f,unsigned* index){std::lock_guard lock(acquisitionGate);return unsafe?VK_ERROR_DEVICE_LOST:acquire(device,swap,t,s,f,index);}
bool PhysicalSwapchain::Release(unsigned index){
 VkReleaseSwapchainImagesInfoEXT ri{VK_STRUCTURE_TYPE_RELEASE_SWAPCHAIN_IMAGES_INFO_EXT};ri.swapchain=swap;ri.imageIndexCount=1;ri.pImageIndices=&index;
 if(release(device,&ri)!=VK_SUCCESS){unsafe=true;return false;}return true;
}
VkResult PhysicalSwapchain::PresentOne(VkQueue q,const VkPresentInfoKHR& input,PresentTicket& ticket,bool generated,bool changed){
 VkSwapchainPresentFenceInfoEXT retirement{VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT};retirement.swapchainCount=1;retirement.pFences=&ticket.fence;
 retirement.pNext=input.pNext;auto pi=input;pi.pNext=&retirement;
 VkResult r;{QueueScope scope(enterQueue,leaveQueue);r=present(q,&pi);}lastPresent=r;
 if(!Enqueued(r)){
  if(!changed&&(r==VK_ERROR_OUT_OF_HOST_MEMORY||r==VK_ERROR_OUT_OF_DEVICE_MEMORY))return r;
  unsafe=true;return changed?VK_ERROR_DEVICE_LOST:r;
 }
 ticket.pending=true;ticket.generated=generated;ticket.accepted=Accepted(r);
 // WSI retirement can require a later presentation or fullscreen transition.
 // The native result must return to the game so it can make that progress.
 PollPresents();
 return r;
}
VkResult PhysicalSwapchain::Present(VkQueue q,const VkPresentInfoKHR& pi,const BridgeFrame& frame){
 if(unsafe||q!=queue)return VK_ERROR_DEVICE_LOST;
 if(pi.swapchainCount!=1||!pi.pSwapchains||pi.pSwapchains[0]!=swap||!pi.pImageIndices||pi.pImageIndices[0]>=images.size()||pi.pNext||
    (pi.waitSemaphoreCount&&!pi.pWaitSemaphores))return VK_ERROR_FEATURE_NOT_PRESENT;
 const auto start=std::chrono::steady_clock::now();
 PollPresents();if(unsafe)return VK_ERROR_DEVICE_LOST;
 auto* realTicket=FreePresent();
 if(!realTicket)return VK_ERROR_OUT_OF_HOST_MEMORY; // no waits consumed, no image submitted
 auto* generatedTicket=FreePresent(realTicket);
 auto real=pi;VkResult generatedResult=VK_SUCCESS;
 bool waitsConsumed=false;
 if(frame.generated&&frame.copy&&generatedTicket){
  if(!IdleGpu()||resetFences(device,1,&acquireFence)!=VK_SUCCESS){unsafe=true;return VK_ERROR_DEVICE_LOST;}
  unsigned extra=0;VkResult ar=VK_NOT_READY;
  {std::unique_lock lock(acquisitionGate,std::try_to_lock);if(lock.owns_lock())ar=acquire(device,swap,0,VK_NULL_HANDLE,acquireFence,&extra);}
  lastAcquire=ar;
  if(Accepted(ar)){
   acquirePending=true;if(!Wait(acquireFence,acquirePending))return VK_ERROR_DEVICE_LOST;
   if(extra>=images.size()||extra==pi.pImageIndices[0]){unsafe=true;return VK_ERROR_DEVICE_LOST;}
   bool recorded=resetPool(device,pool,0)==VK_SUCCESS;
   VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
   recorded=recorded&&begin(command,&bi)==VK_SUCCESS;
   if(recorded){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=images[extra];b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};b.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;b.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    b.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier(command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
    recorded=frame.copy(frame.context,command,images[extra],width,height,frame.frame)==VK_SUCCESS;
    if(recorded&&frame.compose)recorded=frame.compose(frame.composeContext,command,images[extra],width,height,frame.frame)==VK_SUCCESS;
    if(recorded){b.oldLayout=VK_IMAGE_LAYOUT_GENERAL;b.newLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;b.dstAccessMask=0;
     barrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,0,0,nullptr,0,nullptr,1,&b);recorded=end(command)==VK_SUCCESS;}
   }
   if(!recorded){
    // No GPU submission: discard the recording and return the acquired image.
    if(resetPool(device,pool,0)!=VK_SUCCESS||!Release(extra)){unsafe=true;return VK_ERROR_DEVICE_LOST;}
    ++skipped;return PresentOne(q,pi,*realTicket,false);
   }
   if(resetFences(device,1,&submitFence)!=VK_SUCCESS){unsafe=true;return VK_ERROR_DEVICE_LOST;}
   std::vector<VkPipelineStageFlags> stages(pi.waitSemaphoreCount,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
   VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount=pi.waitSemaphoreCount;si.pWaitSemaphores=pi.pWaitSemaphores;si.pWaitDstStageMask=stages.data();
   si.commandBufferCount=1;si.pCommandBuffers=&command;si.signalSemaphoreCount=1;si.pSignalSemaphores=&generatedTicket->ready;
   VkResult sr;{QueueScope scope(enterQueue,leaveQueue);sr=submit(q,1,&si,submitFence);}lastSubmit=sr;if(sr!=VK_SUCCESS){unsafe=true;return sr;}submitPending=true;++generatedSubmitted;if(frame.compose)++composed;
   // CPU joining this submission also protects the shared generated buffer.
   if(!Wait(submitFence,submitPending))return VK_ERROR_DEVICE_LOST;
   VkPresentInfoKHR gp{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};gp.waitSemaphoreCount=1;gp.pWaitSemaphores=&generatedTicket->ready;gp.swapchainCount=1;gp.pSwapchains=&swap;gp.pImageIndices=&extra;
   waitsConsumed=true;
   generatedResult=PresentOne(q,gp,*generatedTicket,true,true);if(unsafe)return generatedResult;
   real.waitSemaphoreCount=0;real.pWaitSemaphores=nullptr; // consumed exactly once by our submission
   if(Accepted(generatedResult)&&presentMode!=VK_PRESENT_MODE_FIFO_KHR&&presentMode!=VK_PRESENT_MODE_FIFO_RELAXED_KHR){
    const auto target=start+std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<double,std::milli>(std::clamp(intervalMs*.5,1.,25.)));
    std::this_thread::sleep_until(target);
   }
  }else if(ar==VK_NOT_READY||ar==VK_TIMEOUT||ar==VK_ERROR_OUT_OF_DATE_KHR||ar==VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT||ar==VK_ERROR_SURFACE_LOST_KHR){
   // Acquire errors do not consume the application's Present waits. Forward
   // the real Present, whose queued error path must still consume those waits.
   ++skipped;
  }else {if(ar==VK_ERROR_DEVICE_LOST)unsafe=true;return ar;}
 }
 const auto rr=PresentOne(q,real,*realTicket,false,waitsConsumed);
 if(Accepted(rr)){
  const auto now=std::chrono::steady_clock::now();if(lastReal.time_since_epoch().count())intervalMs=std::chrono::duration<double,std::milli>(now-lastReal).count();lastReal=now;
 }
 if(!Accepted(rr))return rr;
 if(!Accepted(generatedResult))return generatedResult;
 return generatedResult==VK_SUBOPTIMAL_KHR?generatedResult:rr;
}
bool PhysicalSwapchain::Destroy(bool nativeRetired){
 if(!nativeRetired&&!Drain())return false;
 if(swap)destroy(device,swap,nullptr);swap=VK_NULL_HANDLE;
 for(auto f:{acquireFence,submitFence})if(f)destroyFence(device,f,nullptr);
 acquireFence=submitFence=VK_NULL_HANDLE;
 for(auto& p:presents){if(p.fence)destroyFence(device,p.fence,nullptr);if(p.ready)destroySemaphore(device,p.ready,nullptr);p={};}
 if(pool)destroyPool(device,pool,nullptr);pool=VK_NULL_HANDLE;return true;
}
}
