#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <vulkan/vulkan.h>
int main(int argc,char** argv) {
    if(argc!=4)return 2;
    auto vk=LoadLibraryW(L"vulkan-1.dll");if(!vk){printf("loader error %lu\n",GetLastError());return 3;}
    VkInstanceCreateInfo info{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};VkInstance instance{};
    auto baseline=reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(vk,"vkCreateInstance"))(&info,nullptr,&instance);
    printf("unhooked Vulkan 1.0 create result=%d\n",baseline);fflush(stdout);
    if(baseline!=VK_SUCCESS)return 6;
    reinterpret_cast<PFN_vkDestroyInstance>(GetProcAddress(vk,"vkDestroyInstance"))(instance,nullptr);
    auto dll=LoadLibraryA(argv[1]);if(!dll){printf("product error %lu\n",GetLastError());return 4;}
    auto probe=reinterpret_cast<int(*)(const char*,int)>(GetProcAddress(dll,"NrVulkanHookProbe"));
    if(!probe){printf("probe export error %lu\n",GetLastError());return 5;}
    int result=probe(argv[2],atoi(argv[3]));printf("probe result=%d\n",result);
    // Avoid unloading the hooked runtime while process workers still run.
    ExitProcess(static_cast<UINT>(result));
}
