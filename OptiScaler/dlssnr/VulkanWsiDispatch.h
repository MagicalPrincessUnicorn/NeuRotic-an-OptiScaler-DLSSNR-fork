#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>
#include <vector>

namespace DlssNr
{
// A resolver result is callable only for its dispatch domain. Never replace it
// with a singleton pointer from another device or infer a terminal role from its
// module. Entries retain the exact target for their entire process lifetime.
template<class Tag,class Function,size_t Capacity=64> class VkWsiFunctionSlots;
template<class Tag,class R,class... Args,size_t Capacity>
class VkWsiFunctionSlots<Tag,R(VKAPI_PTR*)(Args...),Capacity>
{
  public:
    using Function=R(VKAPI_PTR*)(Args...);
    static Function Wrap(Function target)
    {
        if(!target)return nullptr;
        std::lock_guard lock(mutex_);
        for(size_t i=0;i<count_;++i)
            if(target==targets_[i].load(std::memory_order_acquire)||target==entries_[i])return entries_[i];
        if(count_==Capacity)return target; // Fail open; no overlay rights fabricated.
        const auto index=count_++;
        targets_[index].store(target,std::memory_order_release);
        return entries_[index];
    }
    static bool IsEntry(Function target)
    {for(const auto entry:entries_)if(entry==target)return true;return false;}
  private:
    template<size_t Index> static R VKAPI_CALL Invoke(Args... args)
    {return Tag::Invoke(targets_[Index].load(std::memory_order_acquire),args...);}
    template<size_t... I> static constexpr auto Entries(std::index_sequence<I...>)
    {return std::array<Function,sizeof...(I)>{&Invoke<I>...};}
    static inline std::mutex mutex_;
    static inline size_t count_=0;
    static inline std::array<std::atomic<Function>,Capacity> targets_{};
    static inline const auto entries_=Entries(std::make_index_sequence<Capacity>{});
};

template<class Function> struct VkWsiTargetScope
{
    static inline thread_local Function current=nullptr;
    Function previous=current;
    explicit VkWsiTargetScope(Function target){current=target;}
    ~VkWsiTargetScope(){current=previous;}
    static Function Get(Function fallback){return current?current:fallback;}
};

template<auto Hook> struct VkWsiHookTag;
template<class R,class... A,R(VKAPI_PTR* Hook)(A...)> struct VkWsiHookTag<Hook>
{
    static R Invoke(R(VKAPI_PTR* target)(A...),A... args)
    {VkWsiTargetScope<decltype(target)> scope(target);return Hook(args...);}
};

enum class VkWsiOperation {Create,Images,Acquire,Acquire2,Present,Destroy,DeviceDestroy,Queue,Queue2,SurfaceCreate,SurfaceDestroy,
    FenceStatus,FenceWait,FenceReset,FenceDestroy};
// Scope is set only around forwarding, not around the complete hook. A nested
// provider Present with a different argument tuple remains an independent call.
struct VkWsiForwardScope
{
    VkWsiOperation operation;uintptr_t object;const void* argument;
    VkWsiForwardScope* previous;
    static inline thread_local VkWsiForwardScope* top=nullptr;
    VkWsiForwardScope(VkWsiOperation op,uintptr_t obj,const void* arg):operation(op),object(obj),argument(arg),previous(top){top=this;}
    ~VkWsiForwardScope(){top=previous;}
    static bool Matches(VkWsiOperation op,uintptr_t obj,const void* arg)
    {for(auto* p=top;p;p=p->previous)if(p->operation==op&&p->object==obj&&p->argument==arg)return true;return false;}
};

struct VkWsiDevice
{
    VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;VkInstance instance=VK_NULL_HANDLE;
    uint64_t generation=0;std::vector<VkQueueFlags> families;
    PFN_vkGetSwapchainImagesKHR getImages=nullptr;
};
class VkWsiDevices
{
  public:
    uint64_t Created(VkDevice device,VkPhysicalDevice physical,VkInstance instance,std::vector<VkQueueFlags> families,
                     PFN_vkGetSwapchainImagesKHR getImages=nullptr)
    {
        std::lock_guard lock(mutex_);EraseQueues(device);
        if(disabled_)return 0;
        const auto generation=++next_;devices_[(uintptr_t)device]={device,physical,instance,generation,std::move(families),getImages};return generation;
    }
    void Destroyed(VkDevice device){std::lock_guard lock(mutex_);EraseQueues(device);devices_.erase((uintptr_t)device);
        std::erase_if(swapchains_,[&](const auto& entry){return entry.second==device;});}
    void Disable(){std::lock_guard lock(mutex_);DisableLocked();}
    void Swapchain(VkDevice device,VkSwapchainKHR swapchain)
    {
        std::lock_guard lock(mutex_);if(disabled_)return;
        // Existing observation registries key non-dispatchable swapchains by
        // numeric handle. Refuse aliases across live devices instead of mixing
        // their images/acquisition records into one overlay identity.
        const auto old=swapchains_.find((uintptr_t)swapchain);
        if(old!=swapchains_.end()&&old->second!=device){DisableLocked();return;}
        swapchains_[(uintptr_t)swapchain]=device;
    }
    void SwapchainDestroyed(VkDevice device,VkSwapchainKHR swapchain)
    {std::lock_guard lock(mutex_);const auto it=swapchains_.find((uintptr_t)swapchain);
     if(it!=swapchains_.end()&&it->second==device)swapchains_.erase(it);}
    VkWsiDevice Device(VkDevice device)const{std::lock_guard lock(mutex_);const auto it=devices_.find((uintptr_t)device);return it==devices_.end()?VkWsiDevice{}:it->second;}
    void ImagesDispatch(VkDevice device,PFN_vkGetSwapchainImagesKHR fn){std::lock_guard lock(mutex_);const auto it=devices_.find((uintptr_t)device);if(it!=devices_.end())it->second.getImages=fn;}
    void Queue(VkDevice device,VkQueue queue,uint32_t family,VkDeviceQueueCreateFlags flags)
    {
        std::lock_guard lock(mutex_);const auto d=devices_.find((uintptr_t)device);
        if(!queue||d==devices_.end()||family>=d->second.families.size()||flags)return;
        queues_[(uintptr_t)queue]={device,d->second.generation};
    }
    VkWsiDevice ForQueue(VkQueue queue)const
    {
        std::lock_guard lock(mutex_);const auto q=queues_.find((uintptr_t)queue);if(q==queues_.end())return {};
        const auto d=devices_.find((uintptr_t)q->second.first);
        return d!=devices_.end()&&d->second.generation==q->second.second?d->second:VkWsiDevice{};
    }
  private:
    void EraseQueues(VkDevice device){std::erase_if(queues_,[&](const auto& q){return q.second.first==device;});}
    void DisableLocked(){disabled_=true;devices_.clear();queues_.clear();swapchains_.clear();}
    mutable std::mutex mutex_;uint64_t next_=0;bool disabled_=false;
    std::unordered_map<uintptr_t,VkWsiDevice> devices_;
    std::unordered_map<uintptr_t,std::pair<VkDevice,uint64_t>> queues_;
    std::unordered_map<uintptr_t,VkDevice> swapchains_;
};
inline VkWsiDevices& VulkanLoaderWsiDevices(){static auto* devices=new VkWsiDevices;return *devices;}
inline bool VkWsiNeedsOverlayOnly(bool adapterObserved,bool adapterKnownOff,bool lastModeActive,bool independentProvider)
{return independentProvider||(adapterObserved?!adapterKnownOff:lastModeActive);}
inline VkResult VkWsiSelectedPresentResult(bool originalCalled,VkResult result,const VkPresentInfoKHR& info)
{
    return originalCalled&&(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)&&info.pResults&&info.swapchainCount
        ? info.pResults[0]:result;
}
class VulkanPresentRegistry;
VulkanPresentRegistry& GetVulkanLoaderPresentRegistry();
}
