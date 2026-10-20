#pragma once
#include <vulkan/vulkan.h>
#include "../../OptiScaler/dlssnr/VulkanNrCompletion.h"
#include <vector>
#include <memory>
#include <stdexcept>
#include <string>
#include <cstdio>
#include <cstring>
#include <atomic>
struct VkGpuUnavailable:std::runtime_error {using std::runtime_error::runtime_error;};
inline void VkGpuCheck(VkResult r,const char* operation){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+" result="+std::to_string(r));}
struct VkGpuHost {
    struct Image {VkImage image{};VkImageView view{};VkDeviceMemory memory{};VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED;VkFormat format{};};
    struct Buffer {VkBuffer buffer{};VkDeviceMemory memory{};void* mapped=nullptr;VkDeviceSize bytes=0;};
    VkInstance instance{};VkPhysicalDevice physical{};VkDevice device{};VkQueue queue{};uint32_t family=UINT32_MAX;
    VkCommandPool pool{};VkCommandBuffer command{};VkExtent2D extent{32,8};
    std::vector<std::unique_ptr<Image>> images;std::vector<std::unique_ptr<Buffer>> buffers;
    VkPhysicalDeviceMemoryProperties memoryProperties{};
    VkDebugUtilsMessengerEXT messenger{};bool validation=false;std::atomic<unsigned> validationErrors{0};
    bool storageWriteWithoutFormatEnabled=false;
    DlssNr::VkNrUseId use;uint64_t submitted=0,completed=0;
    static VKAPI_ATTR VkBool32 VKAPI_CALL Debug(VkDebugUtilsMessageSeverityFlagBitsEXT severity,VkDebugUtilsMessageTypeFlagsEXT,
        const VkDebugUtilsMessengerCallbackDataEXT* data,void* self){
        if(severity&VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT){++static_cast<VkGpuHost*>(self)->validationErrors;std::fprintf(stderr,"VALIDATION: %s\n",data->pMessage);}return VK_FALSE;
    }
    VkGpuHost(){
        uint32_t count=0;VkGpuCheck(vkEnumerateInstanceLayerProperties(&count,nullptr),"enumerate layers");
        std::vector<VkLayerProperties> layers(count);VkGpuCheck(vkEnumerateInstanceLayerProperties(&count,layers.data()),"read layers");
        for(const auto& layer:layers)if(std::strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation")==0)validation=true;
        const char* layer="VK_LAYER_KHRONOS_validation";const char* extension=VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
        VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="NeuRotic non-proprietary Vulkan fixture";app.apiVersion=VK_API_VERSION_1_1;
        VkDebugUtilsMessengerCreateInfoEXT debug{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
        debug.messageSeverity=VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;debug.messageType=VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT|VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
        debug.pfnUserCallback=Debug;debug.pUserData=this;
        VkInstanceCreateInfo create{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};create.pApplicationInfo=&app;
        if(validation){create.enabledLayerCount=1;create.ppEnabledLayerNames=&layer;create.enabledExtensionCount=1;create.ppEnabledExtensionNames=&extension;create.pNext=&debug;}
        const auto instanceResult=vkCreateInstance(&create,nullptr,&instance);
        if(instanceResult!=VK_SUCCESS)throw VkGpuUnavailable("Vulkan instance unavailable: "+std::to_string(instanceResult));
        if(validation){auto fn=reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkCreateDebugUtilsMessengerEXT"));if(fn)VkGpuCheck(fn(instance,&debug,nullptr,&messenger),"debug messenger");}
        VkGpuCheck(vkEnumeratePhysicalDevices(instance,&count,nullptr),"enumerate devices");
        std::vector<VkPhysicalDevice> devices(count);VkGpuCheck(vkEnumeratePhysicalDevices(instance,&count,devices.data()),"read devices");
        for(auto candidate:devices){uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(candidate,&n,qs.data());
            for(uint32_t i=0;i<n;++i)std::printf("GPU queue family=%u count=%u flags=0x%x\n",i,qs[i].queueCount,unsigned(qs[i].queueFlags));
            for(uint32_t i=0;i<n;++i)if(qs[i].queueCount&&(qs[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){physical=candidate;family=i;break;}if(physical)break;}
        if(!physical)throw VkGpuUnavailable("No Vulkan compute queue");
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(physical,&properties);
        VkPhysicalDeviceFeatures features{};vkGetPhysicalDeviceFeatures(physical,&features);
        VkPhysicalDeviceFeatures enabled{};enabled.shaderStorageImageExtendedFormats=features.shaderStorageImageExtendedFormats;
        enabled.shaderStorageImageWriteWithoutFormat=features.shaderStorageImageWriteWithoutFormat;enabled.shaderStorageImageReadWithoutFormat=features.shaderStorageImageReadWithoutFormat;
        float priority=1;VkDeviceQueueCreateInfo qc{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qc.queueFamilyIndex=family;qc.queueCount=1;qc.pQueuePriorities=&priority;
        VkDeviceCreateInfo dc{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dc.queueCreateInfoCount=1;dc.pQueueCreateInfos=&qc;dc.pEnabledFeatures=&enabled;
        VkGpuCheck(vkCreateDevice(physical,&dc,nullptr,&device),"create device");vkGetDeviceQueue(device,family,0,&queue);vkGetPhysicalDeviceMemoryProperties(physical,&memoryProperties);
        storageWriteWithoutFormatEnabled=enabled.shaderStorageImageWriteWithoutFormat==VK_TRUE;
        DlssNr::RegisterVkNrCompletionDevice(device,{vkCreateFence,vkDestroyFence,vkGetFenceStatus,vkQueueSubmit,vkWaitForFences});
        DlssNr::RegisterVkNrCompletionQueue(device,queue,family);
        VkCommandPoolCreateInfo pc{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};pc.queueFamilyIndex=family;pc.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VkGpuCheck(vkCreateCommandPool(device,&pc,nullptr,&pool),"command pool");
        VkCommandBufferAllocateInfo ca{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ca.commandPool=pool;ca.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ca.commandBufferCount=1;
        VkGpuCheck(vkAllocateCommandBuffers(device,&ca,&command),"command buffer");DlssNr::VulkanNrRecordings().OnAllocate(device,pool,command,family);
        std::printf("GPU device=%s driver=%u api=%u validation=%s\n",properties.deviceName,properties.driverVersion,properties.apiVersion,validation?"available":"UNAVAILABLE");
    }
    uint32_t MemoryType(uint32_t bits,VkMemoryPropertyFlags flags){for(uint32_t i=0;i<memoryProperties.memoryTypeCount;++i)if((bits&(1u<<i))&&(memoryProperties.memoryTypes[i].propertyFlags&flags)==flags)return i;throw VkGpuUnavailable("Required Vulkan memory type unavailable");}
    Image& MakeImage(VkFormat format,VkImageUsageFlags usage){
        auto image=std::make_unique<Image>();image->format=format;
        VkImageCreateInfo ci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};ci.imageType=VK_IMAGE_TYPE_2D;ci.format=format;ci.extent={extent.width,extent.height,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=usage;
        VkGpuCheck(vkCreateImage(device,&ci,nullptr,&image->image),"create fixture image");VkMemoryRequirements mr{};vkGetImageMemoryRequirements(device,image->image,&mr);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=mr.size;ma.memoryTypeIndex=MemoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        VkGpuCheck(vkAllocateMemory(device,&ma,nullptr,&image->memory),"allocate image");VkGpuCheck(vkBindImageMemory(device,image->image,image->memory,0),"bind image");
        if(usage&(VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT)){VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image->image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;view.format=format;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkGpuCheck(vkCreateImageView(device,&view,nullptr,&image->view),"image view");}
        auto* result=image.get();images.push_back(std::move(image));return *result;
    }
    Buffer& MakeBuffer(VkDeviceSize bytes,VkBufferUsageFlags usage){
        auto buffer=std::make_unique<Buffer>();buffer->bytes=bytes;VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;ci.usage=usage;
        VkGpuCheck(vkCreateBuffer(device,&ci,nullptr,&buffer->buffer),"buffer");VkMemoryRequirements mr{};vkGetBufferMemoryRequirements(device,buffer->buffer,&mr);
        VkMemoryAllocateInfo ma{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ma.allocationSize=mr.size;ma.memoryTypeIndex=MemoryType(mr.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        VkGpuCheck(vkAllocateMemory(device,&ma,nullptr,&buffer->memory),"buffer memory");VkGpuCheck(vkBindBufferMemory(device,buffer->buffer,buffer->memory,0),"bind buffer");VkGpuCheck(vkMapMemory(device,buffer->memory,0,bytes,0,&buffer->mapped),"map buffer");
        auto* result=buffer.get();buffers.push_back(std::move(buffer));return *result;
    }
    void Begin(VkCommandBufferUsageFlags flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT){
        VkGpuCheck(vkResetCommandBuffer(command,0),"reset");DlssNr::VulkanNrRecordings().OnReset(command,VK_SUCCESS);
        VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=flags;
        VkGpuCheck(vkBeginCommandBuffer(command,&begin),"begin");DlssNr::VulkanNrRecordings().OnBegin(command,begin.flags,VK_SUCCESS);
        auto reserved=DlssNr::VulkanNrRecordings().Reserve(command,DlssNr::VkNrCompletionDeviceGeneration(device));
        if(!reserved||!DlssNr::ReserveVkNrCompletion(device,*reserved))throw std::runtime_error("actual fixture recording/completion reserve failed");use=*reserved;
    }
    void Move(Image& image,VkImageLayout to){
        VkImageMemoryBarrier barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};barrier.oldLayout=image.layout;barrier.newLayout=to;
        barrier.srcAccessMask=image.layout==VK_IMAGE_LAYOUT_UNDEFINED?0:VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.image=image.image;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&barrier);image.layout=to;
    }
    VkBufferImageCopy Region(){VkBufferImageCopy r{};r.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};r.imageExtent={extent.width,extent.height,1};return r;}
    void Upload(Buffer& from,Image& to){Move(to,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);auto r=Region();vkCmdCopyBufferToImage(command,from.buffer,to.image,to.layout,1,&r);}
    void Read(Image& from,Buffer& to){Move(from,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);auto r=Region();vkCmdCopyImageToBuffer(command,from.image,from.layout,to.buffer,1,&r);
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=to.buffer;b.size=to.bytes;
        vkCmdPipelineBarrier(command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);}
    void Submit(bool reset=true){
        VkGpuCheck(vkEndCommandBuffer(command),"end");DlssNr::VulkanNrRecordings().OnEnd(command,VK_SUCCESS);
        VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        VkGpuCheck(DlssNr::ObserveVkNrQueueSubmit(queue,{&command,1},[&]{return vkQueueSubmit(queue,1,&submit,VK_NULL_HANDLE);}),"actual submit");++submitted;
        VkGpuCheck(vkQueueWaitIdle(queue),"fixture completion wait");DlssNr::PollVkNrCompletions();
        if(!DlssNr::VulkanNrRecordings().GpuComplete(use))throw std::runtime_error("GPU completion receipt unavailable");++completed;
        if(reset){VkGpuCheck(vkResetCommandBuffer(command,0),"terminal reset");DlssNr::VulkanNrRecordings().OnReset(command,VK_SUCCESS);}
    }
    ~VkGpuHost(){
        if(device){vkDeviceWaitIdle(device);DlssNr::ObserveVkNrDeviceDestroy(device,[&]{
            if(pool)vkDestroyCommandPool(device,pool,nullptr);
            for(auto& image:images){if(image->view)vkDestroyImageView(device,image->view,nullptr);vkDestroyImage(device,image->image,nullptr);vkFreeMemory(device,image->memory,nullptr);}
            for(auto& buffer:buffers){vkUnmapMemory(device,buffer->memory);vkDestroyBuffer(device,buffer->buffer,nullptr);vkFreeMemory(device,buffer->memory,nullptr);}vkDestroyDevice(device,nullptr);
        });}
        if(messenger){auto destroy=reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(instance,"vkDestroyDebugUtilsMessengerEXT"));if(destroy)destroy(instance,messenger,nullptr);}if(instance)vkDestroyInstance(instance,nullptr);
    }
};
