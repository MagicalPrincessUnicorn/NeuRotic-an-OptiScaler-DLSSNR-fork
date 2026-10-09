#include "pch.h"
#include "VulkanNrImageSnapshot.h"
#include "VulkanNrImageFacts.h"
#include <hooks/VulkanwDx12_Hooks.h>
namespace DlssNr {
void ReleaseVkNrImages(VkNrSnapshotImages& s,bool alive){
 if(alive)for(size_t i=0;i<3;++i){if(s.views[i])vkDestroyImageView(s.device,s.views[i],nullptr);
  if(s.images[i])vkDestroyImage(s.device,s.images[i],nullptr);if(s.memory[i])vkFreeMemory(s.device,s.memory[i],nullptr);}
 s={};
}
bool CaptureVkNrImages(const VkFrameRequest& frame,VkNrSnapshotImages& snapshot,uint64_t budget,std::string& reason){
 auto refuse=[&](const char* why){reason=why;return false;};
 if(snapshot.bytes)return refuse("held images are immutable until their generation retires");
 if(!frame.color||!frame.depth||!frame.motion||!VkNrSnapshotMetadataQualified(frame))return refuse("Hold awaits matching color/depth/motion metadata");
 vk_state::CommandBufferState saved;
 if(!Vulkan_wDx12::cmdBufferStateTracker.CaptureNrState(frame.commandBuffer,saved))return refuse("Hold source command state unavailable");
#ifndef LOW_PRECISION_TRACKING
 std::array<NVSDK_NGX_Resource_VK*,3> source{frame.color,frame.depth,frame.motion};
 std::array<VkNrImageRights,3> rights;std::array<VkImageLayout,3> layouts;
 for(size_t i=0;i<3;++i){auto r=VulkanNrImageFacts().Rights(frame.device,*source[i],VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,i==1&&(source[i]->Resource.ImageViewInfo.SubresourceRange.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT));
  if(!r)return refuse("Hold source image/view sampled or copy rights unproved");rights[i]=*r;
  const auto image=source[i]->Resource.ImageViewInfo.Image;auto l=saved.ImageLayouts.find(image);auto range=saved.ImageLayoutRanges.find(image);
  if(l==saved.ImageLayouts.end()||range==saved.ImageLayoutRanges.end()||range->second.baseMipLevel||range->second.baseArrayLayer||
    (range->second.aspectMask&r->barrierAspect)!=r->barrierAspect||!range->second.levelCount||!range->second.layerCount||
    l->second==VK_IMAGE_LAYOUT_UNDEFINED||l->second==VK_IMAGE_LAYOUT_PREINITIALIZED)return refuse("Hold source subresource layout unproved");
  layouts[i]=l->second;VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(frame.physicalDevice,r->image.format,&props);
  const auto features=VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT;
  if((props.optimalTilingFeatures&features)!=features)return refuse("Hold source format cannot be copied and sampled");
 }
 VkNrSnapshotImages owned;owned.device=frame.device;
 for(size_t i=0;i<3;++i){auto fail=[&]{ReleaseVkNrImages(owned,true);return refuse("Hold private images exceed allocation budget or device features");};
  VkImageCreateInfo info{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};info.imageType=VK_IMAGE_TYPE_2D;info.format=rights[i].image.format;
  info.extent=rights[i].image.extent;info.mipLevels=info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;
  info.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if(vkCreateImage(frame.device,&info,nullptr,&owned.images[i])!=VK_SUCCESS)return fail();
  VkMemoryRequirements req{};vkGetImageMemoryRequirements(frame.device,owned.images[i],&req);if(req.size>budget-owned.bytes)return fail();
  VkPhysicalDeviceMemoryProperties properties{};vkGetPhysicalDeviceMemoryProperties(frame.physicalDevice,&properties);uint32_t type=UINT32_MAX;
  for(uint32_t k=0;k<properties.memoryTypeCount;++k)if((req.memoryTypeBits&(1u<<k))&&(properties.memoryTypes[k].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=k;break;}
  if(type==UINT32_MAX)return fail();VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
  if(vkAllocateMemory(frame.device,&alloc,nullptr,&owned.memory[i])!=VK_SUCCESS||vkBindImageMemory(frame.device,owned.images[i],owned.memory[i],0)!=VK_SUCCESS)return fail();
  owned.bytes+=req.size;VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=owned.images[i];view.viewType=VK_IMAGE_VIEW_TYPE_2D;
  view.format=info.format;view.subresourceRange={rights[i].copyAspect,0,1,0,1};
  if(vkCreateImageView(frame.device,&view,nullptr,&owned.views[i])!=VK_SUCCESS)return fail();
  auto& resource=owned.resources[i];resource=*source[i];resource.Resource.ImageViewInfo.Image=owned.images[i];resource.Resource.ImageViewInfo.ImageView=owned.views[i];resource.Resource.ImageViewInfo.SubresourceRange=view.subresourceRange;
 }
 auto barrier=[&](VkImage image,VkImageAspectFlags aspect,VkImageLayout from,VkImageLayout to,VkAccessFlags src,VkAccessFlags dst){
  VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.subresourceRange={aspect,0,1,0,1};b.oldLayout=from;b.newLayout=to;
  b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=src;b.dstAccessMask=dst;
  vkCmdPipelineBarrier(frame.commandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
 };
 for(size_t i=0;i<3;++i){const auto image=source[i]->Resource.ImageViewInfo.Image;
  barrier(image,rights[i].barrierAspect,layouts[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
  barrier(owned.images[i],rights[i].barrierAspect,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
  VkImageCopy region{};region.srcSubresource={rights[i].copyAspect,0,0,1};region.dstSubresource=region.srcSubresource;region.extent=rights[i].image.extent;
  vkCmdCopyImage(frame.commandBuffer,image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,owned.images[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
  barrier(image,rights[i].barrierAspect,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layouts[i],VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
  barrier(owned.images[i],rights[i].barrierAspect,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
 }
 snapshot=owned;reason.clear();return true;
#else
 (void)budget;return refuse("Hold requires full source layout observation");
#endif
}
}
