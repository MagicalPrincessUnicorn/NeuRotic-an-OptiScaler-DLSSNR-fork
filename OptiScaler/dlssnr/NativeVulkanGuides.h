#pragma once
#include <vulkan/vulkan.h>
#include <cstdint>
#include <string>
#include <functional>
#include <memory>
#include "NativeGuideCapture.h"

namespace DlssNr { struct VkObservedPresent; struct VkCapturedGuideInput; }
namespace DlssNr::NativeVulkanGuides {
struct Snapshot {
 bool selected=false; std::uint64_t captured=0,delivered=0; std::string reason;
 std::uint64_t scopesSeen=0,depthCopies=0,estimatedAssociations=0;
 std::string captureReason,processorReason;
 std::string transportTimings;
 double readbackMs=0,unpackMs=0,processorMs=0;
 bool colorCached=false,depthCached=false;
};
bool Enabled() noexcept;
void SetCaptureActive(bool) noexcept;
// Requires an external gate against new capture/NR/FG work; read-only proof.
bool CanYieldOutput(std::string& reason);
bool CanSwitchInput(std::string& reason);
Snapshot Status();
void Unavailable(const char* reason);
// A completed consumer reports its actual model raster for this acquisition.
void ModelDimensions(const VkObservedPresent&,unsigned width,unsigned height);
void DeviceCreated(VkDevice,VkPhysicalDevice,PFN_vkGetDeviceProcAddr,PFN_vkGetPhysicalDeviceProperties2 = nullptr);
// Called before real destruction. Unknown GPU work is abandoned to device teardown.
void DeviceDestroyed(VkDevice);
void ViewCreated(VkDevice,const VkImageViewCreateInfo&,VkImageView);
void ViewDestroyed(VkDevice,VkImageView);
void RenderPassCreated(VkDevice,const VkRenderPassCreateInfo&,VkRenderPass);
void RenderPassCreated2(VkDevice,const VkRenderPassCreateInfo2&,VkRenderPass);
void RenderPassDestroyed(VkDevice,VkRenderPass);
void FramebufferCreated(VkDevice,const VkFramebufferCreateInfo&,VkFramebuffer);
void FramebufferDestroyed(VkDevice,VkFramebuffer);
VkImageCreateInfo PrepareImage(VkDevice,const VkImageCreateInfo&);
PFN_vkVoidFunction HookAddress(const char*,PFN_vkVoidFunction);
void BeginLegacy(VkCommandBuffer,const VkRenderPassBeginInfo*);
void BeginDynamic(VkCommandBuffer,const VkRenderingInfo*);
void Draw(VkCommandBuffer);
void ClearAttachments(VkCommandBuffer,uint32_t,const VkClearAttachment*,uint32_t,const VkClearRect*);
void Barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*);
void Barrier(VkCommandBuffer,const VkDependencyInfo*);
void InvalidateDepthHistory(VkCommandBuffer);
void ClearDepthImage(VkCommandBuffer,VkImage,VkImageLayout,const VkClearDepthStencilValue*,uint32_t,const VkImageSubresourceRange*);
void End(VkCommandBuffer);
void ColorCopy(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageCopy*);
void ColorCopy(VkCommandBuffer,const VkCopyImageInfo2*);
void ColorBlit(VkCommandBuffer,VkImage,VkImageLayout,VkImage,VkImageLayout,uint32_t,const VkImageBlit*,VkFilter);
void ColorBlit(VkCommandBuffer,const VkBlitImageInfo2*);
// Consumes waits only after a successful owned submit; return !=VK_SUCCESS forbids Present.
using CapturedConsumer=std::function<NativeGuides::Outcome(std::shared_ptr<const VkCapturedGuideInput>,VkPresentInfoKHR&,std::string&)>;
VkResult BeforePresent(const VkObservedPresent&,VkPresentInfoKHR&,const CapturedConsumer& = {},bool* handled = nullptr);
void AfterPresent(VkDevice,VkResult);
}
