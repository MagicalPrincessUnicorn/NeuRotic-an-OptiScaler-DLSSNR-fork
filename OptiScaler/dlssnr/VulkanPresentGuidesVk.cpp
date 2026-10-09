#include "pch.h"
#include "VulkanPresentGuidesVk.h"
#include "VulkanPresentRegistry.h"
#include "VulkanNrPresentTag.h"
#include "VulkanNrPresentFrame.h"
#include "VulkanNrCompletion.h"
#include "VulkanNrFrameParams.h"
#include "VulkanNrGuideLayout.h"
#include <hooks/VulkanwDx12_Hooks.h>
#include <shaders/dlssnr/precompile/GuideCopy_Shader_Vk.h>
#include <cstdio>
namespace DlssNr
{
namespace
{
std::mutex guideMutex;
#include "VulkanNrGuideSampleCopy.inl"
struct GuideImages final : VkNrGuideImages
{
    VkDevice device=VK_NULL_HANDLE;
    GuideCopyBindings sampled;
    void Abandon() override {sampled.Abandon();device=VK_NULL_HANDLE;image={};view={};memory={};}
    std::array<VkImage,2> image{};std::array<VkImageView,2> view{};std::array<VkDeviceMemory,2> memory{};
};
bool ReleaseImages(VkNrGuideImages& base)
{
    auto& r=static_cast<GuideImages&>(base);
    r.sampled.Release(r.device);
    for(size_t i=0;i<2;++i) {
        if(r.view[i])vkDestroyImageView(r.device,r.view[i],nullptr);
        if(r.image[i])vkDestroyImage(r.device,r.image[i],nullptr);
        if(r.memory[i])vkFreeMemory(r.device,r.memory[i],nullptr);
        r.view[i]=VK_NULL_HANDLE;r.image[i]=VK_NULL_HANDLE;r.memory[i]=VK_NULL_HANDLE;
    }
    return true;
}
bool CreateGuide(GuideImages& r,size_t index,VkPhysicalDevice physical,VkFormat format,VkImageAspectFlags aspect,
                 const VkNrRect& rect,uint64_t& remaining,bool sampled=false)
{
    VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(physical,format,&props);
    const VkFormatFeatureFlags required=sampled?(VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT):
        (VK_FORMAT_FEATURE_TRANSFER_SRC_BIT|VK_FORMAT_FEATURE_TRANSFER_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT);
    if((props.optimalTilingFeatures&required)!=required)return false;
    VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=format;
    image.extent={rect.width,rect.height,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;
    image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=sampled?(VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT):
        (VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT);
    image.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    if(r.concurrentFamilies.size()>1){image.sharingMode=VK_SHARING_MODE_CONCURRENT;
        image.queueFamilyIndexCount=static_cast<uint32_t>(r.concurrentFamilies.size());image.pQueueFamilyIndices=r.concurrentFamilies.data();}
    if(vkCreateImage(r.device,&image,nullptr,&r.image[index])!=VK_SUCCESS)return false;
    VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(r.device,r.image[index],&requirements);
    if(requirements.size>remaining)return false;
    VkPhysicalDeviceMemoryProperties memory{};vkGetPhysicalDeviceMemoryProperties(physical,&memory);
    uint32_t type=UINT32_MAX;
    for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((requirements.memoryTypeBits&(1u<<i))&&
        (memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)){type=i;break;}
    if(type==UINT32_MAX)return false;
    VkMemoryAllocateInfo allocation{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};allocation.allocationSize=requirements.size;allocation.memoryTypeIndex=type;
    if(vkAllocateMemory(r.device,&allocation,nullptr,&r.memory[index])!=VK_SUCCESS||
        vkBindImageMemory(r.device,r.image[index],r.memory[index],0)!=VK_SUCCESS)return false;
    remaining-=requirements.size;r.bytes+=requirements.size;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=r.image[index];view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=format;view.subresourceRange={aspect,0,1,0,1};
    if(vkCreateImageView(r.device,&view,nullptr,&r.view[index])!=VK_SUCCESS)return false;
    auto& ngx=index?r.motion:r.depth;ngx.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    auto& w=ngx.Resource.ImageViewInfo;w.Image=r.image[index];w.ImageView=r.view[index];w.SubresourceRange=view.subresourceRange;
    w.Format=format;w.Width=rect.width;w.Height=rect.height;ngx.ReadWrite=false;return true;
}
void Barrier(VkCommandBuffer cmd,VkImage image,VkImageAspectFlags aspects,VkImageLayout from,VkImageLayout to,
             VkAccessFlags src,VkAccessFlags dst)
{
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image;b.oldLayout=from;b.newLayout=to;
    b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange={aspects,0,1,0,1};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
VkNrGuideCreation CopyGuides(const VkFrameRequest& request,VkNrUseId,uint64_t budget)
{
    if(!request.depth||!request.motion||!request.contract.temporal)
        return {{},false,false,"Native depth/motion frame bindings missing"};
    const char* depthReason=nullptr;const char* motionReason=nullptr;
    const auto depth=VulkanNrImageFacts().NgxDepthRights(request.device,*request.depth,request.ngxGuideInputIdentity,&depthReason);
    const auto motion=VulkanNrImageFacts().Rights(request.device,*request.motion,VK_IMAGE_USAGE_SAMPLED_BIT,false,&motionReason);
    if(!depth||!motion)return {{},false,false,std::string(!depth?"Depth guide: ":"Motion guide: ")+
        (!depth?(depthReason?depthReason:"copy rights unavailable"):(motionReason?motionReason:"copy rights unavailable"))+
        " | depth: "+VulkanNrImageFacts().Describe(request.device,*request.depth)+
        " | motion: "+(motion?"access accepted":(motionReason?motionReason:"access unavailable"))+" "+
        VulkanNrImageFacts().Describe(request.device,*request.motion)};
    const auto mf=motion->image.format;
    if(mf!=VK_FORMAT_R16G16_SFLOAT&&mf!=VK_FORMAT_R32G32_SFLOAT&&mf!=VK_FORMAT_R16G16_SNORM&&mf!=VK_FORMAT_R16G16_UNORM&&
       mf!=VK_FORMAT_R16G16B16A16_SFLOAT&&mf!=VK_FORMAT_R32G32B32A32_SFLOAT)
        return {{},false,false,"Native motion guide format is unsupported"};
    vk_state::CommandBufferState saved;
    if(!Vulkan_wDx12::cmdBufferStateTracker.CaptureNrState(request.commandBuffer,saved))
        return {{},false,false,"Native guide command-state observation unavailable"};
#ifndef LOW_PRECISION_TRACKING
    const std::array<VkNrImageRights,2> rights{*depth,*motion};
    const auto& temporal=*request.contract.temporal;
    const std::array<VkNrRect,2> rects{temporal.depth,temporal.motion};
    const std::array<NVSDK_NGX_Resource_VK*,2> sources{request.depth,request.motion};
    std::array<VkImageLayout,2> layouts{};
    bool sampled=false;
    // Sampling needs only the view's depth aspect. Transfer temporarily changes
    // every barrier aspect, so a combined stencil image needs stronger evidence.
    for(size_t i=0;i<2;++i) {
        const auto image=sources[i]->Resource.ImageViewInfo.Image;
        const auto layout=saved.ImageLayouts.find(image);const auto range=saved.ImageLayoutRanges.find(image);
        const auto transferLayout=ResolveVkNrGuideLayout(request.ngxSrInputContract||request.ngxGuideInputReadContract,rights[i].barrierAspect,
            layout==saved.ImageLayouts.end()?std::nullopt:std::optional<VkImageLayout>(layout->second),
            range==saved.ImageLayoutRanges.end()?std::nullopt:std::optional<VkImageSubresourceRange>(range->second));
        VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(request.physicalDevice,rights[i].image.format,&props);
        sampled=sampled||!(rights[i].image.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)||
            !(props.optimalTilingFeatures&VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)||!transferLayout.accepted;
    }
    if(sampled) {
        if(!VulkanNrRecordings().BindingsQualified(request.commandBuffer)||!saved.Recording||saved.InRenderPass)
            return {{},false,false,"Sampled guide copy cannot restore this command buffer's bindings"};
        uint32_t count=0;vkGetPhysicalDeviceQueueFamilyProperties(request.physicalDevice,&count,nullptr);
        std::vector<VkQueueFamilyProperties> families(count);vkGetPhysicalDeviceQueueFamilyProperties(request.physicalDevice,&count,families.data());
        const auto family=VulkanNrRecordings().CommandFamily(request.commandBuffer);
        if(family>=count||!(families[family].queueFlags&VK_QUEUE_COMPUTE_BIT))
            return {{},false,false,"Sampled guide copy requires an observed compute-capable command family"};
    }
    for(size_t i=0;i<2;++i) {
        const auto image=sources[i]->Resource.ImageViewInfo.Image;
        const auto layout=saved.ImageLayouts.find(image);const auto range=saved.ImageLayoutRanges.find(image);
        VkFrame::Failure failure;
        const auto sourceLayout=ResolveVkNrGuideLayout(request.ngxSrInputContract||request.ngxGuideInputReadContract,sampled?rights[i].view.range.aspectMask:rights[i].barrierAspect,
            layout==saved.ImageLayouts.end()?std::nullopt:std::optional<VkImageLayout>(layout->second),
            range==saved.ImageLayoutRanges.end()?std::nullopt:std::optional<VkImageSubresourceRange>(range->second));
        if(!sourceLayout.accepted)
            return {{},false,false,sourceLayout.reason};
        if(!VkFrame::ValidateRect("Native guide",sources[i],rects[i].x,rects[i].y,rects[i].width,rects[i].height,false,failure))
            return {{},false,false,"Native guide source subresource layout or active rectangle is unproved"};
        VkFormatProperties props{};vkGetPhysicalDeviceFormatProperties(request.physicalDevice,rights[i].image.format,&props);
        if(!(props.optimalTilingFeatures&(sampled?VK_FORMAT_FEATURE_SAMPLED_IMAGE_BIT:VK_FORMAT_FEATURE_TRANSFER_SRC_BIT)))
            return {{},false,false,"Native guide format lacks the selected copy feature"};
        layouts[i]=sourceLayout.layout;
    }
    auto images=std::make_shared<GuideImages>();images->device=request.device;
    images->concurrentFamilies=GetVulkanPresentRegistry().CreatedQueueFamilies(request.device);
    const auto sourceFamily=VulkanNrRecordings().CommandFamily(request.commandBuffer);
    if(std::find(images->concurrentFamilies.begin(),images->concurrentFamilies.end(),sourceFamily)==images->concurrentFamilies.end())
        return {{},false,false,"private guide source family was not observed at device creation"};
    if(images->concurrentFamilies.size()==1)images->concurrentFamilies.clear(); // exclusive source family only
    for(size_t i=0;i<2;++i)if(!CreateGuide(*images,i,request.physicalDevice,
        sampled?(i?VK_FORMAT_R32G32B32A32_SFLOAT:VK_FORMAT_R32_SFLOAT):rights[i].image.format,
        sampled?VK_IMAGE_ASPECT_COLOR_BIT:rights[i].copyAspect,rects[i],budget,sampled)) {
        ReleaseImages(*images);return {{},false,false,"private guide allocation/features exceed actual 512 MiB capacity"};
    }
    if(sampled) {
        VkPhysicalDeviceProperties properties{};vkGetPhysicalDeviceProperties(request.physicalDevice,&properties);
        for(const auto& rect:rects)if((static_cast<uint64_t>(rect.width)+7)/8>properties.limits.maxComputeWorkGroupCount[0]||
            (static_cast<uint64_t>(rect.height)+7)/8>properties.limits.maxComputeWorkGroupCount[1]) {
            ReleaseImages(*images);return {{},false,false,"Sampled guide dimensions exceed compute dispatch limits"};
        }
        if(!PrepareGuideCopyBindings(images->sampled,request.device,request.physicalDevice,request.contract.deviceGeneration,
            sources,images->view,layouts,rects,budget,images->bytes)) {
            ReleaseImages(*images);return {{},false,false,"Sampled guide program or immutable bindings allocation failed"};
        }
        VkMemoryBarrier readable{VK_STRUCTURE_TYPE_MEMORY_BARRIER};
        readable.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;readable.dstAccessMask=VK_ACCESS_SHADER_READ_BIT;
        vkCmdPipelineBarrier(request.commandBuffer,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,1,&readable,0,nullptr,0,nullptr);
        // No borrowed-image layout transition and no push constants. The original
        // SR invocation sees exactly its original guide images and numeric values.
        for(size_t i=0;i<2;++i) {
            Barrier(request.commandBuffer,images->image[i],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_UNDEFINED,
                VK_IMAGE_LAYOUT_GENERAL,0,VK_ACCESS_SHADER_WRITE_BIT);
            vkCmdBindPipeline(request.commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,images->sampled.program->pipeline[i]);
            vkCmdBindDescriptorSets(request.commandBuffer,VK_PIPELINE_BIND_POINT_COMPUTE,images->sampled.program->pipelineLayout,
                0,1,&images->sampled.set[i],0,nullptr);
            vkCmdDispatch(request.commandBuffer,(rects[i].width+7)/8,(rects[i].height+7)/8,1);
            Barrier(request.commandBuffer,images->image[i],VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_LAYOUT_GENERAL,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_SHADER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
        }
        vk_state::ReplayParams replay;replay.RequiredGraphicsSetMask=UINT32_MAX;
        replay.ReplayComputeToo=true;replay.ReplayVertexIndex=true;
        // We did not change push constants; replaying a partially observed range
        // would be less faithful than retaining the untouched game values.
        replay.ReplayPushConstants=false;
        const bool restored=Vulkan_wDx12::cmdBufferStateTracker.ReplaySaved(request.commandBuffer,saved,replay);
        if(!restored)return {images,true,false,"Sampled guide commands recorded but original bindings could not be restored"};
        static thread_local bool loggedSampled=false;
        if(!loggedSampled){loggedSampled=true;LOG_INFO("Vulkan Present guide copy method: exact sampled depth/motion capture");}
        return {images,true,true,{}};
    }
    for(size_t i=0;i<2;++i) {
        const auto source=sources[i]->Resource.ImageViewInfo.Image;
        Barrier(request.commandBuffer,source,rights[i].barrierAspect,layouts[i],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        Barrier(request.commandBuffer,images->image[i],rights[i].barrierAspect,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
            0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageCopy copy{};copy.srcSubresource={rights[i].copyAspect,0,0,1};copy.dstSubresource=copy.srcSubresource;
        copy.srcOffset={static_cast<int32_t>(rects[i].x),static_cast<int32_t>(rects[i].y),0};copy.extent={rects[i].width,rects[i].height,1};
        vkCmdCopyImage(request.commandBuffer,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,images->image[i],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
        Barrier(request.commandBuffer,source,rights[i].barrierAspect,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layouts[i],VK_ACCESS_TRANSFER_READ_BIT,
            VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
        Barrier(request.commandBuffer,images->image[i],rights[i].barrierAspect,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
            VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT);
    }
    static thread_local bool loggedTransfer=false;
    if(!loggedTransfer){loggedTransfer=true;LOG_INFO("Vulkan Present guide copy method: direct image transfer");}
    return {images,true,true,{}};
#else
    (void)budget;return {{},false,false,"Native guides require full source-layout tracking"};
#endif
}
VulkanPresentGuides& Guides()
{ static VulkanPresentGuides owner(VulkanNrRecordings(),CopyGuides,ReleaseImages);return owner; }
}
static void LogGuideCapture(const std::string& reason)
{
    static thread_local std::string last="unobserved";
    if(last!=reason){last=reason;LOG_INFO("Vulkan Present guide capture: {}",reason.empty()?"private depth/motion copied; awaiting successful SR and matching Present":reason);}
}
// Only the first sixteen sources are traced. Use module names and relative
// offsets, never process addresses or user filesystem paths.
static void LogGuideCaptureStack(VkNrUseId use,const VkFrameRequest& request)
{
    void* frames[12]{};const auto count=CaptureStackBackTrace(1,12,frames,nullptr);
    std::string stack;
    for(USHORT i=0;i<count;++i){
        HMODULE module=nullptr;char path[MAX_PATH]{};
        if(GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(frames[i]),&module)&&GetModuleFileNameA(module,path,MAX_PATH)){
            path[MAX_PATH-1]='\0';
            const std::string name(path);const auto separator=name.find_last_of("/\\");
            char offset[32]{};
            std::snprintf(offset,sizeof(offset),"+0x%llx",static_cast<unsigned long long>(
                reinterpret_cast<uintptr_t>(frames[i])-reinterpret_cast<uintptr_t>(module)));
            stack+=(i?";":"")+name.substr(separator==std::string::npos?0:separator+1)+offset;
        }else stack+=i?";unresolved":"unresolved";
    }
    LOG_INFO("Vulkan guide lifetime capture: use={} acquire={} image={} stack=[{}]",use.value,
        request.contract.acquireGeneration,request.contract.swapchainImageIndex,stack);
}
static void LogGuideLifetimes()
{
    static std::array<uint64_t,16> reported{};
    const auto traces=VulkanNrRecordings().GuideTraces();
    for(size_t i=0;i<traces.size();++i){const auto& t=traces[i];
        const auto revision=std::max({t.capture,t.end,t.present,t.selected,t.root,t.returned,t.closed});
        if(reported[i]==revision)continue;reported[i]=revision;
        LOG_INFO("Vulkan guide lifetime: use={} incarnation={} thread={} submitDepth={} capture={} end={} "
            "firstPresent={} snapshotSource={} acceptedRoot={} returned={} result={} closed={} closeReason={} returnPath={}",
            t.use,t.incarnation,t.thread,t.submitDepth,t.capture,t.end,t.present,t.selected,t.root,t.returned,
            static_cast<int>(t.result),t.closed,t.closeReason,t.returnPath);
    }
}
void CaptureVulkanPresentGuides(const VkFrameRequest& request)
{
    std::lock_guard lock(guideMutex);
    if(const char* reason=VulkanNrRecordings().ModelRecordingReason(request.commandBuffer)) {
        Guides().RejectNative(reason);LogGuideCapture(Guides().Reason());return;
    }
    auto use=VulkanNrRecordings().ReserveCopy(request.commandBuffer,request.contract.deviceGeneration);
    if(!use||!ReserveVkNrCompletion(request.device,*use)) {
        if(use)VulkanNrRecordings().CancelBeforeRecording(*use);
        Guides().RejectNative("guide recording/completion capacity unavailable");LogGuideCapture(Guides().Reason());return;
    }
    if(VulkanNrRecordings().TraceGuideCapture(*use))LogGuideCaptureStack(*use,request);
    Guides().Capture(request,*use);LogGuideCapture(Guides().Reason());
}
void RejectVulkanNativeGuides(const char* reason)
{ std::lock_guard lock(guideMutex);Guides().RejectNative(reason);LogGuideCapture(Guides().Reason()); }
void ObserveVulkanPresentTags(uint64_t provider,uint64_t frame,uint32_t viewport,const sl::ResourceTag* tags,uint32_t count,bool succeeded)
{
    VulkanPublicPresentFrames().Tags(provider,frame,viewport,succeeded&&tags&&count<=64);
    // Resolve WSI identity before taking the guide lock; do not invert registry/guide ownership.
    const auto parsed=ParseVkNrPresentTag(provider,frame,viewport,tags,count,succeeded,
        [](VkImage image,VkFormat format,VkExtent2D extent){return GetVulkanPresentRegistry().TaggedImageContext(image,format,extent);});
    std::lock_guard lock(guideMutex);
    bool accepted=false;
    if(parsed.tag)accepted=Guides().ObservePresentTag(*parsed.tag);
    else if(parsed.observed)Guides().RevokePresentTags(provider,viewport);
    static uint64_t calls=0,reports=0;static std::string last;const auto n=++calls;
    const auto reason=std::string(parsed.reason);
    if(reports<64&&(n<=4||reason!=last||n%1024==0)){++reports;last=reason;
        LOG_INFO("Vulkan public Present tag: backbuffer={} accepted={} provider={} viewport={} reason=[{}] calls={}",
            parsed.observed,accepted,provider,viewport,reason,n);}
}
void RevokeVulkanPresentTags(){std::lock_guard lock(guideMutex);Guides().RevokePresentTags(0);}
void CompleteVulkanGuideEvaluation(const VkNrEvaluationIdentity& evaluation,bool succeeded)
{std::lock_guard lock(guideMutex);Guides().CompleteEvaluation(evaluation,succeeded);}
std::optional<VkNrGuideLease> SelectVulkanRecordingGuides(const VkNrFrameContract& f,VkCommandBuffer cb,VkImage output){std::lock_guard lock(guideMutex);Guides().RetireCompleted(VulkanNrRecordings());return Guides().SelectRecording(f,cb,output);}
std::shared_ptr<const VkNrGuideSelection> BeginVulkanGuidePresent(VkQueue queue,const VkNrPresentGuideTag* applicationFrame)
{ std::lock_guard lock(guideMutex);
  if(applicationFrame)Guides().ObservePresentTag(*applicationFrame);
  auto selection=Guides().BeginPresent(queue);LogGuideLifetimes();return selection; }
void RetireInactiveVulkanPresentGuides(VkDevice device)
{ std::lock_guard lock(guideMutex);Guides().RetireInactive(device); }
std::optional<VkNrGuideLease> SelectVulkanPresentGuides(const VkNrFrameContract& frame,const VkNrGuideSelection* selection,VkImage image)
{
    std::lock_guard lock(guideMutex);auto& owner=Guides();owner.RetireCompleted(VulkanNrRecordings());
    auto selected=owner.Select(frame,selection,image);
    // Diagnose the consumer boundary independently of the copy boundary. Raw
    // handles are omitted, and changing frame numbers cannot flood the log.
    static std::string lastReason;static uint64_t calls=0,reports=0;
    const auto reason=selected?std::string("matched private guides"):owner.SelectionReason();
    ++calls;
    if(reports<128&&(reports<4||reason!=lastReason||calls%1024==0)) {
        ++reports;lastReason=reason;
        LOG_INFO("Vulkan Present guide selection: {} facts=[{}]",reason,owner.DescribeSelection(frame,selection));
    }
    return selected;
}
bool BindVulkanPresentGuides(const VkNrGuideLease& lease,VkNrUseId use,VkQueue queue)
{
    std::lock_guard lock(guideMutex);
    if(!Guides().Bind(lease,use,queue))return false;
    if(lease.completedProducer||lease.orderedProducer){
        // Completed producers already have available writes. An earlier accepted
        // submission on this same queue instead needs a write-to-read dependency.
        // Both privately owned images remain in their completed shader-read layout.
        for(const auto* resource:{&lease.images->depth,&lease.images->motion}){
            const auto& image=resource->Resource.ImageViewInfo;
            Barrier(lease.consumerCommand,image.Image,image.SubresourceRange.aspectMask,
                VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                lease.orderedProducer?VK_ACCESS_MEMORY_WRITE_BIT:0,VK_ACCESS_SHADER_READ_BIT);
        }
    }
    return true;
}
std::optional<VkNrFrameContract> SelectVulkanGuideMetadata(const VkNrFrameContract& frame)
{ std::lock_guard lock(guideMutex);return Guides().SelectMetadata(frame); }
std::optional<VkNrFrameContract> SelectVulkanResolutionMetadata(const VkNrFrameContract& frame,const VkNrGuideSelection* selection)
{ std::lock_guard lock(guideMutex);return Guides().SelectResolutionMetadata(frame,selection); }
void VulkanGuidesDeviceDestroyed(VkDevice device)
{ std::lock_guard lock(guideMutex);Guides().AbandonDevice();
  auto found=guideCopyPrograms.find(device);if(found!=guideCopyPrograms.end()){found->second->Abandon();guideCopyPrograms.erase(found);} }
uint64_t VulkanGuidePrivateBytes()
{ std::lock_guard lock(guideMutex);return Guides().PrivateBytes(); }
uint64_t VulkanGuideContextGeneration(const VkNrFrameContract& frame,const VkNrGuideSelection* selection)
{ std::lock_guard lock(guideMutex);return Guides().ContextGeneration(frame,selection); }
}
