#pragma once
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <utility>
#include <atomic>
#include <type_traits>
namespace NrFgSdk {
extern void (*enter)(bool);extern void (*leave)(bool);inline std::atomic<VkResult> failure{VK_SUCCESS};
struct Guard {bool queue;Guard(bool q):queue(q){if(enter)enter(q);}~Guard(){if(leave)leave(queue);}};
template<class Fn,class...Args>auto Invoke(bool q,Fn fn,Args&&... args){
 Guard g(q);
 if constexpr(std::is_same_v<std::invoke_result_t<Fn,Args...>,VkResult>){
  if(failure.load(std::memory_order_relaxed)!=VK_SUCCESS)return VK_ERROR_DEVICE_LOST;
  const auto result=fn(std::forward<Args>(args)...);
  if(q&&result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR&&result!=VK_ERROR_OUT_OF_DATE_KHR)failure.store(result,std::memory_order_relaxed);
  return result;
 }else return fn(std::forward<Args>(args)...);
}
inline VkResult Wait(VkDevice device,const VkSemaphoreWaitInfo* info,uint64_t timeout){
 Guard g(false);if(failure.load(std::memory_order_relaxed)!=VK_SUCCESS)return VK_ERROR_DEVICE_LOST;
 const auto bounded=timeout==UINT64_MAX?5000000000ull:timeout;const auto result=vkWaitSemaphores(device,info,bounded);
 if(timeout==UINT64_MAX&&result!=VK_SUCCESS)failure.store(result,std::memory_order_relaxed);return result;
}
}

