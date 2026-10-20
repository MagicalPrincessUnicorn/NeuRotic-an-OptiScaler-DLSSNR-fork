#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <dlssnr/PreparedVkDx12Transport.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cstring>
// Optional actual-driver import probe. Exit 77 means unavailable, never PASS.
int main(){
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.apiVersion=VK_API_VERSION_1_1;
    VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;VkInstance instance{};
    if(vkCreateInstance(&create,nullptr,&instance)!=VK_SUCCESS){std::puts("UNAVAILABLE: no local Vulkan 1.1 driver");return 77;}
    uint32_t count=0;vkEnumeratePhysicalDevices(instance,&count,nullptr);std::vector<VkPhysicalDevice> physical(count);vkEnumeratePhysicalDevices(instance,&count,physical.data());
    int result=77;
    for(auto p:physical){
        VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};props.pNext=&id;vkGetPhysicalDeviceProperties2(p,&props);
        uint32_t n=0;vkEnumerateDeviceExtensionProperties(p,nullptr,&n,nullptr);std::vector<VkExtensionProperties> extensions(n);vkEnumerateDeviceExtensionProperties(p,nullptr,&n,extensions.data());
        const char* names[]={"VK_KHR_external_memory_win32","VK_KHR_external_semaphore_win32","VK_KHR_timeline_semaphore"};bool enabled=true;
        for(auto name:names){bool found=false;for(const auto& e:extensions)found|=std::strcmp(e.extensionName,name)==0;enabled&=found;}
        VkPhysicalDeviceTimelineSemaphoreFeatures timeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};features.pNext=&timeline;vkGetPhysicalDeviceFeatures2(p,&features);
        std::printf("Adapter: %s; LUID=%u; sharing extensions=%u; timeline=%u\n",props.properties.deviceName,id.deviceLUIDValid,unsigned(enabled),timeline.timelineSemaphore);
        if(!enabled||!timeline.timelineSemaphore||!id.deviceLUIDValid)continue;
        DlssNr::PreparedTransport::Dispatch d{};d.getProperties2=vkGetPhysicalDeviceProperties2;d.getImageFormatProperties2=vkGetPhysicalDeviceImageFormatProperties2;d.getExternalSemaphoreProperties=vkGetPhysicalDeviceExternalSemaphoreProperties;
        d.getExternalBufferProperties=vkGetPhysicalDeviceExternalBufferProperties;d.getMemoryProperties=vkGetPhysicalDeviceMemoryProperties;
        Microsoft::WRL::ComPtr<IDXGIFactory4> factory;Microsoft::WRL::ComPtr<IDXGIAdapter1> adapter;Microsoft::WRL::ComPtr<ID3D12Device> dx;
        if(FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))||!DlssNr::PreparedTransport::MatchAdapter(p,d,factory.Get(),&adapter)||FAILED(D3D12CreateDevice(adapter.Get(),D3D_FEATURE_LEVEL_12_0,IID_PPV_ARGS(&dx))))continue;
        vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> families(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,families.data());uint32_t family=UINT32_MAX;
        for(uint32_t i=0;i<n;++i)if(families[i].queueFlags&VK_QUEUE_GRAPHICS_BIT){family=i;break;}if(family==UINT32_MAX)continue;
        float priority=1;VkDeviceQueueCreateInfo q{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};q.queueFamilyIndex=family;q.queueCount=1;q.pQueuePriorities=&priority;
        VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};ci.pNext=&timeline;ci.queueCreateInfoCount=1;ci.pQueueCreateInfos=&q;ci.enabledExtensionCount=3;ci.ppEnabledExtensionNames=names;VkDevice device{};
        if(vkCreateDevice(p,&ci,nullptr,&device)!=VK_SUCCESS)continue;VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
#define LOAD(Field,Name) d.Field=reinterpret_cast<PFN_vk##Name>(vkGetDeviceProcAddr(device,"vk" #Name))
        LOAD(getHandleProperties,GetMemoryWin32HandlePropertiesKHR);LOAD(createImage,CreateImage);LOAD(destroyImage,DestroyImage);LOAD(getImageMemoryRequirements,GetImageMemoryRequirements);
        LOAD(allocateMemory,AllocateMemory);LOAD(freeMemory,FreeMemory);LOAD(bindImageMemory,BindImageMemory);LOAD(createSemaphore,CreateSemaphore);LOAD(destroySemaphore,DestroySemaphore);LOAD(importSemaphore,ImportSemaphoreWin32HandleKHR);
        LOAD(barrier,CmdPipelineBarrier);LOAD(copyImage,CmdCopyImage);LOAD(createBuffer,CreateBuffer);LOAD(destroyBuffer,DestroyBuffer);LOAD(getBufferMemoryRequirements,GetBufferMemoryRequirements);LOAD(bindBufferMemory,BindBufferMemory);
#undef LOAD
        {DlssNr::PreparedTransport::Session session;DlssNr::PreparedTransport::ImageSpec raw{};raw.rawBytes=1024*1024;
            const auto imported=session.Initialize(p,device,queue,family,dx.Get(),d,{true,true,true},std::span(&raw,1));
            if(imported){std::puts("PASS: actual driver shared raw buffer and timeline fence import (no submitted cross-API frame)");session.Retire({true,true,true,true});result=0;}
            else std::printf("UNAVAILABLE: %s\n",imported.reason.c_str());}
        vkDestroyDevice(device,nullptr);
    }
    vkDestroyInstance(instance,nullptr);if(result==77)std::puts("UNAVAILABLE: shared Vulkan/D3D12 hardware import unqualified locally");return result;
}
