#include <vulkan/vulkan.h>
#include <cstdio>
#include <cstdlib>
#if __has_include("dlssnr/VulkanNrMemoryBudget.h")
#include "dlssnr/VulkanNrMemoryBudget.h"
#else
namespace DlssNr { inline bool VkNrHasMemoryHeadroom(uint64_t,uint64_t,uint64_t,uint64_t){return true;} }
#endif

static bool supported=true,enumerationFails=false;
static uint64_t reportedUsage=0,reportedBudget=16ull*1024*1024*1024;
static unsigned queries=0;
static VKAPI_ATTR VkResult VKAPI_CALL Enumerate(VkPhysicalDevice,const char*,uint32_t* n,VkExtensionProperties* out){
 if(enumerationFails)return VK_ERROR_UNKNOWN;
 if(!out){*n=1;return VK_SUCCESS;}
 strcpy_s(out[0].extensionName,supported?VK_EXT_MEMORY_BUDGET_EXTENSION_NAME:"VK_dummy");*n=1;return VK_SUCCESS;
}
static VKAPI_ATTR void VKAPI_CALL Query(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties2* out){
 ++queries;out->memoryProperties.memoryHeapCount=1;
 out->memoryProperties.memoryHeaps[0]={reportedBudget,VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
 out->memoryProperties.memoryHeapCount=2;
 out->memoryProperties.memoryHeaps[1]={256ull*1024*1024,VK_MEMORY_HEAP_DEVICE_LOCAL_BIT};
 out->memoryProperties.memoryTypeCount=2;
 out->memoryProperties.memoryTypes[0]={VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0};
 out->memoryProperties.memoryTypes[1]={VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,1};
 auto* budget=static_cast<VkPhysicalDeviceMemoryBudgetPropertiesEXT*>(out->pNext);
 budget->heapBudget[0]=reportedBudget;budget->heapUsage[0]=reportedUsage;budget->heapBudget[1]=256ull*1024*1024;
}
static VKAPI_ATTR PFN_vkVoidFunction VKAPI_CALL Proc(VkInstance,const char* name){
 if(std::strcmp(name,"vkEnumerateDeviceExtensionProperties")==0)return reinterpret_cast<PFN_vkVoidFunction>(Enumerate);
 if(std::strcmp(name,"vkGetPhysicalDeviceMemoryProperties2")==0)return reinterpret_cast<PFN_vkVoidFunction>(Query);
 return nullptr;
}
int main(){
 auto check=[](bool v,const char* s){if(!v){std::fprintf(stderr,"FAIL %s\n",s);std::exit(1);}};
 constexpr uint64_t M=1024*1024;
 using DlssNr::VkNrHasMemoryHeadroom;
 check(!VkNrHasMemoryHeadroom(14*1024*M,15*1024*M,256*M,1024*M),"over-budget third layer cannot allocate");
 check(!VkNrHasMemoryHeadroom(14*1024*M,13*1024*M,256*M,1024*M),"images plus opaque-model reserve must fit");
 check(VkNrHasMemoryHeadroom(14*1024*M,12*1024*M,256*M,1024*M),"roomy allocation admitted");
 check(VkNrHasMemoryHeadroom(2048,1024,512,512),"exact arithmetic boundary admitted");
 check(!VkNrHasMemoryHeadroom(2048,1025,512,512),"one byte short refuses");
 check(!VkNrHasMemoryHeadroom(UINT64_MAX,UINT64_MAX-10,8,8),"overflow cannot admit");

 auto admit=[] {return DlssNr::VkNrAdmitModelMemory(reinterpret_cast<VkInstance>(1),reinterpret_cast<VkPhysicalDevice>(2),Proc,3840,2160,3840,2160,false,false,false);};
 auto healthy=admit();const auto estimated=healthy.images;check(healthy.known&&healthy.ReserveImage(0,64*M)&&queries==1,"supported query admits headroom");
 reportedUsage=reportedBudget-estimated-healthy.reserve+1;
 auto low=admit();check(low.known&&!low.ReserveImage(0,64*M)&&queries==2,"driver budget prevents new model before allocation");
 reportedUsage=0;check(admit().ReserveImage(0,64*M),"budget recovery allows retry");
 supported=false;check(!admit().known&&queries==3,"unsupported extension is unknown and not queried");
 supported=true;enumerationFails=true;check(!admit().known,"enumeration error is unknown");
 enumerationFails=false;reportedBudget=0;auto unknown=admit();check(unknown.ReserveImage(0,64*M)&&!unknown.known,"invalid budget is unknown");
 reportedBudget=16ull*1024*1024*1024;auto small=admit();check(!small.ReserveImage(1,64*M),"selected small heap is protected");
 auto big=admit();check(big.ReserveImage(0,64*M),"unrelated local BAR heap cannot block VRAM image");
 auto exact=admit();exact.images=0;exact.reserve=0;exact.snapshot.heapBudget[0]=128;
 check(exact.ReserveImage(0,64)&&exact.ReserveImage(0,64)&&!exact.ReserveImage(0,1),"own allocations charged despite stale driver snapshot");
 std::puts("PASS live budget arithmetic: overcommit, opaque reserve, recovery, boundary, overflow");
}
