#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#ifndef VK_USE_PLATFORM_WIN32_KHR
#define VK_USE_PLATFORM_WIN32_KHR
#endif
#include <Windows.h>
#include <vulkan/vulkan.h>
#include <vulkan/vulkan_win32.h>
#include <d3d12.h>
#include <dxgi1_4.h>
#include <memory>
#include <optional>
#include <span>
#include <string>

namespace DlssNr::PreparedTransport
{
struct Status {
    bool available=false;std::string reason;
    explicit operator bool()const{return available;}
};
// These are facts observed from the successful existing device creation.
// Advertised extensions or supported feature bits do not establish enablement.
struct Enabled { bool externalMemoryWin32=false,externalSemaphoreWin32=false,timeline=false; };
struct Dispatch {
    PFN_vkGetPhysicalDeviceProperties2 getProperties2=nullptr;
    PFN_vkGetPhysicalDeviceImageFormatProperties2 getImageFormatProperties2=nullptr;
    PFN_vkGetPhysicalDeviceExternalSemaphoreProperties getExternalSemaphoreProperties=nullptr;
    PFN_vkGetPhysicalDeviceMemoryProperties getMemoryProperties=nullptr;
    PFN_vkGetMemoryWin32HandlePropertiesKHR getHandleProperties=nullptr;
    PFN_vkCreateImage createImage=nullptr;PFN_vkDestroyImage destroyImage=nullptr;
    PFN_vkGetImageMemoryRequirements getImageMemoryRequirements=nullptr;
    PFN_vkAllocateMemory allocateMemory=nullptr;PFN_vkFreeMemory freeMemory=nullptr;
    PFN_vkBindImageMemory bindImageMemory=nullptr;
    PFN_vkCreateSemaphore createSemaphore=nullptr;PFN_vkDestroySemaphore destroySemaphore=nullptr;
    PFN_vkImportSemaphoreWin32HandleKHR importSemaphore=nullptr;
    PFN_vkCmdPipelineBarrier barrier=nullptr;PFN_vkCmdCopyImage copyImage=nullptr;
    PFN_vkGetPhysicalDeviceExternalBufferProperties getExternalBufferProperties=nullptr;
    PFN_vkCreateBuffer createBuffer=nullptr;PFN_vkDestroyBuffer destroyBuffer=nullptr;
    PFN_vkGetBufferMemoryRequirements getBufferMemoryRequirements=nullptr;PFN_vkBindBufferMemory bindBufferMemory=nullptr;
};
struct ImageSpec { std::uint32_t width=0,height=0;DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;bool storage=false;
    // Nonzero selects a raw shared buffer, independently of image format support.
    std::uint64_t rawBytes=0;
};
VkFormat Format(DXGI_FORMAT);
bool SameLuid(const LUID&,const LUID&) noexcept;
std::optional<std::uint32_t> ChooseMemoryType(const VkPhysicalDeviceMemoryProperties&,std::uint32_t imageBits,std::uint32_t handleBits);
Status QuerySupport(VkPhysicalDevice,const Dispatch&,const Enabled&,const ImageSpec&);
// Returns an AddRef'd matching DXGI adapter. The caller must Release it.
Status MatchAdapter(VkPhysicalDevice,const Dispatch&,IDXGIFactory4*,IDXGIAdapter1**);
struct ImageView { VkImage image=VK_NULL_HANDLE;ID3D12Resource* resource=nullptr;ImageSpec spec{};std::uint64_t allocationBytes=0; };
// Fresh evidence from the canonical owners, after recording/submission stops.
// No timeout, scalar frame age, host return or IPC receipt supplies these facts.
struct DrainEvidence { bool vulkanComplete=false,d3d12Complete=false,providerReleased=false,recordingsReleased=false; };
inline bool CanRetire(const DrainEvidence& d) { return d.vulkanComplete&&d.d3d12Complete&&d.providerReleased&&d.recordingsReleased; }
class Session
{
    struct Impl;std::unique_ptr<Impl> impl_;
public:
    Session();~Session();Session(const Session&)=delete;Session& operator=(const Session&)=delete;
    // Caller supplies already-enabled device, exact queue family and D3D12 device
    // on the matching adapter. No device hooks, feature writes or global registry.
    Status Initialize(VkPhysicalDevice,VkDevice,VkQueue,std::uint32_t queueFamily,ID3D12Device*,
                      const Dispatch&,const Enabled&,std::span<const ImageSpec>);
    VkDevice Device()const;VkQueue Queue()const;std::uint32_t QueueFamily()const;
    std::size_t Count()const;ImageView Image(std::size_t)const;
    VkBuffer Buffer(std::size_t)const;
    VkSemaphore InputSemaphore()const;VkSemaphore OutputSemaphore()const;
    ID3D12Fence* InputFence()const;ID3D12Fence* OutputFence()const;
    // Call BEFORE a command/fence reference crosses into any external owner.
    // Recording helpers arm this automatically. Queue synchronization is recorded
    // by the existing queue owner, not by these helpers.
    bool MarkReferenced();
    // The existing recording owner must verify command device/family and serialize
    // these calls. Initial acquisition discards undefined contents: perform it
    // before any D3D12 producer writes, then Release for external processing.
    // Subsequent acquisitions require the queue owner's real external fence wait.
    Status Acquire(VkCommandBuffer,std::size_t,bool initial=false);
    Status Release(VkCommandBuffer,std::size_t);
    Status CopyFrom(VkCommandBuffer,std::size_t,VkImage,VkImageLayout,std::uint32_t width,std::uint32_t height);
    Status CopyTo(VkCommandBuffer,std::size_t,VkImage,VkImageLayout,std::uint32_t width,std::uint32_t height);
    Status Retire(const DrainEvidence&);
    bool PendingOwnership()const;
};
}
