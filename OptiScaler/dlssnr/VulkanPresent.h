#pragma once

#include "VulkanNrSession.h"
#include "VulkanPresentGuides.h"
#include "CapturedGuideContract.h"
#include "VulkanPresentColor.h"
#include <shaders/format_transfer/PresentColor_Vk.h>
#include "DlssNr_PresentInputDecision.h"
#include <array>

namespace DlssNr
{

// Present reads post-upscale color, while public SR jitter/motion are in the
// original render-pixel basis. Keep image rectangles unchanged.
inline VkNrTemporalMetadata VkPresentTemporal(const VkNrTemporalMetadata& value,VkExtent2D output)
{return ScaleVkNrTemporal(value,{value.color.width,value.color.height},output);}

struct VkCapturedGuideInput
{
    CapturedGuideContract identity;
    NVSDK_NGX_Resource_VK depth{},motion{};
    bool depthInverted=false,reset=true;
    std::shared_ptr<void> lease;
    std::function<bool(VkNrUseId)> bind;
};
struct VkPresentImageRequest
{
    std::shared_ptr<const VkNrWaitBudget> waitBudget;
    VkImage image = VK_NULL_HANDLE;
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkExtent2D extent {};
    uint64_t generation = 0;
    bool captureOnly = false;
    VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
    uint64_t privateAllocationBudget = 1536ull*1024*1024;
    VkColorSpaceKHR colorSpace = VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkNrFrameContract frame;
    std::optional<VkExtent2D> observedRenderSize;
    std::optional<VkNrGuideLease> guides;
    std::shared_ptr<const VkCapturedGuideInput> capturedGuides;
    PresentInputDecision::Decision inputDecision;
    std::shared_ptr<const NrConfigSnapshot<Config>> settings;
};

struct VkPresentRecordResult
{
    VkRecordStatus status = VkRecordStatus::NoWork;
    std::string reason;
    bool modelRecorded = false;
    bool targetWriteRecorded = false;
    uint32_t requestedPasses=0,completedPasses=0;
    uint64_t outputVersion=0;
    bool chainDeliverable=false;
    VkNrUseId use;
    bool captureOnly=false;
    bool preparationOnly=false;
};

// Recording commands are deliberately separate from submission. The caller owns the command
// buffer and discards it when this function does not return Complete.
class IVkPresentCommands
{
  public:
    virtual ~IVkPresentCommands() = default;
    virtual bool Check(const VkPresentImageRequest&) = 0;
    virtual bool Begin() = 0;
    virtual void SourceToTransfer() = 0;
    virtual void Capture() = 0;
    virtual void SourceToPresent() = 0;
    virtual bool ConvertToModel() = 0;
    virtual bool Evaluate(bool reset) = 0;
    virtual VkRecordResult ModelResult() const {
        VkRecordResult result{VkRecordStatus::Complete,"",true,true};
        result.requestedPasses=result.completedPasses=1;result.outputVersion=1;result.chainDeliverable=true;return result;
    }
    virtual bool ConvertFromModel() = 0;
    virtual void TargetToTransfer() = 0;
    virtual void CopyBack() = 0;
    virtual void TargetToPresent() = 0;
    virtual bool End() = 0;
    virtual void Discard() = 0;
};

VkPresentRecordResult RecordVkPresentImage(const VkPresentImageRequest& request,
                                           IVkPresentCommands& commands, bool reset = false);

// One retained set of command and image resources for one swapchain image. The owner must
// prove GPU completion before destruction and GPU/presentation completion before reuse.
class VkPresentPrivateFrame final : public IVkPresentCommands
{
  public:
    VkPresentPrivateFrame() = default;
    ~VkPresentPrivateFrame();
    VkPresentPrivateFrame(const VkPresentPrivateFrame&) = delete;
    VkPresentPrivateFrame& operator=(const VkPresentPrivateFrame&) = delete;

    bool Initialize(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                    uint32_t queueFamily, const VkPresentImageRequest& image);
    VkCommandBuffer CommandBuffer() const { return commandBuffer_; }
    uint64_t AllocatedBytes() const { return allocatedBytes_; }
    bool Ready() const { return commandBuffer_ != VK_NULL_HANDLE; }
    void Release(bool deviceAlive);
    // The executor joins the actual one-time recording before releasing this lease.
    void ReleaseCapturedGuides() { request_.capturedGuides.reset(); }
    bool DiscardCapturedGuides();

    bool Check(const VkPresentImageRequest&) override;
    bool Begin() override;
    void SourceToTransfer() override;
    void Capture() override;
    void SourceToPresent() override;
    bool ConvertToModel() override;
    bool Evaluate(bool reset) override;
    VkRecordResult ModelResult() const override { return modelResult_; }
    bool ConvertFromModel() override;
    void TargetToTransfer() override;
    void CopyBack() override;
    void TargetToPresent() override;
    bool End() override;
    void Discard() override;

  private:
    struct Image
    {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
        NVSDK_NGX_Resource_VK ngx {};
    };
    enum : size_t { Captured, ModelInput, ModelOutput, OutputCopy, Depth, Motion, Encoded, ImageCount };
    bool Create(Image& image, VkFormat format, VkImageUsageFlags usage, bool writable);
    void Move(Image& image, VkImageLayout to, VkAccessFlags dstAccess, VkPipelineStageFlags dstStage);
    void MoveGame(VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                  VkAccessFlags dstAccess, VkPipelineStageFlags srcStage, VkPipelineStageFlags dstStage);
    VkImageBlit BlitRegion() const;

    VkInstance instance_ = VK_NULL_HANDLE;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkPresentImageRequest request_ {};
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    std::array<Image, ImageCount> images_ {};
    bool modelTouched_ = false;
    VkRecordResult modelResult_;
    std::unique_ptr<PresentColor_Vk> color_;
    std::optional<VkNrReservation> colorReservation_;
    uint64_t allocatedBytes_ = 0;
};

} // namespace DlssNr
