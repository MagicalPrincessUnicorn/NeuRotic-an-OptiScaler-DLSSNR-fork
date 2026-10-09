#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <string>
#include <vector>

namespace DlssNr::PreparedVulkan
{
inline constexpr const char* Extensions[]={"VK_KHR_external_memory_win32","VK_KHR_external_semaphore_win32",
    "VK_KHR_timeline_semaphore"};
struct Enabled { bool externalMemoryWin32=false,externalSemaphoreWin32=false,timeline=false; };
struct Timeline { bool seen=false,enabled=false,complete=true; };
inline Timeline Inspect(const void* head)
{
    Timeline out;std::array<const void*,64> visited{};std::size_t count=0;
    for(auto p=static_cast<const VkBaseInStructure*>(head);p;p=p->pNext)
    {
        if(count==visited.size() || std::find(visited.begin(),visited.begin()+count,p)!=visited.begin()+count)
        {out.complete=false;break;}
        visited[count++]=p;
        bool relevant=false,enabled=false;
        if(p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES)
        {relevant=true;enabled=reinterpret_cast<const VkPhysicalDeviceVulkan12Features*>(p)->timelineSemaphore==VK_TRUE;}
        if(p->sType==VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES)
        {relevant=true;enabled=reinterpret_cast<const VkPhysicalDeviceTimelineSemaphoreFeatures*>(p)->timelineSemaphore==VK_TRUE;}
        if(relevant){if(out.seen){out.complete=false;break;}out.seen=true;out.enabled=enabled;}
    }
    return out;
}
inline bool Has(const VkDeviceCreateInfo& ci,const char* name)
{
    if(!ci.ppEnabledExtensionNames)return false;
    for(std::uint32_t i=0;i<ci.enabledExtensionCount;++i)
        if(ci.ppEnabledExtensionNames[i] && !std::strcmp(ci.ppEnabledExtensionNames[i],name))return true;
    return false;
}
inline Enabled Observe(const VkDeviceCreateInfo& ci,VkResult result)
{
    if(result!=VK_SUCCESS)return {};
    const auto t=Inspect(ci.pNext);
    return {Has(ci,Extensions[0]),Has(ci,Extensions[1]),t.complete && t.seen && t.enabled};
}
// A create-call-local immutable view. Never modifies a caller-owned feature node.
// If the caller explicitly disabled timelines, or the chain is ambiguous, leave it alone.
class Preparation
{
    std::vector<const char*> names_;
    VkPhysicalDeviceTimelineSemaphoreFeatures timeline_{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    bool amend_=false,prepend_=false;
public:
    Preparation(const VkDeviceCreateInfo& ci,const std::vector<std::string>& supported,bool timelineSupported,bool optIn,bool core11=false)
    {
        const auto t=Inspect(ci.pNext);
        if(!optIn || !core11 || !timelineSupported || !t.complete || (t.seen && !t.enabled) ||
           (ci.enabledExtensionCount && !ci.ppEnabledExtensionNames))return;
        for(auto name:Extensions)
            if(!Has(ci,name) && std::find(supported.begin(),supported.end(),name)==supported.end())return;
        if(ci.enabledExtensionCount)names_.assign(ci.ppEnabledExtensionNames,ci.ppEnabledExtensionNames+ci.enabledExtensionCount);
        for(auto name:Extensions)if(!Has(ci,name))names_.push_back(name);
        amend_=true;prepend_=!t.seen;timeline_.timelineSemaphore=VK_TRUE;
        timeline_.pNext=const_cast<void*>(ci.pNext);
    }
    Preparation(const Preparation&)=delete;Preparation& operator=(const Preparation&)=delete;
    VkDeviceCreateInfo Apply(VkDeviceCreateInfo ci)const
    {
        if(amend_){ci.enabledExtensionCount=static_cast<std::uint32_t>(names_.size());ci.ppEnabledExtensionNames=names_.data();}
        if(prepend_)ci.pNext=&timeline_;
        return ci;
    }
};
// In-process query ABI. These observed identities/capabilities grant no image,
// command-buffer, queue-submission, or lifetime rights to the caller.
struct DeviceInfo
{
    std::uint32_t size=sizeof(DeviceInfo),version=1;
    VkDevice device=VK_NULL_HANDLE;VkQueue queue=VK_NULL_HANDLE;
    VkPhysicalDevice physical=VK_NULL_HANDLE;std::uint64_t generation=0;
    std::uint32_t family=UINT32_MAX,queueFlags=0;
    std::uint32_t externalMemoryWin32=0,externalSemaphoreWin32=0,timeline=0,reserved=0;
    PFN_vkGetDeviceProcAddr getDeviceProcAddr=nullptr;
    // Loader-exported physical dispatch trampolines: no guessed/current instance.
    PFN_vkGetPhysicalDeviceProperties2 getProperties2=nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 getImageFormatProperties2=nullptr;
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties getExternalSemaphoreProperties=nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties=nullptr;
};
using QueryFn=unsigned(__cdecl*)(DeviceInfo*);
}
