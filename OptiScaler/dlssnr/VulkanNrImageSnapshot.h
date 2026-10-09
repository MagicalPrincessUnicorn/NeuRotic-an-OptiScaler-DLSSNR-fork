#pragma once
#include "VulkanNrSession.h"
#include <array>
namespace DlssNr {
struct VkNrSnapshotImages {
 VkDevice device=VK_NULL_HANDLE;
 std::array<VkImage,3> images{};std::array<VkImageView,3> views{};std::array<VkDeviceMemory,3> memory{};
 std::array<NVSDK_NGX_Resource_VK,3> resources{};uint64_t bytes=0;
};
inline bool VkNrSnapshotMetadataQualified(const VkFrameRequest& frame){return frame.contract.temporal.has_value()||(frame.ownedPresentInputs&&frame.route==VkNrRoute::Present&&frame.contract.route==VkNrRoute::Present);}
// Checks every source right/layout and allocates the entire copy before recording any commands.
bool CaptureVkNrImages(const VkFrameRequest&,VkNrSnapshotImages&,uint64_t budget,std::string& reason);
void ReleaseVkNrImages(VkNrSnapshotImages&,bool deviceAlive);
}
