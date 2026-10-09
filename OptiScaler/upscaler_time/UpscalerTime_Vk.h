#pragma once
#include <vulkan/vulkan.h>

class UpscalerTimeVk
{
  public:
    static void Init(VkDevice device, VkPhysicalDevice pd);
    static void UpscaleStart(VkCommandBuffer cmdBuffer);
    static void UpscaleEnd(VkCommandBuffer cmdBuffer);
    static void ReadUpscalingTime(VkDevice device);

    // A Vulkan-on-D3D12 backend publishes its own D3D12 measurement. Removing
    // this legacy Vulkan timer must not hide that provider's valid readout.
    static constexpr bool UnavailableFor(bool vulkanInput, bool usesDx12) noexcept
    { return vulkanInput && !usesDx12; }
};
