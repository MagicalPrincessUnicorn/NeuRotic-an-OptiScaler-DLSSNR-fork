#pragma once
#include <vulkan/vulkan.h>
#include <algorithm>
#include <cstring>
#include <vector>
namespace DlssNr {
inline bool VkNrHasMemoryHeadroom(uint64_t budget,uint64_t usage,uint64_t images,uint64_t reserve) {
    return budget && usage<=budget && images<=budget-usage && reserve<=budget-usage-images;
}
struct VkNrMemoryAdmission {
    bool known=false,allowed=true;
    uint64_t budget=0,usage=0,images=0,reserve=0;
    VkPhysicalDeviceMemoryProperties memory{};
    VkPhysicalDeviceMemoryBudgetPropertiesEXT snapshot{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    uint64_t allocated[VK_MAX_MEMORY_HEAPS]{};
    bool ReserveImage(uint32_t memoryType,uint64_t bytes) {
        if(!known)return true;
        if(memoryType>=memory.memoryTypeCount){allowed=false;return false;}
        const auto heap=memory.memoryTypes[memoryType].heapIndex;
        if(heap>=memory.memoryHeapCount||heap>=VK_MAX_MEMORY_HEAPS){allowed=false;return false;}
        budget=snapshot.heapBudget[heap];usage=snapshot.heapUsage[heap];
        if(!budget){known=false;return true;}
        // Include our admitted allocations even if the driver's estimated usage
        // has not caught up. Charge only heaps actually selected by requirements.
        if(allocated[heap]>UINT64_MAX-usage){allowed=false;return false;}
        usage+=allocated[heap];
        allowed=VkNrHasMemoryHeadroom(budget,usage,(std::max)(images,bytes),reserve);
        if(allowed){allocated[heap]+=bytes;images=bytes<images?images-bytes:0;}
        return allowed;
    }
};
// Query only on model creation, never on ordinary model reuse. Extension support
// is advertised by the physical device; this query does not amend device creation.
inline VkNrMemoryAdmission VkNrAdmitModelMemory(VkInstance instance,VkPhysicalDevice physical,
    PFN_vkGetInstanceProcAddr getProc,uint32_t width,uint32_t height,uint32_t workWidth,uint32_t workHeight,
    bool before,bool dlaa,bool hold) {
    VkNrMemoryAdmission result;
    if(!instance||!physical||!getProc)return result;
    auto enumerate=reinterpret_cast<PFN_vkEnumerateDeviceExtensionProperties>(getProc(instance,"vkEnumerateDeviceExtensionProperties"));
    auto query=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(getProc(instance,"vkGetPhysicalDeviceMemoryProperties2"));
    if(!query)query=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties2>(getProc(instance,"vkGetPhysicalDeviceMemoryProperties2KHR"));
    if(!enumerate||!query)return result;
    uint32_t count=0;
    if(enumerate(physical,nullptr,&count,nullptr)!=VK_SUCCESS||!count||count>4096)return result;
    std::vector<VkExtensionProperties> extensions(count);
    if(enumerate(physical,nullptr,&count,extensions.data())!=VK_SUCCESS)return result;
    bool supported=false;
    for(uint32_t i=0;i<count;++i)supported|=std::strcmp(extensions[i].extensionName,VK_EXT_MEMORY_BUDGET_EXTENSION_NAME)==0;
    if(!supported)return result;
    VkPhysicalDeviceMemoryBudgetPropertiesEXT budget{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_BUDGET_PROPERTIES_EXT};
    VkPhysicalDeviceMemoryProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2,&budget};
    query(physical,&properties);
    const uint64_t pixels=uint64_t(width)*height,work=uint64_t(workWidth)*workHeight;
    // Conservative image footprint before driver alignment; CreateImage still
    // checks every actual allocation against the existing private-byte cap.
    constexpr uint64_t M=1024*1024;
    if(pixels>UINT64_MAX/256||work>UINT64_MAX/256){result.known=true;result.allowed=false;return result;}
    result.images=pixels*(32+(before?8:0)+(dlaa?16:0)+(hold?24:0))+
        work*(8+((width!=workWidth||height!=workHeight)?8:0))+
        ((workWidth>width||workHeight>height)?pixels*8:0)+16*M;
    // Opaque feature allocations have no public NR size contract. This is a
    // conservative admission reserve, not a measurement or a residency guarantee.
    result.reserve=(std::max)(512*M,work*128);
    result.memory=properties.memoryProperties;result.snapshot=budget;result.snapshot.pNext=nullptr;
    result.known=result.memory.memoryHeapCount>0&&result.memory.memoryHeapCount<=VK_MAX_MEMORY_HEAPS;

    return result;
}
}
