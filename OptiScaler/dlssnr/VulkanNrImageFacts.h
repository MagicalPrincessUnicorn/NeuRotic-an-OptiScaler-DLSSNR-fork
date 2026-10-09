#pragma once
#include <vulkan/vulkan.h>
#include <nvsdk_ngx_vk.h>
#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>
namespace DlssNr
{
inline bool TrackVkNrCommandState(bool bridgeActive,bool nativeDeviceObserved)
{ return bridgeActive || nativeDeviceObserved; }
struct VkNrObservedImage
{
    VkDevice device=VK_NULL_HANDLE;uint64_t generation=0;
    VkFormat format=VK_FORMAT_UNDEFINED;VkExtent3D extent{};
    VkImageUsageFlags usage=0;VkImageCreateFlags flags=0;
    VkImageType type=VK_IMAGE_TYPE_2D;VkImageTiling tiling=VK_IMAGE_TILING_OPTIMAL;
    VkSampleCountFlagBits samples=VK_SAMPLE_COUNT_1_BIT;
    uint32_t mips=0,layers=0;VkSharingMode sharing=VK_SHARING_MODE_EXCLUSIVE;
    std::vector<uint32_t> families;bool chainKnown=false;
    std::vector<VkFormat> viewFormats;
};
struct VkNrObservedView
{
    VkDevice device=VK_NULL_HANDLE;VkImage image=VK_NULL_HANDLE;
    uint64_t imageGeneration=0,generation=0;VkFormat format=VK_FORMAT_UNDEFINED;
    VkImageViewType type=VK_IMAGE_VIEW_TYPE_2D;VkImageSubresourceRange range{};
    VkComponentMapping components{};bool chainKnown=false;
    std::optional<VkImageUsageFlags> usage;
};
// Returned view ranges use explicit counts resolved against the observed image.
// The stored creation facts remain raw for diagnostic descriptions.
struct VkNrImageRights { VkNrObservedImage image;VkNrObservedView view;VkImageAspectFlags copyAspect=0,barrierAspect=0; };
class VkNrImageFacts
{
  public:
    void Created(VkDevice,const VkImageCreateInfo&,VkImage,VkResult);
    void ViewCreated(VkDevice,const VkImageViewCreateInfo&,VkImageView,VkResult);
    void Destroyed(VkDevice,VkImage);
    void ViewDestroyed(VkDevice,VkImageView);
    void DeviceDestroyed(VkDevice);
    std::optional<VkNrObservedImage> Image(VkDevice,VkImage) const;
    // Bounded, handle-free creation facts for a rejected input; never grants access.
    std::string Describe(VkDevice,const NVSDK_NGX_Resource_VK&) const;
    std::optional<VkNrImageRights> Rights(VkDevice,const NVSDK_NGX_Resource_VK&,VkImageUsageFlags required,bool depth,
                                        const char** unavailable=nullptr) const;
    // Streamline's omitted SubresourceRange becomes a generic COLOR/all-remaining
    // NGX wrapper, even for depth. At that input boundary only, validate the
    // depth interpretation against the complete observed image/view facts.
    std::optional<VkNrImageRights> NgxDepthRights(VkDevice,const NVSDK_NGX_Resource_VK&,bool authenticatedInput,
                                                const char** unavailable=nullptr) const;
  private:
    // Non-dispatchable handles are only unique within their parent device.
    template<class Handle> struct ObjectKey
    {
        VkDevice device;Handle handle;
        bool operator==(const ObjectKey&) const = default;
    };
    template<class Handle> struct ObjectHash
    {
        size_t operator()(const ObjectKey<Handle>& key) const noexcept
        {
            const auto device=std::hash<VkDevice>{}(key.device);
            const auto handle=std::hash<Handle>{}(key.handle);
            return device^(handle+0x9e3779b9+(device<<6)+(device>>2));
        }
    };
    using ImageKey=ObjectKey<VkImage>;
    using ViewKey=ObjectKey<VkImageView>;
    // Caller holds mutex_. Only visit views belonging to the affected image.
    void ForgetImageViews(ImageKey);
    void ForgetView(ViewKey);
    mutable std::mutex mutex_;uint64_t next_=1;
    std::unordered_map<ImageKey,VkNrObservedImage,ObjectHash<VkImage>> images_;
    std::unordered_map<ViewKey,VkNrObservedView,ObjectHash<VkImageView>> views_;
    std::unordered_map<ImageKey,std::unordered_set<ViewKey,ObjectHash<VkImageView>>,ObjectHash<VkImage>> imageViews_;
};
VkNrImageFacts& VulkanNrImageFacts();
}
