#pragma once

#include "VulkanPresentAdmission.h"
#include "VulkanNrFrameContract.h"
#include "VulkanNrRecording.h"
#include "VulkanNrRenderSize.h"
#include "PreparedVulkanDevice.h"

#include <mutex>
#include <atomic>
#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace DlssNr
{

// An exact acquisition's evidence survives registry replacement while leased by
// a previous Present wait. Only a completed observed submission or the actual
// acquisition fence signal can publish completion.
struct VkNrAcquireProof
{
    VkNrUseId use;
    VkDevice device=VK_NULL_HANDLE;
    uint64_t deviceGeneration=0,swapchainGeneration=0,acquireGeneration=0;
    VkSwapchainKHR swapchain=VK_NULL_HANDLE;
    uint32_t imageIndex=UINT32_MAX;
    uint64_t priorPresentSerial=0;
    VkQueue priorPresentQueue=VK_NULL_HANDLE;
    std::vector<uint64_t> predecessorGenerations;
    ~VkNrAcquireProof();
    bool Complete() const;
  private:
    friend class VulkanPresentRegistry;
    mutable std::atomic_bool fenceComplete_{false};
};

struct VkObservedPresent
{
    bool applicationFacing = false;
    VkPresentRequest request;
    VkPresentCapabilities capabilities;
    VkDevice device = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
    VkQueue queue = VK_NULL_HANDLE;
    uint32_t queueFamily = UINT32_MAX;
    VkSwapchainKHR swapchain = VK_NULL_HANDLE;
    VkImage image = VK_NULL_HANDLE;
    std::vector<uint64_t> predecessorGenerations;
    VkNrRenderSizeSelection renderSize;
    uint64_t priorPresentSerial=0;
};

// Observations made at actual device, queue, swapchain, acquire and Present calls.
// The registry never creates a frame number or treats pImageIndices as acquisition proof.
class VulkanPresentRegistry
{
  public:
    ~VulkanPresentRegistry();
    bool ImportDeviceAndQueues(const VulkanPresentRegistry&,VkDevice);
    void SwapchainImages(VkDevice,VkSwapchainKHR,std::vector<VkImage>);
    void SwapchainDestroyed(VkSwapchainKHR);
    void DeviceDestroyed(VkDevice);
    void DeviceCreated(VkDevice device, VkPhysicalDevice physical, bool modelExtensionsEnabled,
                       bool routePrepared, std::vector<uint32_t> createdFamilies,
                       bool modelExtensionsUnsupported = false, bool storageWriteWithoutFormatEnabled = false,
                       bool storageWriteUnsupported = false, bool storageWriteUnsafeChain = false,
                       uint64_t actualDeviceGeneration = 0);
    bool StorageWriteWithoutFormatEnabled(VkDevice device) const;
    VkPhysicalDevice PhysicalDevice(VkDevice device) const;
    bool PresentPrepared(VkDevice device) const;
    void PreparedTransportObserved(VkDevice,PreparedVulkan::Enabled);
    bool PreparedDevice(PreparedVulkan::DeviceInfo&) const;
    bool FramegenQueue(VkDevice,VkQueue&,std::uint32_t&)const;
    void InstanceObserved(VkInstance,std::uint32_t,std::vector<VkPhysicalDevice>);
    void InstanceDestroyed(VkInstance);
    std::uint32_t PhysicalInstanceApi(VkPhysicalDevice) const;
    void QueueObserved(VkDevice device, VkQueue queue, uint32_t family, VkQueueFlags flags);
    std::vector<uint32_t> QueueFamilies(VkDevice device) const;
    std::vector<uint32_t> CreatedQueueFamilies(VkDevice device) const;
    void SwapchainCreated(VkDevice device, VkSwapchainKHR swapchain, const VkSwapchainCreateInfoKHR& create,
                          VkImageUsageFlags supportedUsage, bool knownUsageChain,
                          std::vector<VkImage> images, std::vector<uint32_t> presentFamilies);
    void ImageAcquired(VkDevice device, VkSwapchainKHR swapchain, uint32_t imageIndex,
                       VkSemaphore semaphore=VK_NULL_HANDLE,VkFence fence=VK_NULL_HANDLE);
    std::vector<VkNrUseId> PrepareAcquireWait(VkQueue,std::span<const VkSemaphore>,uint64_t);
    void AcquireWaitSubmitted(VkQueue,std::span<const VkNrUseId>,VkResult);
    std::shared_ptr<const VkNrAcquireProof> AcquireProof(VkSwapchainKHR,uint32_t,uint64_t expectedAcquireGeneration) const;
    std::shared_ptr<const VkNrAcquireProof> LatestAcquireProof(VkSwapchainKHR,uint32_t) const;
    std::shared_ptr<const VkNrAcquireProof> AcquireFenceProof(VkDevice,VkFence) const;
    static void ObserveAcquireFence(const std::shared_ptr<const VkNrAcquireProof>&,VkResult);
    void InvalidateAcquireFence(VkDevice,VkFence);
    VkNrUseId AcquireUse(VkSwapchainKHR,uint32_t) const;
    bool AcquireConsumed(VkSwapchainKHR,uint32_t) const;
    // Completion must belong to the exact acquisition in the caller's snapshot.
    bool AcquireConsumed(VkSwapchainKHR,uint32_t,uint64_t expectedAcquireGeneration) const;
    void ObserveNativeRenderSize(VkDevice,VkExtent2D,VkNrRenderSize);
    void InvalidateNativeRenderSize(VkDevice);
    std::optional<VkNrFrameContract> NativeFrameContext(VkDevice,VkQueue,VkExtent2D output) const;
    // Dimensions only: SR may precede image acquisition. Never grants image/frame rights.
    std::optional<VkNrFrameContract> NativeResolutionContext(VkDevice,VkQueue,VkExtent2D output) const;
    // Exact identity from a public provider Backbuffer tag. This does not
    // authenticate an SR frame or grant queue/image access by itself.
    std::optional<VkNrFrameContract> TaggedImageContext(VkImage,VkFormat,VkExtent2D) const;
    // Observation only; prevents estimated depth crossing window/swapchain owners.
    bool OnlySwapchain(VkDevice,VkSwapchainKHR) const;
    bool IsSwapchainImage(VkDevice,VkImage) const;
    // Only for the current intercepted vkQueuePresentKHR call. Exclusive access
    // relies on that call's Vulkan ownership/layout contract, not a prior frame.
    // Consumers must submit on this exact queue behind the original Present waits.
    std::optional<VkObservedPresent> SnapshotForPresent(VkQueue queue, const VkPresentInfoKHR& info,
        bool enabled, uint32_t route, PresentInput::Policy policy, bool fgKnownActive);
    void PresentSucceeded(VkSwapchainKHR swapchain, uint32_t imageIndex,VkQueue queue=VK_NULL_HANDLE);
    uint64_t LastPresentSerial(VkSwapchainKHR,uint32_t) const;

  private:
    struct Device
    {
        VkPhysicalDevice physical = VK_NULL_HANDLE;
        uint64_t generation = 0;
        bool modelExtensionsEnabled = false;
        bool routePrepared = false;
        std::vector<uint32_t> createdFamilies;
        VkNrRenderSizeObservations renderSizes;
        bool modelExtensionsUnsupported = false;
        bool storageWriteWithoutFormatEnabled = false;
        bool storageWriteUnsupported = false;
        bool storageWriteUnsafeChain = false;
        PreparedVulkan::Enabled preparedTransport;
    };
    struct Queue
    {
        VkDevice device = VK_NULL_HANDLE;
        uint64_t deviceGeneration = 0;
        uint32_t family = UINT32_MAX;
        VkQueueFlags flags = 0;
    };
    struct Swapchain
    {
        VkDevice device = VK_NULL_HANDLE;
        uint64_t deviceGeneration = 0;
        uint64_t generation = 0;
        VkFormat format = VK_FORMAT_UNDEFINED;
        std::vector<uint64_t> predecessorGenerations;
        VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
        VkExtent2D extent {};
        uint32_t arrayLayers = 0;
        VkImageUsageFlags usage = 0;
        VkSwapchainCreateFlagsKHR flags = 0;
        VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        std::vector<uint32_t> sharingFamilies;
        VkImageUsageFlags supportedUsage = 0;
        bool knownUsageChain = false;
        std::vector<VkImage> images;
        std::vector<bool> acquired;
        std::vector<uint64_t> acquisition;
        std::vector<VkSemaphore> acquireSemaphores;
        std::vector<std::shared_ptr<VkNrAcquireProof>> acquireProofs;
        std::vector<uint64_t> presentSerials;
        std::vector<VkQueue> presentQueues;
        std::vector<uint32_t> presentFamilies;
    };

    mutable std::mutex mutex_;
    struct Instance {std::uint32_t api=0;std::vector<VkPhysicalDevice> physical;};
    std::unordered_map<VkInstance,Instance> instances_;
    uint64_t nextDeviceGeneration_ = 0;
    uint64_t nextSwapchainGeneration_ = 0;
    uint64_t nextAcquire_ = 0;
    uint64_t nextPresentSerial_ = 0;
    std::unordered_map<uintptr_t, Device> devices_;
    std::unordered_map<uintptr_t, Queue> queues_;
    std::unordered_map<uintptr_t, Swapchain> swapchains_;
    std::unordered_map<VkDevice,std::unordered_map<VkFence,std::weak_ptr<VkNrAcquireProof>>> acquireFences_;
};

VulkanPresentRegistry& GetVulkanPresentRegistry();
VulkanPresentRegistry& GetVulkanLoaderPresentRegistry();
VulkanPresentRegistry& GetVulkanApplicationPresentRegistry();

} // namespace DlssNr
