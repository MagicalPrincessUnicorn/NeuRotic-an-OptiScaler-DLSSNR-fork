#pragma once
#include "VulkanNrFrameContract.h"
namespace DlssNr {
struct VkPresentColorRecipe
{
    bool supported = false;
    bool bgra = false;
    bool srgb = false;
    VkFilter filter = VK_FILTER_NEAREST;
    bool hdr=false,signedOriginal=false;
    float whitePoint=1.0f;
    uint64_t revision=1;
};

inline bool VkPresentNeedsView(VkImageUsageFlags usage) noexcept
{
    return (usage & (VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT)) != 0;
}

inline VkPresentColorRecipe SelectVkPresentColorRecipe(VkFormat format) noexcept
{
    switch (format)
    {
    case VK_FORMAT_R8G8B8A8_UNORM: return { true, false, false, VK_FILTER_NEAREST };
    case VK_FORMAT_B8G8R8A8_UNORM: return { true, true, false, VK_FILTER_NEAREST };
    case VK_FORMAT_R8G8B8A8_SRGB: return { true, false, true, VK_FILTER_NEAREST };
    case VK_FORMAT_B8G8R8A8_SRGB: return { true, true, true, VK_FILTER_NEAREST };
    default: return {};
    }
}

inline VkPresentColorRecipe SelectVkPresentColorRecipe(VkFormat format,VkColorSpaceKHR colorSpace) noexcept
{
    if(colorSpace==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)return SelectVkPresentColorRecipe(format);
    if(colorSpace==VK_COLOR_SPACE_HDR10_ST2084_EXT&&
        (format==VK_FORMAT_A2B10G10R10_UNORM_PACK32||format==VK_FORMAT_A2R10G10B10_UNORM_PACK32))
        return {true,false,false,VK_FILTER_NEAREST,true,true,2.5f,2};
    if(colorSpace==VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT&&format==VK_FORMAT_R16G16B16A16_SFLOAT)
        return {true,false,false,VK_FILTER_NEAREST,true,true,2.5f,3};
    return {};
}
}
