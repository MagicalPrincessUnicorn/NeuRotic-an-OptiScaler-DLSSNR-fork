#pragma once
#include "../../OptiScaler/dlssnr/NativeFgBridge.h"
#include <vector>
#include <chrono>
#include <atomic>
#include <mutex>
#include <array>

namespace DlssNr::NativeFg {
// This owner never fabricates Acquire signals or submits future waits. The
// physical handle is also the application's handle, including fullscreen APIs.
class PhysicalSwapchain {
 VkDevice device{};VkSwapchainKHR swap{};VkQueue queue{};unsigned family=UINT32_MAX,width=0,height=0;
 VkCommandPool pool{};VkCommandBuffer command{};
 VkFence acquireFence{},submitFence{};
 struct PresentTicket {VkFence fence{};VkSemaphore ready{};bool pending=false,generated=false,accepted=false;};
 std::array<PresentTicket,32> presents{};
 bool acquirePending=false,submitPending=false;std::atomic<bool> unsafe{false};
 VkPresentModeKHR presentMode=VK_PRESENT_MODE_FIFO_KHR;
 bool creationAttempted=false;std::mutex acquisitionGate;
 std::vector<VkImage> images;
 std::chrono::steady_clock::time_point lastReal{};
 double intervalMs=16.667;
 EnterFn enterQueue=nullptr;LeaveFn leaveQueue=nullptr;
 PFN_vkCreateSwapchainKHR create{};PFN_vkDestroySwapchainKHR destroy{};
 PFN_vkGetSwapchainImagesKHR getImages{};PFN_vkAcquireNextImageKHR acquire{};
 PFN_vkReleaseSwapchainImagesEXT release{};PFN_vkQueuePresentKHR present{};
 PFN_vkCreateCommandPool createPool{};PFN_vkDestroyCommandPool destroyPool{};
 PFN_vkAllocateCommandBuffers allocate{};PFN_vkResetCommandPool resetPool{};
 PFN_vkBeginCommandBuffer begin{};PFN_vkEndCommandBuffer end{};
 PFN_vkCmdPipelineBarrier barrier{};PFN_vkQueueSubmit submit{};
 PFN_vkCreateSemaphore createSemaphore{};PFN_vkDestroySemaphore destroySemaphore{};
 PFN_vkCreateFence createFence{};PFN_vkDestroyFence destroyFence{};
 PFN_vkResetFences resetFences{};PFN_vkWaitForFences waitFences{};
 bool Wait(VkFence,bool&);
 bool PollPresents();
 PresentTicket* FreePresent(const PresentTicket* =nullptr);
 VkResult PresentOne(VkQueue,const VkPresentInfoKHR&,PresentTicket&,bool,bool=false);
 bool Release(unsigned);
public:
 uint64_t generatedSubmitted=0,generatedRetired=0,realRetired=0,skipped=0;
 uint64_t composed=0;
 VkResult lastAcquire=VK_SUCCESS,lastSubmit=VK_SUCCESS,lastPresent=VK_SUCCESS,lastWait=VK_SUCCESS;
 unsigned waitKind=0;
 uint64_t PendingPresents()const{uint64_t n=0;for(const auto& p:presents)if(p.pending)++n;return n;}
 VkResult Initialize(const BridgeCreate&,PFN_vkGetDeviceProcAddr,EnterFn=nullptr,LeaveFn=nullptr);
 VkSwapchainKHR Handle()const{return swap;}
 bool CreationAttempted()const{return creationAttempted;}
 bool Bind(VkQueue,unsigned);
 VkResult Images(unsigned*,VkImage*);
 VkResult Acquire(uint64_t,VkSemaphore,VkFence,unsigned*);
 VkResult Present(VkQueue,const VkPresentInfoKHR&,const BridgeFrame&);
 bool IdleGpu();bool Drain();bool Destroy(bool nativeRetired=false);bool Unsafe()const{return unsafe;}
 void Poison(){unsafe=true;}
};
}
