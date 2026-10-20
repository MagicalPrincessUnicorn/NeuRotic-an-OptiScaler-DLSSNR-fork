// Injected into a local verification DLL by vulkan_hook_probe.targets only.
// This file is not part of the shipping project.
#include <windows.h>
#include <vulkan/vulkan.h>
#include "dlssnr/VulkanNrRecording.h"
#include "dlssnr/VulkanNrCompletion.h"
#include "dlssnr/VulkanNrDeviceSubmitHooks.h"
#include <cstdio>
#include <vector>

namespace DlssNr {
struct VkNrDeviceSubmitProbe {
    template<class Info> static typename VkNrDeviceSubmitHooks<Info>::Function Target()
    {
        std::lock_guard lock(VkNrDeviceSubmitHooks<Info>::mutex_);
        return VkNrDeviceSubmitHooks<Info>::slots_[0].target;
    }
};
}

extern "C" __declspec(dllexport) int NrVulkanHookProbe(const char* report, int mode)
{
    FILE* out=nullptr;fopen_s(&out,report,"w");if(!out)return 90;
    const auto fail=[&](const char* where,int result){fprintf(out,"FAIL %s: %d\n",where,result);fclose(out);return result?result:91;};
    auto module=GetModuleHandleW(L"vulkan-1.dll");if(!module)return fail("loader",92);
    auto gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(module,"vkGetInstanceProcAddr"));
    auto create=reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(module,"vkCreateInstance"));
    const bool direct=mode>=18;
    const bool secondary=direct?mode>=21:mode%6>=3;const int submitKind=direct?(mode-18)%3:mode/6;
    VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="NeuRotic hook probe";app.apiVersion=VK_API_VERSION_1_3;
    VkInstanceCreateInfo ii{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ii.pApplicationInfo=&app;
    VkInstance instance{};auto result=create(&ii,nullptr,&instance);if(result)return fail("instance",result);
    auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"));
    uint32_t count=0;enumerate(instance,&count,nullptr);if(!count)return fail("physical device",93);
    std::vector<VkPhysicalDevice> pds(count);enumerate(instance,&count,pds.data());auto pd=pds.front();
    auto families=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
    families(pd,&count,nullptr);std::vector<VkQueueFamilyProperties> properties(count);families(pd,&count,properties.data());
    uint32_t family=0;while(family<count&&!(properties[family].queueFlags&VK_QUEUE_GRAPHICS_BIT))++family;
    if(family==count)return fail("graphics family",94);
    float priority=1;VkDeviceQueueCreateInfo qi{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
    VkDeviceCreateInfo di{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;
    VkPhysicalDeviceSynchronization2Features sync{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SYNCHRONIZATION_2_FEATURES};sync.synchronization2=VK_TRUE;
    const char* extension=VK_KHR_SYNCHRONIZATION_2_EXTENSION_NAME;
    if(submitKind){di.pNext=&sync;if(submitKind==2){di.enabledExtensionCount=1;di.ppEnabledExtensionNames=&extension;}}
    VkDevice device{};result=reinterpret_cast<PFN_vkCreateDevice>(gipa(instance,"vkCreateDevice"))(pd,&di,nullptr,&device);if(result)return fail("device",result);
    auto gdpa=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));
    const auto proc=[&](const char* name)->PFN_vkVoidFunction {return mode%3==0?reinterpret_cast<PFN_vkVoidFunction>(GetProcAddress(module,name)):mode%3==1?gipa(instance,name):gdpa(device,name);};
    VkQueue queue{};reinterpret_cast<PFN_vkGetDeviceQueue>(proc("vkGetDeviceQueue"))(device,family,0,&queue);
    VkCommandPoolCreateInfo pi{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pi.queueFamilyIndex=family;
    VkCommandPool pool{};result=reinterpret_cast<PFN_vkCreateCommandPool>(proc("vkCreateCommandPool"))(device,&pi,nullptr,&pool);if(result)return fail("pool",result);
    VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;
    VkCommandBuffer cb{};result=reinterpret_cast<PFN_vkAllocateCommandBuffers>(proc("vkAllocateCommandBuffers"))(device,&ai,&cb);if(result)return fail("allocate",result);
    VkCommandBuffer primary=cb;
    if(secondary){ai.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;result=reinterpret_cast<PFN_vkAllocateCommandBuffers>(proc("vkAllocateCommandBuffers"))(device,&ai,&cb);if(result)return fail("secondary",result);}
    VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};if(secondary)bi.pInheritanceInfo=&inheritance;
    result=reinterpret_cast<PFN_vkBeginCommandBuffer>(proc("vkBeginCommandBuffer"))(cb,&bi);if(result)return fail("begin",result);
    auto& owner=DlssNr::VulkanNrRecordings();auto use=owner.ReserveCopy(cb,DlssNr::VkNrCompletionDeviceGeneration(device));
    if(!use)return fail("reserve tracked use",95);
    owner.RetainUse(*use);
    result=reinterpret_cast<PFN_vkEndCommandBuffer>(proc("vkEndCommandBuffer"))(cb);if(result)return fail("end",result);
    if(secondary){bi.pInheritanceInfo=nullptr;reinterpret_cast<PFN_vkBeginCommandBuffer>(proc("vkBeginCommandBuffer"))(primary,&bi);
        reinterpret_cast<PFN_vkCmdExecuteCommands>(proc("vkCmdExecuteCommands"))(primary,1,&cb);
        reinterpret_cast<PFN_vkEndCommandBuffer>(proc("vkEndCommandBuffer"))(primary);}
    fprintf(out,"before %s\n",owner.DescribeProducer(*use).c_str());
    VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&primary;
    if(!submitKind){
        auto submit=direct?DlssNr::VkNrDeviceSubmitProbe::Target<VkSubmitInfo>():reinterpret_cast<PFN_vkQueueSubmit>(proc("vkQueueSubmit"));
        if(!submit)return fail("distinct device legacy target unavailable",97);
        result=submit(queue,1,&si,VK_NULL_HANDLE);
    }
    else {
        auto submit=direct?DlssNr::VkNrDeviceSubmitProbe::Target<VkSubmitInfo2>():reinterpret_cast<PFN_vkQueueSubmit2>(proc(submitKind==1?"vkQueueSubmit2":"vkQueueSubmit2KHR"));
        if(!submit)return fail("submit2 entry unavailable",97);
        VkCommandBufferSubmitInfo ci{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};ci.commandBuffer=primary;
        VkSubmitInfo2 si2{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};si2.commandBufferInfoCount=1;si2.pCommandBufferInfos=&ci;
        result=submit(queue,1,&si2,VK_NULL_HANDLE);
    }
    if(result)return fail("submit",result);
    fprintf(out,"after %s\n",owner.DescribeProducer(*use).c_str());
    bool proved=owner.ProducerProof(*use).has_value();
    reinterpret_cast<PFN_vkQueueWaitIdle>(proc("vkQueueWaitIdle"))(queue);DlssNr::PollVkNrCompletions();
    fprintf(out,"complete %s\n",owner.DescribeProducer(*use).c_str());
    owner.ReleaseUse(*use);
    reinterpret_cast<PFN_vkDestroyCommandPool>(proc("vkDestroyCommandPool"))(device,pool,nullptr);
    reinterpret_cast<PFN_vkDestroyDevice>(proc("vkDestroyDevice"))(device,nullptr);
    reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"))(instance,nullptr);
    fprintf(out,"%s mode=%d\n",proved?"PASS":"FAIL no producer proof",mode);fclose(out);return proved?0:96;
}