#define vkAcquireNextImageKHR(...) NrFgSdk::Invoke(false,vkAcquireNextImageKHR,__VA_ARGS__)
#define vkAllocateCommandBuffers(...) NrFgSdk::Invoke(false,vkAllocateCommandBuffers,__VA_ARGS__)
#define vkAllocateDescriptorSets(...) NrFgSdk::Invoke(false,vkAllocateDescriptorSets,__VA_ARGS__)
#define vkAllocateMemory(...) NrFgSdk::Invoke(false,vkAllocateMemory,__VA_ARGS__)
#define vkBeginCommandBuffer(...) NrFgSdk::Invoke(false,vkBeginCommandBuffer,__VA_ARGS__)
#define vkBindImageMemory(...) NrFgSdk::Invoke(false,vkBindImageMemory,__VA_ARGS__)
#define vkCmdBeginRenderPass(...) NrFgSdk::Invoke(false,vkCmdBeginRenderPass,__VA_ARGS__)
#define vkCmdBindDescriptorSets(...) NrFgSdk::Invoke(false,vkCmdBindDescriptorSets,__VA_ARGS__)
#define vkCmdBindPipeline(...) NrFgSdk::Invoke(false,vkCmdBindPipeline,__VA_ARGS__)
#define vkCmdCopyImage(...) NrFgSdk::Invoke(false,vkCmdCopyImage,__VA_ARGS__)
#define vkCmdDraw(...) NrFgSdk::Invoke(false,vkCmdDraw,__VA_ARGS__)
#define vkCmdEndRenderPass(...) NrFgSdk::Invoke(false,vkCmdEndRenderPass,__VA_ARGS__)
#define vkCmdPipelineBarrier(...) NrFgSdk::Invoke(false,vkCmdPipelineBarrier,__VA_ARGS__)
#define vkCmdPushConstants(...) NrFgSdk::Invoke(false,vkCmdPushConstants,__VA_ARGS__)
#define vkCmdSetScissor(...) NrFgSdk::Invoke(false,vkCmdSetScissor,__VA_ARGS__)
#define vkCmdSetViewport(...) NrFgSdk::Invoke(false,vkCmdSetViewport,__VA_ARGS__)
#define vkCreateCommandPool(...) NrFgSdk::Invoke(false,vkCreateCommandPool,__VA_ARGS__)
#define vkCreateDescriptorPool(...) NrFgSdk::Invoke(false,vkCreateDescriptorPool,__VA_ARGS__)
#define vkCreateDescriptorSetLayout(...) NrFgSdk::Invoke(false,vkCreateDescriptorSetLayout,__VA_ARGS__)
#define vkCreateFramebuffer(...) NrFgSdk::Invoke(false,vkCreateFramebuffer,__VA_ARGS__)
#define vkCreateGraphicsPipelines(...) NrFgSdk::Invoke(false,vkCreateGraphicsPipelines,__VA_ARGS__)
#define vkCreateImage(...) NrFgSdk::Invoke(false,vkCreateImage,__VA_ARGS__)
#define vkCreateImageView(...) NrFgSdk::Invoke(false,vkCreateImageView,__VA_ARGS__)
#define vkCreatePipelineLayout(...) NrFgSdk::Invoke(false,vkCreatePipelineLayout,__VA_ARGS__)
#define vkCreateRenderPass(...) NrFgSdk::Invoke(false,vkCreateRenderPass,__VA_ARGS__)
#define vkCreateSemaphore(...) NrFgSdk::Invoke(false,vkCreateSemaphore,__VA_ARGS__)
#define vkCreateShaderModule(...) NrFgSdk::Invoke(false,vkCreateShaderModule,__VA_ARGS__)
#define vkCreateSwapchainKHR(...) NrFgSdk::Invoke(false,vkCreateSwapchainKHR,__VA_ARGS__)
#define vkDestroyCommandPool(...) NrFgSdk::Invoke(false,vkDestroyCommandPool,__VA_ARGS__)
#define vkDestroyDescriptorPool(...) NrFgSdk::Invoke(false,vkDestroyDescriptorPool,__VA_ARGS__)
#define vkDestroyDescriptorSetLayout(...) NrFgSdk::Invoke(false,vkDestroyDescriptorSetLayout,__VA_ARGS__)
#define vkDestroyFramebuffer(...) NrFgSdk::Invoke(false,vkDestroyFramebuffer,__VA_ARGS__)
#define vkDestroyImage(...) NrFgSdk::Invoke(false,vkDestroyImage,__VA_ARGS__)
#define vkDestroyImageView(...) NrFgSdk::Invoke(false,vkDestroyImageView,__VA_ARGS__)
#define vkDestroyPipeline(...) NrFgSdk::Invoke(false,vkDestroyPipeline,__VA_ARGS__)
#define vkDestroyPipelineLayout(...) NrFgSdk::Invoke(false,vkDestroyPipelineLayout,__VA_ARGS__)
#define vkDestroyRenderPass(...) NrFgSdk::Invoke(false,vkDestroyRenderPass,__VA_ARGS__)
#define vkDestroySemaphore(...) NrFgSdk::Invoke(false,vkDestroySemaphore,__VA_ARGS__)
#define vkDestroyShaderModule(...) NrFgSdk::Invoke(false,vkDestroyShaderModule,__VA_ARGS__)
#define vkDestroySwapchainKHR(...) NrFgSdk::Invoke(false,vkDestroySwapchainKHR,__VA_ARGS__)
#define vkDeviceWaitIdle(...) NrFgSdk::Invoke(true,vkDeviceWaitIdle,__VA_ARGS__)
#define vkEndCommandBuffer(...) NrFgSdk::Invoke(false,vkEndCommandBuffer,__VA_ARGS__)
#define vkFreeCommandBuffers(...) NrFgSdk::Invoke(false,vkFreeCommandBuffers,__VA_ARGS__)
#define vkFreeDescriptorSets(...) NrFgSdk::Invoke(false,vkFreeDescriptorSets,__VA_ARGS__)
#define vkFreeMemory(...) NrFgSdk::Invoke(false,vkFreeMemory,__VA_ARGS__)
#define vkGetDeviceProcAddr(...) NrFgSdk::Invoke(false,vkGetDeviceProcAddr,__VA_ARGS__)
#define vkGetImageMemoryRequirements(...) NrFgSdk::Invoke(false,vkGetImageMemoryRequirements,__VA_ARGS__)
#define vkGetPhysicalDeviceMemoryProperties(...) NrFgSdk::Invoke(false,vkGetPhysicalDeviceMemoryProperties,__VA_ARGS__)
#define vkGetPhysicalDeviceQueueFamilyProperties(...) NrFgSdk::Invoke(false,vkGetPhysicalDeviceQueueFamilyProperties,__VA_ARGS__)
#define vkGetPhysicalDeviceSurfaceSupportKHR(...) NrFgSdk::Invoke(false,vkGetPhysicalDeviceSurfaceSupportKHR,__VA_ARGS__)
#define vkGetSemaphoreCounterValue(...) NrFgSdk::Invoke(false,vkGetSemaphoreCounterValue,__VA_ARGS__)
#define vkGetSwapchainImagesKHR(...) NrFgSdk::Invoke(false,vkGetSwapchainImagesKHR,__VA_ARGS__)
#define vkQueuePresentKHR(...) NrFgSdk::Invoke(true,vkQueuePresentKHR,__VA_ARGS__)
#define vkQueueSubmit(...) NrFgSdk::Invoke(true,vkQueueSubmit,__VA_ARGS__)
#define vkQueueWaitIdle(...) NrFgSdk::Invoke(true,vkQueueWaitIdle,__VA_ARGS__)
#define vkResetCommandBuffer(...) NrFgSdk::Invoke(false,vkResetCommandBuffer,__VA_ARGS__)
#define vkResetCommandPool(...) NrFgSdk::Invoke(false,vkResetCommandPool,__VA_ARGS__)
#define vkUpdateDescriptorSets(...) NrFgSdk::Invoke(false,vkUpdateDescriptorSets,__VA_ARGS__)
#define vkWaitSemaphores(...) NrFgSdk::Wait(__VA_ARGS__)
