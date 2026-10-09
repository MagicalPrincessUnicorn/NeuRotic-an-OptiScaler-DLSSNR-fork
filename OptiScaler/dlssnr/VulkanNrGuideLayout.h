#pragma once
#include "VulkanNrInputLayout.h"

namespace DlssNr
{
// Called before authenticated NGX SR or public Streamline RR invocation.
// NVIDIA sl.dlss_d transitions depth/motion to eTextureRead before NGX, mapped
// by sl.chi Vulkan to SHADER_READ_ONLY_OPTIMAL. This does not authorize arbitrary
// RR integrations, nor override conflicting recorded barriers. Sampling a depth-only
// view does not establish the stencil aspect of a combined depth/stencil image.
inline VkNrInputLayoutDecision ResolveVkNrGuideLayout(bool ngxGuideInputReadContract,
    VkImageAspectFlags barrierAspect, std::optional<VkImageLayout> observed,
    std::optional<VkImageSubresourceRange> range)
{
    if (observed || range) {
        if (!observed || !range)
            return {false,VK_IMAGE_LAYOUT_UNDEFINED,"incomplete-observation","Guide layout observation is incomplete"};
        if ((range->aspectMask & barrierAspect) != barrierAspect || range->baseMipLevel ||
            range->baseArrayLayer || !range->levelCount || !range->layerCount ||
            (*observed != VK_IMAGE_LAYOUT_GENERAL && *observed != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             *observed != VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL &&
             !(barrierAspect & VK_IMAGE_ASPECT_DEPTH_BIT && *observed == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL)))
            return {false,*observed,"conflicting-observation","Guide barrier does not cover the readable input aspects"};
        return {true,*observed,"recording-barrier",""};
    }
    if (ngxGuideInputReadContract && (barrierAspect == VK_IMAGE_ASPECT_COLOR_BIT || barrierAspect == VK_IMAGE_ASPECT_DEPTH_BIT))
        return {true,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,"ngx-guide-input-convention",""};
    return {false,VK_IMAGE_LAYOUT_UNDEFINED,"unavailable","Guide source layout lacks complete observation or NGX input contract"};
}
}
