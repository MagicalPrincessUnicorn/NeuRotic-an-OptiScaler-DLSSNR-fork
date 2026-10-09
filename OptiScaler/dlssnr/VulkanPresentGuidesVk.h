#pragma once
#include "VulkanPresentGuides.h"
#include "VulkanPresentRegistry.h"
#include <sl_core_types.h>
namespace DlssNr
{
void ObserveVulkanPresentTags(uint64_t provider,uint64_t frame,uint32_t viewport,const sl::ResourceTag*,uint32_t count,bool succeeded);
void RevokeVulkanPresentTags();
void CaptureVulkanPresentGuides(const VkFrameRequest&);
void CompleteVulkanGuideEvaluation(const VkNrEvaluationIdentity&,bool succeeded);
void RejectVulkanNativeGuides(const char*);
std::optional<VkNrGuideLease> SelectVulkanRecordingGuides(const VkNrFrameContract&,VkCommandBuffer,VkImage output);
std::shared_ptr<const VkNrGuideSelection> BeginVulkanGuidePresent(VkQueue queue,const VkNrPresentGuideTag* applicationFrame=nullptr);
void RetireInactiveVulkanPresentGuides(VkDevice device);
std::optional<VkNrGuideLease> SelectVulkanPresentGuides(const VkNrFrameContract&,const VkNrGuideSelection*,VkImage image=VK_NULL_HANDLE);
std::optional<VkNrFrameContract> SelectVulkanGuideMetadata(const VkNrFrameContract&);
std::optional<VkNrFrameContract> SelectVulkanResolutionMetadata(const VkNrFrameContract&,const VkNrGuideSelection*);
bool BindVulkanPresentGuides(const VkNrGuideLease&,VkNrUseId,VkQueue);
void VulkanGuidesDeviceDestroyed(VkDevice);
uint64_t VulkanGuidePrivateBytes();
uint64_t VulkanGuideContextGeneration(const VkNrFrameContract&,const VkNrGuideSelection*);
inline VkNrFrameContract MakeVulkanGuidePresentContract(const VkObservedPresent& observed,const VkNrGuideSelection* selection)
{
 VkNrFrameContract frame;
 frame.deviceGeneration=observed.request.deviceGeneration;
 frame.swapchainGeneration=observed.request.swapchainGeneration;
 frame.swapchain=observed.swapchain;frame.swapchainImageIndex=observed.request.imageIndex;
 frame.acquireGeneration=observed.request.acquireGeneration;
 frame.queue=observed.queue;frame.queueFamily=observed.queueFamily;frame.output=observed.request.extent;
 frame.guideGeneration=VulkanGuideContextGeneration(frame,selection);
 frame.representation.format=observed.request.format;frame.representation.colorSpace=observed.request.colorSpace;
 return frame;
}
}
