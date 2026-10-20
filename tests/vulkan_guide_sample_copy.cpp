#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <unordered_map>
#include <memory>
#include <cstdint>
#include <cstring>
#include <cassert>
#include <iostream>
#include "shaders/dlssnr/precompile/GuideCopy_Shader_Vk.h"
struct VkNrRect {uint32_t x,y,width,height;};
struct NVSDK_NGX_Resource_VK {int Type=0;bool ReadWrite=false;struct {struct {VkImage Image{};VkImageView ImageView{};VkImageSubresourceRange SubresourceRange{};VkFormat Format{};uint32_t Width=0,Height=0;} ImageViewInfo;} Resource;};
static uintptr_t nextHandle=1;
template<class T>T Handle(){return reinterpret_cast<T>(nextHandle++);}
static unsigned pipelinesCreated=0,pipelinesDestroyed=0,poolsDestroyed=0,buffersDestroyed=0,memoryFreed=0;
static bool failAllocate=false;
static std::unordered_map<VkDeviceMemory,std::array<uint32_t,4>> constants;
static std::array<VkDescriptorImageInfo,2> capturedSource{},capturedDestination{};
static unsigned updates=0;
extern "C" {
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorSetLayout(VkDevice,const VkDescriptorSetLayoutCreateInfo* p,const VkAllocationCallbacks*,VkDescriptorSetLayout* out){assert(p->bindingCount==3);*out=Handle<VkDescriptorSetLayout>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorSetLayout(VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*){}
VKAPI_ATTR VkResult VKAPI_CALL vkCreatePipelineLayout(VkDevice,const VkPipelineLayoutCreateInfo* p,const VkAllocationCallbacks*,VkPipelineLayout* out){assert(p->pushConstantRangeCount==0&&p->setLayoutCount==1);*out=Handle<VkPipelineLayout>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipelineLayout(VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateShaderModule(VkDevice,const VkShaderModuleCreateInfo* p,const VkAllocationCallbacks*,VkShaderModule* out){assert((reinterpret_cast<uintptr_t>(p->pCode)&3)==0&&p->pCode[0]==0x07230203);*out=Handle<VkShaderModule>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyShaderModule(VkDevice,VkShaderModule,const VkAllocationCallbacks*){}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateComputePipelines(VkDevice,VkPipelineCache,uint32_t count,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* out){assert(count==1);*out=Handle<VkPipeline>();++pipelinesCreated;return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyPipeline(VkDevice,VkPipeline,const VkAllocationCallbacks*){++pipelinesDestroyed;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateDescriptorPool(VkDevice,const VkDescriptorPoolCreateInfo* p,const VkAllocationCallbacks*,VkDescriptorPool* out){assert(p->maxSets==2);*out=Handle<VkDescriptorPool>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyDescriptorPool(VkDevice,VkDescriptorPool,const VkAllocationCallbacks*){++poolsDestroyed;}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateDescriptorSets(VkDevice,const VkDescriptorSetAllocateInfo* p,VkDescriptorSet* out){for(uint32_t i=0;i<p->descriptorSetCount;++i)out[i]=Handle<VkDescriptorSet>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkGetPhysicalDeviceMemoryProperties(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* p){*p={};p->memoryTypeCount=1;p->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT|VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;}
VKAPI_ATTR VkResult VKAPI_CALL vkCreateBuffer(VkDevice,const VkBufferCreateInfo* p,const VkAllocationCallbacks*,VkBuffer* out){assert(p->size==16&&p->usage==VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);*out=Handle<VkBuffer>();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkDestroyBuffer(VkDevice,VkBuffer,const VkAllocationCallbacks*){++buffersDestroyed;}
VKAPI_ATTR void VKAPI_CALL vkGetBufferMemoryRequirements(VkDevice,VkBuffer,VkMemoryRequirements* p){*p={256,256,1};}
VKAPI_ATTR VkResult VKAPI_CALL vkAllocateMemory(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory* out){if(failAllocate)return VK_ERROR_OUT_OF_DEVICE_MEMORY;*out=Handle<VkDeviceMemory>();constants[*out]={};return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkFreeMemory(VkDevice,VkDeviceMemory m,const VkAllocationCallbacks*){++memoryFreed;constants.erase(m);}
VKAPI_ATTR VkResult VKAPI_CALL vkBindBufferMemory(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
VKAPI_ATTR VkResult VKAPI_CALL vkMapMemory(VkDevice,VkDeviceMemory memory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** out){*out=constants[memory].data();return VK_SUCCESS;}
VKAPI_ATTR void VKAPI_CALL vkUnmapMemory(VkDevice,VkDeviceMemory){}
VKAPI_ATTR void VKAPI_CALL vkUpdateDescriptorSets(VkDevice,uint32_t count,const VkWriteDescriptorSet* p,uint32_t,const VkCopyDescriptorSet*){assert(count==3&&p[2].pBufferInfo->range==16);capturedSource[updates%2]=*p[0].pImageInfo;capturedDestination[updates%2]=*p[1].pImageInfo;++updates;}
}
#include "dlssnr/VulkanNrGuideSampleCopy.inl"
#ifndef GUIDE_RECORDING_TEST
int main(){
 auto device=Handle<VkDevice>();auto physical=Handle<VkPhysicalDevice>();
 NVSDK_NGX_Resource_VK d{},m{};d.Resource.ImageViewInfo.ImageView=Handle<VkImageView>();m.Resource.ImageViewInfo.ImageView=Handle<VkImageView>();
 std::array<NVSDK_NGX_Resource_VK*,2> source{&d,&m};std::array<VkImageView,2> destination{Handle<VkImageView>(),Handle<VkImageView>()};
 std::array<VkImageLayout,2> layouts{VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL};
 std::array<VkNrRect,2> rects{{{17,29,1920,1080},{0,0,3840,2160}}};uint64_t budget=512,bytes=0;
 GuideCopyBindings first;assert(PrepareGuideCopyBindings(first,device,physical,10,source,destination,layouts,rects,budget,bytes));
 assert(budget==0&&bytes==512&&pipelinesCreated==2);
 assert((constants[first.memory[0]]==std::array<uint32_t,4>{17,29,1920,1080}));
 assert((constants[first.memory[1]]==std::array<uint32_t,4>{0,0,3840,2160}));
 assert(capturedSource[0].imageView==d.Resource.ImageViewInfo.ImageView&&capturedSource[0].imageLayout==layouts[0]);
 assert(capturedDestination[1].imageView==destination[1]&&capturedDestination[1].imageLayout==VK_IMAGE_LAYOUT_GENERAL);
 GuideCopyBindings second;budget=512;bytes=0;rects[0].x=99;
 assert(PrepareGuideCopyBindings(second,device,physical,10,source,destination,layouts,rects,budget,bytes));
 assert(pipelinesCreated==2&&first.program==second.program&&first.set!=second.set&&first.memory!=second.memory);
 assert(constants[first.memory[0]][0]==17&&constants[second.memory[0]][0]==99);
 assert(!GetGuideCopyProgram(device,11));
 first.Release(device);assert(pipelinesDestroyed==0&&poolsDestroyed==1&&buffersDestroyed==2&&memoryFreed==2);
 second.Release(device);assert(pipelinesDestroyed==0);
 GuideCopyBindings capacity;budget=255;bytes=0;assert(!PrepareGuideCopyBindings(capacity,device,physical,10,source,destination,layouts,rects,budget,bytes));capacity.Release(device);
 assert(budget==255&&bytes==0);
 failAllocate=true;GuideCopyBindings failed;budget=512;bytes=0;assert(!PrepareGuideCopyBindings(failed,device,physical,10,source,destination,layouts,rects,budget,bytes));failed.Release(device);failAllocate=false;
 GuideCopyBindings abandoned;budget=512;bytes=0;assert(PrepareGuideCopyBindings(abandoned,device,physical,10,source,destination,layouts,rects,budget,bytes));
 const auto released=poolsDestroyed+buffersDestroyed+memoryFreed;
 // A global owner abandonment during device B teardown must not poison A's cache.
 auto deviceB=Handle<VkDevice>();auto programB=GetGuideCopyProgram(deviceB,21);
 abandoned.Abandon();programB->Abandon();guideCopyPrograms.erase(deviceB);programB.reset();
 assert(GetGuideCopyProgram(device,10)->pipeline[0]);guideCopyPrograms.at(device)->Abandon();guideCopyPrograms.erase(device);
 assert(pipelinesDestroyed==0&&released==poolsDestroyed+buffersDestroyed+memoryFreed);
 auto other=GetGuideCopyProgram(Handle<VkDevice>(),11);auto otherDevice=other->device;other.reset();guideCopyPrograms.erase(otherDevice);assert(pipelinesDestroyed==2);
 std::cout<<"PASS sampled guide actual helper: exact rectangles/layouts, immutable descriptors/constants, two cached pipelines, generation/budget/failure/abandon lifetime\n";
}

#endif
