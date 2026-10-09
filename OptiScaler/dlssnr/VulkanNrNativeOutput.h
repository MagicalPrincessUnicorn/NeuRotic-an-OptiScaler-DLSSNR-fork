#pragma once
#include <vulkan/vulkan.h>

namespace DlssNr {
// The final Native composition replaces the caller's NGX output after the upscaler
// has returned. Publish that write beyond our compute-only intermediate barriers.
// This covers subsequent commands on the same queue; the caller still owns layout
// transitions, cross-queue synchronization and queue-family ownership transfers.
inline void PublishVkNrNativeOutput(VkCommandBuffer command,VkImage target,VkImageSubresourceRange range)
{
    VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
    barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
    barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    barrier.image=target;
    barrier.subresourceRange=range;
    vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
        0,0,nullptr,0,nullptr,1,&barrier);
}
}
