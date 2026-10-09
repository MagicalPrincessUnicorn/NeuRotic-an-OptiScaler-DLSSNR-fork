#include "VulkanPresent.h"
#include "DlssNrFeature_Vk.h"
#include "VulkanPresentGuidesVk.h"
#include "VulkanNrCompletion.h"

namespace DlssNr
{

VkPresentPrivateFrame::~VkPresentPrivateFrame() { Release(true); }

bool VkPresentPrivateFrame::Create(Image& image, VkFormat format, VkImageUsageFlags usage, bool writable)
{
    VkImageCreateInfo info { VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO };
    info.imageType = VK_IMAGE_TYPE_2D;
    info.format = format;
    info.extent = { request_.extent.width, request_.extent.height, 1 };
    info.mipLevels = 1;
    info.arrayLayers = 1;
    info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage;
    info.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateImage(device_, &info, nullptr, &image.image) != VK_SUCCESS) return false;

    VkMemoryRequirements requirements {};
    vkGetImageMemoryRequirements(device_, image.image, &requirements);
    VkPhysicalDeviceMemoryProperties memory {};
    vkGetPhysicalDeviceMemoryProperties(physical_, &memory);
    uint32_t type = UINT32_MAX;
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i)
        if ((requirements.memoryTypeBits & (1u << i)) &&
            (memory.memoryTypes[i].propertyFlags & VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT))
        {
            type = i;
            break;
        }
    if (type == UINT32_MAX) return false;
    VkMemoryAllocateInfo allocation { VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO };
    allocation.allocationSize = requirements.size;
    allocation.memoryTypeIndex = type;
    if (vkAllocateMemory(device_, &allocation, nullptr, &image.memory) != VK_SUCCESS ||
        vkBindImageMemory(device_, image.image, image.memory, 0) != VK_SUCCESS)
        return false;
    allocatedBytes_ += requirements.size;

    // Capture/output-copy images are transfer-only. Vulkan image views require a
    // sampled, storage, attachment, or input-attachment use and these have none.
    if (!VkPresentNeedsView(usage)) return true;

    VkImageViewCreateInfo view { VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO };
    view.image = image.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = format;
    view.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    if (vkCreateImageView(device_, &view, nullptr, &image.view) != VK_SUCCESS) return false;

    image.ngx.Type = NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    image.ngx.Resource.ImageViewInfo.Image = image.image;
    image.ngx.Resource.ImageViewInfo.ImageView = image.view;
    image.ngx.Resource.ImageViewInfo.SubresourceRange = view.subresourceRange;
    image.ngx.Resource.ImageViewInfo.Format = format;
    image.ngx.Resource.ImageViewInfo.Width = request_.extent.width;
    image.ngx.Resource.ImageViewInfo.Height = request_.extent.height;
    image.ngx.ReadWrite = writable;
    return true;
}

