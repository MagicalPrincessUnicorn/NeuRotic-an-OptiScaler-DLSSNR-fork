#pragma once
#include "VulkanNrCapture.h"
namespace DlssNr {
void RequestVulkanNrCapture(bool stages=false);
bool WantsVulkanNrCapture();
void CancelVulkanNrCapture();
void VulkanNrCaptureWaiting(const std::string& reason);
bool VulkanNrCaptureBusy();
std::string VulkanNrCaptureStatus();
void RecordVulkanNrCapture(const VkNrCaptureRequest&,VkCommandBuffer,VkPhysicalDevice,VkDevice);
// Bounded sparse samples of private original/composed/proxy/model images; logs only.
void RecordVulkanNrPixelProbe(const VkNrCaptureRequest&,VkCommandBuffer,VkPhysicalDevice,VkDevice);
void PollVulkanNrCaptures();
void RevokeVulkanNrCaptures(uint64_t epoch);
void AbandonVulkanNrCaptures();
uint64_t VulkanNrCaptureBytes();
}
