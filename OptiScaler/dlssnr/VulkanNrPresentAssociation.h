#pragma once
#include "VulkanNrPresentFrame.h"
#include "VulkanPresentRegistry.h"
#include "VulkanPresentGuides.h"
namespace DlssNr {
// Called at the application Present boundary with its consumed public marker
// scope and exact WSI snapshot. No image equality with an earlier SR output is
// required: the guide owner separately selects the unique successful evaluation
// for provider/frame/viewport and proves its submission and resource lifetime.
inline std::optional<VkNrPresentGuideTag> MakeVkApplicationPresentTag(
    const VkNrPublicPresentFrame& frame,const VkObservedPresent& present)
{
    const auto& p=present.request;
    if(!frame.provider||frame.viewport==UINT32_MAX||!present.device||!present.queue||
       present.queueFamily==UINT32_MAX||!present.image||!present.swapchain||p.swapchainCount!=1||
       !p.acquiredObserved||!p.deviceGeneration||!p.swapchainGeneration||!p.acquireGeneration||
       p.imageIndex==UINT32_MAX||!p.extent.width||!p.extent.height||p.format==VK_FORMAT_UNDEFINED)return {};
    VkNrPresentGuideTag tag;tag.providerGeneration=frame.provider;tag.frameToken=frame.frame;tag.viewport=frame.viewport;
    tag.image=present.image;auto& target=tag.target;
    target.deviceGeneration=p.deviceGeneration;target.swapchainGeneration=p.swapchainGeneration;
    target.swapchain=present.swapchain;target.swapchainImageIndex=p.imageIndex;target.acquireGeneration=p.acquireGeneration;
    target.queue=present.queue;target.queueFamily=present.queueFamily;target.output=p.extent;
    target.representation.format=p.format;target.representation.colorSpace=p.colorSpace;
    return tag;
}
}
