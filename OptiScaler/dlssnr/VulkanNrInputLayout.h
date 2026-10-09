#pragma once
#include <vulkan/vulkan.h>
#include <optional>

namespace DlssNr
{
struct VkNrInputLayoutDecision
{
    bool accepted = false;
    VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
    const char* basis = "unavailable";
    const char* reason = "Native Performance input layout is unavailable";
};

// NVIDIA Streamline sl.dlss transitions NGX inputs to eTextureRead, mapped by
// sl.chi Vulkan to SHADER_READ_ONLY_OPTIMAL. This is a call-boundary convention,
// NOT an observed barrier or a layout default for arbitrary Vulkan resources.
// A caller may use it only at the authenticated NGX SuperSampling input boundary.
// Creation/usage, recording, queue and lifetime rights remain separate checks.
inline VkNrInputLayoutDecision ResolveVkNrInputLayout(bool ngxSrInputContract,
    const VkImageSubresourceRange& view, std::optional<VkImageLayout> observed,
    std::optional<VkImageSubresourceRange> range)
{
    if (view.aspectMask != VK_IMAGE_ASPECT_COLOR_BIT || view.baseMipLevel || view.levelCount != 1 ||
        view.baseArrayLayer || view.layerCount != 1)
        return {false, VK_IMAGE_LAYOUT_UNDEFINED, "unavailable", "Native Performance requires a single color input subresource"};
    if (observed || range)
    {
        if (!observed || !range)
            return {false, VK_IMAGE_LAYOUT_UNDEFINED, "incomplete-observation", "Native Performance source layout observation is incomplete"};
        if (!(range->aspectMask & VK_IMAGE_ASPECT_COLOR_BIT) || range->baseMipLevel ||
            range->baseArrayLayer || !range->levelCount || !range->layerCount ||
            (*observed != VK_IMAGE_LAYOUT_GENERAL && *observed != VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             *observed != VK_IMAGE_LAYOUT_READ_ONLY_OPTIMAL))
            return {false, *observed, "conflicting-observation", "Native Performance source barrier does not cover a readable color subresource"};
        return {true, *observed, "recording-barrier", ""};
    }
    if (ngxSrInputContract)
        return {true, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL, "ngx-sr-input-convention", ""};
    return {false, VK_IMAGE_LAYOUT_UNDEFINED, "unavailable", "Native Performance source layout lacks an observation or NGX input contract"};
}
}
