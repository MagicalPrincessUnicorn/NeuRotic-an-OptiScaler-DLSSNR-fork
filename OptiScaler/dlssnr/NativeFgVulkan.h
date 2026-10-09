#pragma once
#include "NativeFgBridge.h"
#include <atomic>
#include <mutex>
#include <optional>
#include <string>
#include <utility>
namespace DlssNr::NativeFg {
inline std::atomic<bool> configured{false};
inline void Configure(bool value){configured.store(value,std::memory_order_relaxed);}
bool Selected();bool Internal();
void DeviceCreated(VkDevice,unsigned family,unsigned first,PFN_vkGetDeviceQueue);
void InstanceMaintenance(VkPhysicalDevice,bool);bool SurfaceMaintenance(VkPhysicalDevice);
void DeviceMaintenance(VkDevice,bool);
struct InternalScope{InternalScope();~InternalScope();};
inline std::recursive_mutex queueGate;
inline std::recursive_mutex& QueueMutex(){return queueGate;}
template<class Fn,class...Args>decltype(auto) QueueCall(Fn fn,Args&&...args){std::lock_guard lock(QueueMutex());return fn(std::forward<Args>(args)...);}
std::optional<VkResult> Create(VkDevice,VkPhysicalDevice,VkQueue,unsigned,const VkSwapchainCreateInfoKHR*,const VkAllocationCallbacks*,VkSwapchainKHR*,bool qualified,const char* qualificationRefusal=nullptr);
bool Owns(VkSwapchainKHR);
bool BindPresentQueue(VkDevice,VkSwapchainKHR,VkQueue,unsigned);
std::optional<VkResult> Images(VkDevice,VkSwapchainKHR,unsigned*,VkImage*);
std::optional<VkResult> Acquire(VkDevice,VkSwapchainKHR,std::uint64_t,VkSemaphore,VkFence,unsigned*);
bool Begin(VkSwapchainKHR);
std::uint64_t Frame(VkSwapchainKHR);
bool Publish(VkSwapchainKHR,CopyFn,void*);
bool Finish(VkSwapchainKHR);
// Synchronous physical Present only; the caller pins context through GPU completion.
bool Composition(VkSwapchainKHR,CopyFn,void*);
PFN_vkQueuePresentKHR ResolvePresent(const VkPresentInfoKHR*,PFN_vkQueuePresentKHR);
VkResult CallPresent(PFN_vkQueuePresentKHR,VkQueue,const VkPresentInfoKHR*);
// Async SDK workers must finish before native idle takes the queue gate. Physical
// WSI fences are polled afterwards and may remain pending through recreation.
bool Drain(VkSwapchainKHR);bool DrainDevice(VkDevice,bool includePhysical=true);bool DrainQueue(VkQueue,bool includePhysical=true);
std::optional<bool> Destroy(VkDevice,VkSwapchainKHR);bool DestroyDevice(VkDevice);
std::string Status();
struct PresentationProgress { uint64_t owners=0,real=0,requested=0,identity=0; bool physicalAvailable=false; BridgeProgress physical; std::string reason; };
PresentationProgress Progress();
#ifdef NRPG_CPU_MAINLINE_TESTING
void InjectBridge(BridgeApi);
#endif
}
