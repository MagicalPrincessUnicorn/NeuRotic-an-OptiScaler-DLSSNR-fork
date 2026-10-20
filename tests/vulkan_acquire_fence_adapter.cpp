#include <dlssnr/VulkanNrAcquireFence.h>
#include <cassert>
#include <cstdio>
using namespace DlssNr;
template<class T> T H(uintptr_t n){return reinterpret_cast<T>(n);}
int main(){
 auto d=H<VkDevice>(1);auto s=H<VkSwapchainKHR>(2);auto q=H<VkQueue>(3);
 VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.imageArrayLayers=1;ci.imageExtent={64,64};
 for(auto r:{&GetVulkanPresentRegistry(),&GetVulkanLoaderPresentRegistry()}){
  r->DeviceCreated(d,H<VkPhysicalDevice>(4),true,true,{0});r->QueueObserved(d,q,0,VK_QUEUE_GRAPHICS_BIT);
  r->SwapchainCreated(d,s,ci,0,true,{H<VkImage>(5),H<VkImage>(6)},{0});
 }
 auto& r=GetVulkanPresentRegistry();VkFence fences[]{H<VkFence>(7),H<VkFence>(8)};
 auto acquire=[&](uint32_t index){for(auto reg:{&r,&GetVulkanLoaderPresentRegistry()})reg->ImageAcquired(d,s,index,VK_NULL_HANDLE,fences[index]);};
 acquire(0);acquire(1);auto a=r.AcquireFenceProof(d,fences[0]);auto b=r.AcquireFenceProof(d,fences[1]);
 unsigned calls=0,polls=0;
 auto original=[&]{++calls;return VK_SUCCESS;};
 auto poll=[&](VkFence f){++polls;return f==fences[0]?VK_SUCCESS:VK_NOT_READY;};
 assert(ObserveVkAcquireFenceWait(d,2,fences,VK_FALSE,original,poll)==VK_SUCCESS);
 assert(calls==1&&polls==2&&a->Complete()&&!b->Complete());
 assert(GetVulkanLoaderPresentRegistry().AcquireFenceProof(d,fences[0])->Complete());
 // Timeout supplies no completion, even for waitAll.
 ObserveVkAcquireFenceWait(d,2,fences,VK_TRUE,[]{return VK_TIMEOUT;},poll);assert(!b->Complete()&&polls==2);
 // Reset captures a signalled old lease before invalidating the fence binding.
 ObserveVkAcquireFenceReset(d,1,fences,original,poll);assert(a->Complete()&&!r.AcquireFenceProof(d,fences[0]));
 acquire(0);auto newer=r.AcquireFenceProof(d,fences[0]);assert(!newer->Complete());
 assert(ObserveVkAcquireFenceStatus(d,fences[0],[]{return VK_NOT_READY;})==VK_NOT_READY&&!newer->Complete());
 // Failed reset preserves its identity, successful status completes only that lease.
 ObserveVkAcquireFenceReset(d,1,fences,[]{return VK_ERROR_OUT_OF_HOST_MEMORY;},[](VkFence){return VK_NOT_READY;});
 assert(r.AcquireFenceProof(d,fences[0])==newer);
 ObserveVkAcquireFenceStatus(d,fences[0],original);assert(newer->Complete()&&!b->Complete());
 unsigned destroys=0;ObserveVkAcquireFenceDestroy(d,fences[1],[&]{++destroys;},[](VkFence){return VK_NOT_READY;});
 assert(destroys==1&&!r.AcquireFenceProof(d,fences[1])&&!b->Complete());
 auto unknown=H<VkFence>(99);polls=0;ObserveVkAcquireFenceReset(d,1,&unknown,original,poll);assert(polls==0);
 puts("PASS actual fence adapter: exact leases, waitAny/all, timeout, reset failure/reuse, destroy and unknown fast path");
}