bool VkPresentPrivateFrame::Initialize(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                                       uint32_t queueFamily, const VkPresentImageRequest& image)
{
    if (Ready() || device == VK_NULL_HANDLE || physical == VK_NULL_HANDLE || instance == VK_NULL_HANDLE ||
        image.image == VK_NULL_HANDLE || image.generation == 0 || image.extent.width < 8 ||
        image.extent.height < 8 || image.extent.width > 8192 || image.extent.height > 8192 ||
        queueFamily == UINT32_MAX)
        return false;
    if (!SelectVkPresentColorRecipe(image.format,image.colorSpace).supported) return false;

    const auto hasFeatures = [&](VkFormat format, VkFormatFeatureFlags required) {
        VkFormatProperties props {};
        vkGetPhysicalDeviceFormatProperties(physical, format, &props);
        return (props.optimalTilingFeatures & required) == required;
    };
    if (!hasFeatures(image.format, VK_FORMAT_FEATURE_BLIT_SRC_BIT | VK_FORMAT_FEATURE_BLIT_DST_BIT) ||
        !hasFeatures(VK_FORMAT_R16G16B16A16_SFLOAT, VK_FORMAT_FEATURE_BLIT_SRC_BIT |
            VK_FORMAT_FEATURE_BLIT_DST_BIT | VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT |
            VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        !hasFeatures(VK_FORMAT_R32_SFLOAT, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT) ||
        !hasFeatures(VK_FORMAT_R16G16_SFLOAT, VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))
        return false;

    instance_ = instance;
    physical_ = physical;
    device_ = device;
    request_ = image;
    request_.commandBuffer = VK_NULL_HANDLE;
    constexpr auto transfer = VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    const auto recipe=SelectVkPresentColorRecipe(image.format,image.colorSpace);
    const bool pq=recipe.revision==2;
    if(pq&&!hasFeatures(image.format,VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT))return false;
    const bool made =
        Create(images_[Captured], image.format, transfer|(pq?VK_IMAGE_USAGE_SAMPLED_BIT:0), false) &&
        Create(images_[ModelInput], VK_FORMAT_R16G16B16A16_SFLOAT,
               transfer | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, false) &&
        Create(images_[ModelOutput], VK_FORMAT_R16G16B16A16_SFLOAT,
               transfer | VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_STORAGE_BIT, true) &&
        Create(images_[OutputCopy], image.format, transfer, false) &&
        Create(images_[Depth], VK_FORMAT_R32_SFLOAT, transfer | VK_IMAGE_USAGE_SAMPLED_BIT, false) &&
        Create(images_[Motion], VK_FORMAT_R16G16_SFLOAT, transfer | VK_IMAGE_USAGE_SAMPLED_BIT, false);
    if (!made || (pq&&!Create(images_[Encoded],VK_FORMAT_R16G16B16A16_SFLOAT,
        transfer|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,true))) { Release(true); return false; }
    if(pq){color_=std::make_unique<PresentColor_Vk>(device_,request_.extent);if(!color_->IsInit()){Release(true);return false;}}


    VkCommandPoolCreateInfo pool { VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO };
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = queueFamily;
    if (vkCreateCommandPool(device_, &pool, nullptr, &commandPool_) != VK_SUCCESS)
    { Release(true); return false; }
    VkCommandBufferAllocateInfo allocation { VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO };
    allocation.commandPool = commandPool_;
    allocation.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocation.commandBufferCount = 1;
    if (vkAllocateCommandBuffers(device_, &allocation, &commandBuffer_) != VK_SUCCESS)
    { Release(true); return false; }
    request_.commandBuffer = commandBuffer_;
    return true;
}

void VkPresentPrivateFrame::Release(bool deviceAlive)
{
    if (deviceAlive && device_ != VK_NULL_HANDLE)
    {
        if (commandPool_ != VK_NULL_HANDLE) vkDestroyCommandPool(device_, commandPool_, nullptr);
        for (auto& image : images_)
        {
            if (image.view != VK_NULL_HANDLE) vkDestroyImageView(device_, image.view, nullptr);
            if (image.image != VK_NULL_HANDLE) vkDestroyImage(device_, image.image, nullptr);
            if (image.memory != VK_NULL_HANDLE) vkFreeMemory(device_, image.memory, nullptr);
        }
    }
    if(!deviceAlive&&color_)color_->AbandonDevice();
    color_.reset();
    colorReservation_.reset();
    images_ = {};
    request_ = {};
    commandPool_ = VK_NULL_HANDLE;
    commandBuffer_ = VK_NULL_HANDLE;
    instance_ = VK_NULL_HANDLE;
    physical_ = VK_NULL_HANDLE;
    device_ = VK_NULL_HANDLE;
    allocatedBytes_ = 0;
}

#include "VulkanPresentCheck.inl"

bool VkPresentPrivateFrame::Begin()
{
    if (vkResetCommandPool(device_, commandPool_, 0) != VK_SUCCESS) return false;
    for (auto& image : images_) image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    modelTouched_ = false;
    modelResult_ = {};
    VkCommandBufferBeginInfo begin { VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO };
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    colorReservation_.reset();
    if(vkBeginCommandBuffer(commandBuffer_, &begin)!=VK_SUCCESS)return false;
    if(color_) {
        const auto use=VulkanNrRecordings().Reserve(commandBuffer_,VkNrCompletionDeviceGeneration(device_));
        if(!use||!ReserveVkNrCompletion(device_,*use))return false;
        colorReservation_=color_->Reserve(*use);
        if(!colorReservation_)return false;
    }
    return true;
}

void VkPresentPrivateFrame::Move(Image& image, VkImageLayout to, VkAccessFlags dstAccess,
                                 VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = image.layout;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? 0 :
        VK_ACCESS_TRANSFER_WRITE_BIT | VK_ACCESS_TRANSFER_READ_BIT | VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(commandBuffer_, image.layout == VK_IMAGE_LAYOUT_UNDEFINED ? VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT :
        VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
    image.layout = to;
}

void VkPresentPrivateFrame::MoveGame(VkImageLayout from, VkImageLayout to, VkAccessFlags srcAccess,
                                    VkAccessFlags dstAccess, VkPipelineStageFlags srcStage,
                                    VkPipelineStageFlags dstStage)
{
    VkImageMemoryBarrier barrier { VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER };
    barrier.oldLayout = from;
    barrier.newLayout = to;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = request_.image;
    barrier.subresourceRange = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    barrier.srcAccessMask = srcAccess;
    barrier.dstAccessMask = dstAccess;
    vkCmdPipelineBarrier(commandBuffer_, srcStage, dstStage, 0, 0, nullptr, 0, nullptr, 1, &barrier);
}

VkImageBlit VkPresentPrivateFrame::BlitRegion() const
{
    VkImageBlit region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.srcOffsets[1] = { static_cast<int32_t>(request_.extent.width),
                             static_cast<int32_t>(request_.extent.height), 1 };
    region.dstOffsets[1] = region.srcOffsets[1];
    return region;
}

void VkPresentPrivateFrame::SourceToTransfer()
{
    MoveGame(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, 0,
             VK_ACCESS_TRANSFER_READ_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void VkPresentPrivateFrame::Capture()
{
    Move(images_[Captured], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    VkImageCopy region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.extent = { request_.extent.width, request_.extent.height, 1 };
    vkCmdCopyImage(commandBuffer_, request_.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   images_[Captured].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VkPresentPrivateFrame::SourceToPresent()
{
    MoveGame(VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_ACCESS_TRANSFER_READ_BIT,
             0, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

bool VkPresentPrivateFrame::ConvertToModel()
{
    const auto recipe=SelectVkPresentColorRecipe(request_.format,request_.colorSpace);
    if(recipe.revision==2) {
        if(!color_||!colorReservation_)return false;
        Move(images_[Captured],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        Move(images_[ModelInput],VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if(!color_->Decode(commandBuffer_,*colorReservation_,images_[Captured].view,images_[ModelInput].view,
            {request_.format,request_.colorSpace,recipe.revision}))return false;
        Move(images_[ModelInput],VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    } else {
    Move(images_[Captured], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    Move(images_[ModelInput], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    const auto region = BlitRegion();
    // Vulkan blit applies BGRA component mapping and sRGB decode/encode between formats.
    vkCmdBlitImage(commandBuffer_, images_[Captured].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   images_[ModelInput].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, SelectVkPresentColorRecipe(request_.format,request_.colorSpace).filter);
    Move(images_[ModelInput], VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    }
    Move(images_[ModelOutput], VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_WRITE_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    Move(images_[Depth], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    Move(images_[Motion], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    const VkImageSubresourceRange range { VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1 };
    const VkClearColorValue farDepth { { 1.0f, 0.0f, 0.0f, 0.0f } };
    const VkClearColorValue stillMotion { { 0.0f, 0.0f, 0.0f, 0.0f } };
    vkCmdClearColorImage(commandBuffer_, images_[Depth].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &farDepth, 1, &range);
    vkCmdClearColorImage(commandBuffer_, images_[Motion].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         &stillMotion, 1, &range);
    Move(images_[Depth], VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    Move(images_[Motion], VK_IMAGE_LAYOUT_GENERAL, VK_ACCESS_SHADER_READ_BIT,
         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
    return true;
}

bool VkPresentPrivateFrame::Evaluate(bool reset)
{
    VkFrameRequest frame {};
    frame.waitBudget=request_.waitBudget;
    frame.observedRenderSize=request_.observedRenderSize;
    frame.route = VkNrRoute::Present;
    frame.contract.route = VkNrRoute::Present;
    frame.contract.swapchainGeneration = request_.generation;
    frame.contract.representation.format = request_.format;
    frame.contract.representation.colorSpace = request_.colorSpace;
    const auto recipe=SelectVkPresentColorRecipe(request_.format,request_.colorSpace);
    frame.contract.representation.recipeRevision = recipe.revision;
    frame.gameHdr = recipe.hdr;
    frame.contract.queue = request_.frame.queue;
    frame.contract.acquireGeneration = request_.frame.acquireGeneration;
    frame.contract.swapchain = request_.frame.swapchain;
    frame.contract.swapchainImageIndex = request_.frame.swapchainImageIndex;
    frame.contract.guideGeneration = request_.frame.guideGeneration;
    if (request_.frame.temporal) {
        frame.resolutionMetadata = request_.frame.temporal;
        frame.contract.guideGeneration = request_.frame.temporal->featureGeneration;
    }
    frame.privateAllocationBudget = request_.privateAllocationBudget;
    frame.settings = request_.settings;
    frame.presentInput = request_.inputDecision;
    frame.ownedPresentInputs = true;frame.captureOnly=request_.captureOnly;
    frame.effectiveResolutionMode = request_.inputDecision.resolution.mode;
    frame.effectiveResolutionScale = request_.inputDecision.resolution.scale;
    frame.commandBuffer = commandBuffer_;
    frame.instance = instance_;
    frame.physicalDevice = physical_;
    frame.device = device_;
    frame.color = &images_[ModelInput].ngx;
    frame.targetColor = &images_[ModelOutput].ngx;
    frame.depth = &images_[Depth].ngx;
    frame.motion = &images_[Motion].ngx;
    if(request_.capturedGuides) {
        const auto& capture=*request_.capturedGuides;
        frame.capturedGuideWrappers=std::make_shared<std::array<NVSDK_NGX_Resource_VK,2>>(std::array{capture.depth,capture.motion});
        frame.depth=&(*frame.capturedGuideWrappers)[0];frame.motion=&(*frame.capturedGuideWrappers)[1];
        VkNrTemporalMetadata temporal;
        temporal.color=temporal.depth=temporal.motion={0,0,request_.extent.width,request_.extent.height};
        temporal.featureGeneration=capture.identity.swapchainGeneration;
        temporal.depthInverted=capture.depthInverted;temporal.reset=capture.reset;
        // Estimated current-to-previous pixel motion, with zero-jitter policy.
        // No engine/provider frame token or camera observation is invented.
        frame.contract.temporal=temporal;frame.contract.guideGeneration=temporal.featureGeneration;
        frame.depthInverted=capture.depthInverted;frame.gameReset=capture.reset;
        frame.bindGuides=capture.bind;
    } else if (request_.inputDecision.input == PresentInputDecision::InputClass::Guided) {
        if (!request_.guides || !request_.guides->images) return false;
        auto lease = *request_.guides;lease.consumerCommand=commandBuffer_;
        frame.depth = &lease.images->depth; frame.motion = &lease.images->motion;
        frame.contract.temporal = VkPresentTemporal(*lease.contract.temporal,request_.extent);
        frame.contract.temporal->depth.x = frame.contract.temporal->depth.y = 0;
        frame.contract.temporal->motion.x = frame.contract.temporal->motion.y = 0;
        frame.contract.guideGeneration = lease.contract.temporal->featureGeneration;
        frame.depthInverted = lease.contract.temporal->depthInverted;
        frame.gameReset = lease.contract.temporal->reset;
        frame.bindGuides = [lease,queue=request_.frame.queue](VkNrUseId use) {return BindVulkanPresentGuides(lease,use,queue);};
    }
    frame.width = frame.guideWidth = request_.extent.width;
    frame.height = frame.guideHeight = request_.extent.height;
    frame.creationReset = reset;
    frame.colorLayout = VK_IMAGE_LAYOUT_GENERAL;
    modelResult_ = EvaluatePresentImageVk(frame);
    modelTouched_ = modelResult_.status == VkRecordStatus::Complete && modelResult_.chainDeliverable;
    return modelTouched_;
}

bool VkPresentPrivateFrame::ConvertFromModel()
{
    const auto recipe=SelectVkPresentColorRecipe(request_.format,request_.colorSpace);
    if(recipe.revision==2) {
        if(!color_||!colorReservation_)return false;
        Move(images_[ModelOutput],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        Move(images_[Captured],VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_READ_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        Move(images_[Encoded],VK_IMAGE_LAYOUT_GENERAL,VK_ACCESS_SHADER_WRITE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
        if(!color_->Encode(commandBuffer_,*colorReservation_,images_[Captured].view,images_[ModelOutput].view,
            images_[Encoded].view,{request_.format,request_.colorSpace,recipe.revision}))return false;
        Move(images_[Encoded],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        Move(images_[OutputCopy],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        const auto region=BlitRegion();
        vkCmdBlitImage(commandBuffer_,images_[Encoded].image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            images_[OutputCopy].image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region,VK_FILTER_NEAREST);
        Move(images_[OutputCopy],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_READ_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT);
        return true;
    }
    Move(images_[ModelOutput], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    Move(images_[OutputCopy], VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_ACCESS_TRANSFER_WRITE_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    const auto region = BlitRegion();
    vkCmdBlitImage(commandBuffer_, images_[ModelOutput].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   images_[OutputCopy].image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   1, &region, SelectVkPresentColorRecipe(request_.format,request_.colorSpace).filter);
    Move(images_[OutputCopy], VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, VK_ACCESS_TRANSFER_READ_BIT,
         VK_PIPELINE_STAGE_TRANSFER_BIT);
    return true;
}

void VkPresentPrivateFrame::TargetToTransfer()
{
    MoveGame(VK_IMAGE_LAYOUT_PRESENT_SRC_KHR, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 0,
             VK_ACCESS_TRANSFER_WRITE_BIT, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT);
}

void VkPresentPrivateFrame::CopyBack()
{
    VkImageCopy region {};
    region.srcSubresource = { VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1 };
    region.dstSubresource = region.srcSubresource;
    region.extent = { request_.extent.width, request_.extent.height, 1 };
    vkCmdCopyImage(commandBuffer_, images_[OutputCopy].image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                   request_.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void VkPresentPrivateFrame::TargetToPresent()
{
    MoveGame(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
             VK_ACCESS_TRANSFER_WRITE_BIT, 0, VK_PIPELINE_STAGE_TRANSFER_BIT,
             VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT);
}

bool VkPresentPrivateFrame::End() { return vkEndCommandBuffer(commandBuffer_) == VK_SUCCESS; }
void VkPresentPrivateFrame::Discard()
{
    DiscardCapturedGuides();
}
bool VkPresentPrivateFrame::DiscardCapturedGuides()
{
    AbandonPresentRecordingVk(modelTouched_);
    modelTouched_ = false;
    if (vkResetCommandPool(device_, commandPool_, 0) == VK_SUCCESS) {
        // Discard predicted layouts; future full writes may start with undefined contents.
        for (auto& image : images_) image.layout = VK_IMAGE_LAYOUT_UNDEFINED;
        ReleaseCapturedGuides();return true;
    }
    MarkPresentRuntimeUncertainVk("Vulkan Present command-pool discard failed");return false;
}

} // namespace DlssNr
