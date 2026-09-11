#pragma once
#include "SysUtils.h"

struct VulkanPresentedExtent
{
    uint32_t width = 0;
    uint32_t height = 0;
};

VulkanPresentedExtent GetVulkanPresentedExtent();

class VulkanHooks
{
  public:
    static PFN_vkCreateSemaphore o_vkCreateSemaphore;
    static PFN_vkSignalSemaphore o_vkSignalSemaphore;
    static PFN_vkAntiLagUpdateAMD o_vkAntiLagUpdateAMD;

    static void Hook(HMODULE vulkan1);
    static void Unhook();
};
