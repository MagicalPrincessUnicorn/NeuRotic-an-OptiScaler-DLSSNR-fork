#include <dlssnr/PreparedVulkanDevice.h>
#include <cassert>
int main()
{
    using namespace DlssNr::PreparedVulkan;
    VkDeviceCreateInfo ci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
    std::vector<std::string> supported;
    for(auto n:Extensions)supported.emplace_back(n);
    Preparation off(ci,supported,true,false);
    assert(off.Apply(ci).pNext==ci.pNext && off.Apply(ci).enabledExtensionCount==0);
    Preparation legacy(ci,supported,true,true,false);
    assert(legacy.Apply(ci).pNext==ci.pNext && legacy.Apply(ci).enabledExtensionCount==0);
    Preparation on(ci,supported,true,true,true);
    auto out=on.Apply(ci);auto facts=Observe(out,VK_SUCCESS);
    assert(facts.externalMemoryWin32 && facts.externalSemaphoreWin32 && facts.timeline);
    assert(!Observe(out,VK_ERROR_INITIALIZATION_FAILED).timeline);
    assert(ci.pNext==nullptr && ci.enabledExtensionCount==0);
    VkPhysicalDeviceVulkan12Features v12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
    ci.pNext=&v12;Preparation disabled(ci,supported,true,true,true);
    assert(disabled.Apply(ci).pNext==&v12 && !Observe(disabled.Apply(ci),VK_SUCCESS).timeline);
    v12.timelineSemaphore=VK_TRUE;Preparation existing(ci,supported,true,true,true);
    assert(existing.Apply(ci).pNext==&v12 && Observe(existing.Apply(ci),VK_SUCCESS).timeline);
    VkPhysicalDeviceTimelineSemaphoreFeatures duplicate{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    duplicate.timelineSemaphore=VK_TRUE;v12.pNext=&duplicate;
    assert(!Observe(ci,VK_SUCCESS).timeline);
    duplicate.pNext=&duplicate;assert(!Observe(ci,VK_SUCCESS).timeline);
    ci.pNext=nullptr;supported.pop_back();Preparation missing(ci,supported,true,true,true);
    assert(missing.Apply(ci).pNext==nullptr && missing.Apply(ci).enabledExtensionCount==0);
}
