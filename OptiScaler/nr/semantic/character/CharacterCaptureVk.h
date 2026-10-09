#pragma once
#include "CharacterPipeline.h"
#include <vulkan/vulkan.h>
#include <array>

namespace Neurotic::Semantic::Character {
bool ValidCharacterVkPixels(const CpuFrame&) noexcept;
bool ConvertCharacterVkPixels(CpuFrame&);
inline VkImageUsageFlags CharacterVkSwapchainUsage(VkImageUsageFlags original,VkImageUsageFlags supported,
    bool knownChain,VkSwapchainCreateFlagsKHR flags) noexcept {
    return knownChain&&(supported&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)&&!(flags&VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR)
        ? original|VK_IMAGE_USAGE_TRANSFER_SRC_BIT:original;
}
// Access is serialized by the existing Vulkan overlay operation lock. Fences
// belong to that overlay; Poll must run before any of its fences are reset.
class CharacterCaptureVk {
public:
    CharacterCaptureVk()=default;
    CharacterCaptureVk(const CharacterCaptureVk&)=delete;
    CharacterCaptureVk& operator=(const CharacterCaptureVk&)=delete;
    static std::optional<unsigned> ColorPolicy(VkFormat,VkColorSpaceKHR) noexcept;
    bool Initialize(VkPhysicalDevice,VkDevice,VkFormat,VkColorSpaceKHR,VkExtent2D);
    bool Ready() const noexcept {return device_ != VK_NULL_HANDLE;}
    int Record(VkCommandBuffer,VkImage,CpuFrame,VkImageLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    void Cancel(int) noexcept;
    void Submitted(int,VkFence) noexcept;
    std::optional<CpuFrame> Poll();
    // Only after the overlay's successful device-idle retirement proof.
    void ReleaseAfterIdle() noexcept;
    // Only after the actual vkDestroyDevice; dispatches no Vulkan calls.
    void AbandonDestroyedDevice() noexcept;
private:
    struct Slot {
        VkImage image{}; VkDeviceMemory imageMemory{};
        VkBuffer buffer{}; VkDeviceMemory bufferMemory{}; void* mapped=nullptr;
        VkFence fence{}; CaptureSlotState state; CpuFrame frame;
    };
    std::array<Slot,3> slots_;
    VkDevice device_{}; VkFormat format_{}; VkExtent2D source_{},target_{};
    unsigned policy_=0,bytesPerPixel_=0;
};
}
