#define VULKAN_GUIDE_PACKET_EMBEDDED
#include "vulkan_guide_packet.cpp"
#include "../OptiScaler/dlssnr/VulkanPresentGuides.h"
#include "../OptiScaler/dlssnr/DlssNr_PresentInputDecision.h"
#include "../OptiScaler/dlssnr/VulkanNrRenderSize.h"
#include "../OptiScaler/dlssnr/VulkanNrGuideLayout.h"
#include "../OptiScaler/dlssnr/VulkanNrInputLayout.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool v,const char* why){if(!v){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
static NVSDK_NGX_Resource_VK Image(uintptr_t id,VkFormat format,VkImageAspectFlags aspect)
{
    NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;
    auto& v=r.Resource.ImageViewInfo;v.Image=reinterpret_cast<VkImage>(id);v.ImageView=reinterpret_cast<VkImageView>(id+1);
    v.Width=v.Height=32;v.Format=format;v.SubresourceRange={aspect,0,1,0,1};return r;
}
template<class Facts>
static auto NgxDepthRights(Facts& facts,VkDevice device,const NVSDK_NGX_Resource_VK& image,bool authenticated)
{
    if constexpr(requires { facts.NgxDepthRights(device,image,authenticated); })
        return facts.NgxDepthRights(device,image,authenticated);
    else return facts.Rights(device,image,VK_IMAGE_USAGE_SAMPLED_BIT,true);
}
int main()
{
    Check(TrackVkNrCommandState(false,true),"native Vulkan observations cannot require a D3D12 bridge");
    VkNrImageFacts facts;auto device=reinterpret_cast<VkDevice>(1);auto depth=Image(100,VK_FORMAT_D24_UNORM_S8_UINT,VK_IMAGE_ASPECT_DEPTH_BIT);
    VkImageCreateInfo create{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};create.imageType=VK_IMAGE_TYPE_2D;create.format=depth.Resource.ImageViewInfo.Format;
    create.extent={32,32,1};create.mipLevels=create.arrayLayers=1;create.samples=VK_SAMPLE_COUNT_1_BIT;
    create.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;create.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
    VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=depth.Resource.ImageViewInfo.Image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
    view.format=create.format;view.subresourceRange=depth.Resource.ImageViewInfo.SubresourceRange;
    facts.Created(device,create,view.image,VK_ERROR_OUT_OF_DEVICE_MEMORY);facts.ViewCreated(device,view,depth.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,depth,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true),"failed image creation grants no rights");
    facts.Created(device,create,view.image,VK_SUCCESS);facts.ViewCreated(device,view,depth.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    const auto rights=facts.Rights(device,depth,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true);
    Check(rights&&rights->copyAspect==VK_IMAGE_ASPECT_DEPTH_BIT&&rights->barrierAspect==(VK_IMAGE_ASPECT_DEPTH_BIT|VK_IMAGE_ASPECT_STENCIL_BIT),
        "depth/stencil copies depth only and transitions both aspects without separate-layout assumptions");
    {
        // Exact DOOM 035934 creation facts: D32S8 depth, sampled/attachment usage,
        // depth-only view, and an explicit (R,0,0,1) component mapping.
        auto doomDepth=Image(300,VK_FORMAT_D32_SFLOAT_S8_UINT,VK_IMAGE_ASPECT_DEPTH_BIT);
        doomDepth.Resource.ImageViewInfo.Width=2560;doomDepth.Resource.ImageViewInfo.Height=1440;
        auto dc=create;dc.format=VK_FORMAT_D32_SFLOAT_S8_UINT;dc.extent={2560,1440,1};
        dc.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_SAMPLED_BIT;
        auto dv=view;dv.image=doomDepth.Resource.ImageViewInfo.Image;dv.format=dc.format;
        dv.components={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE};
        facts.Created(device,dc,dv.image,VK_SUCCESS);facts.ViewCreated(device,dv,doomDepth.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(facts.Rights(device,doomDepth,VK_IMAGE_USAGE_SAMPLED_BIT,true).has_value(),"DOOM R001 depth view admits scalar sampled depth");
        Check(!facts.Rights(device,doomDepth,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true),"DOOM depth still cannot use image transfer");
        dv.components.r=VK_COMPONENT_SWIZZLE_ZERO;
        facts.ViewCreated(device,dv,doomDepth.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(!facts.Rights(device,doomDepth,VK_IMAGE_USAGE_SAMPLED_BIT,true),"depth red mapping still must preserve the actual depth value");
        dv.components.r=VK_COMPONENT_SWIZZLE_IDENTITY;
        facts.ViewCreated(device,dv,doomDepth.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(facts.Rights(device,doomDepth,VK_IMAGE_USAGE_SAMPLED_BIT,true).has_value(),"identity depth red also preserves scalar value");
    }
    auto color=Image(200,VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT);
    {
        // Streamline's default NGX wrapper range has COLOR for every format when
        // the optional SubresourceRange tag is absent. The real tracked view is authoritative.
        auto da=Image(700,VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_COLOR_BIT);
        da.Resource.ImageViewInfo.SubresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,VK_REMAINING_MIP_LEVELS,0,VK_REMAINING_ARRAY_LAYERS};
        auto dc=create;dc.format=VK_FORMAT_D32_SFLOAT;dc.usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        auto dv=view;dv.image=da.Resource.ImageViewInfo.Image;dv.format=dc.format;
        dv.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};
        dv.components={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ZERO,VK_COMPONENT_SWIZZLE_ONE};
        facts.Created(device,dc,dv.image,VK_SUCCESS);facts.ViewCreated(device,dv,da.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(!facts.Rights(device,da,VK_IMAGE_USAGE_SAMPLED_BIT,true),"generic Rights still refuses contradictory aspects");
        const auto repaired=NgxDepthRights(facts,device,da,true);
        Check(repaired&&repaired->view.range.aspectMask==VK_IMAGE_ASPECT_DEPTH_BIT,"authenticated NGX default depth range uses observed DEPTH view");
        Check(da.Resource.ImageViewInfo.SubresourceRange.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT,"caller wrapper remains immutable");
        Check(!NgxDepthRights(facts,device,da,false),"no provider input contract cannot canonicalize depth");
        auto bad=da;bad.Resource.ImageViewInfo.SubresourceRange.levelCount=1;
        Check(!NgxDepthRights(facts,device,bad,true),"explicit contradictory range is not a default wrapper");
        bad=da;bad.Resource.ImageViewInfo.Format=VK_FORMAT_R32_SFLOAT;
        Check(!NgxDepthRights(facts,device,bad,true),"color format cannot borrow depth canonicalization");
        bad=da;bad.Resource.ImageViewInfo.Image=reinterpret_cast<VkImage>(999);
        Check(!NgxDepthRights(facts,device,bad,true),"wrong image identity cannot canonicalize depth");
        Check(!NgxDepthRights(facts,reinterpret_cast<VkDevice>(999),da,true),"wrong device cannot canonicalize depth");
        dv.subresourceRange.aspectMask=VK_IMAGE_ASPECT_STENCIL_BIT;
        facts.ViewCreated(device,dv,da.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(!NgxDepthRights(facts,device,da,true),"tracked stencil view cannot be used as depth");
        dv.subresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        facts.ViewCreated(device,dv,da.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        facts.Created(device,dc,dv.image,VK_SUCCESS);
        Check(!NgxDepthRights(facts,device,da,true),"reused image cannot revive stale depth view");
    }
    auto colorCreate=create;colorCreate.format=color.Resource.ImageViewInfo.Format;colorCreate.extent={64,64,1};
    VkFormat format=colorCreate.format;
    VkImageFormatListCreateInfo formatList{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO};
    formatList.viewFormatCount=1;formatList.pViewFormats=&format;colorCreate.pNext=&formatList;
    auto colorView=view;colorView.image=color.Resource.ImageViewInfo.Image;colorView.format=colorCreate.format;
    colorView.subresourceRange=color.Resource.ImageViewInfo.SubresourceRange;
    VkImageViewUsageCreateInfo usage{VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO};usage.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    colorView.pNext=&usage;colorView.components={VK_COMPONENT_SWIZZLE_R,VK_COMPONENT_SWIZZLE_G,VK_COMPONENT_SWIZZLE_B,VK_COMPONENT_SWIZZLE_A};
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false).has_value(),
        "legal format-list/view-usage chains and explicit identity swizzles allow a bounded smaller render wrapper");
    Check(facts.Rights(device,color,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,false).has_value(),
        "image transfer rights remain valid with a sampled-only view usage override");
    colorCreate.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    const char* accessReason=nullptr;
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,false,&accessReason)&&
        std::string(accessReason)=="source image usage lacks the required access","missing image transfer rights remain refused precisely");
    Check(facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false).has_value(),"sample-only source remains eligible for sampled guide capture");
    colorCreate.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT;
    usage.usage=VK_IMAGE_USAGE_STORAGE_BIT;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false,&accessReason)&&
        std::string(accessReason)=="source image-view usage lacks the required access","restricted view cannot inherit image sample rights");
    usage.usage=VK_IMAGE_USAGE_SAMPLED_BIT;
    facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    color.Resource.ImageViewInfo.Width=65;
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false,&accessReason)&&
        std::string(accessReason)=="wrapper extent exceeds observed image extent","extent refusal identifies the exact field");
    const auto detail=facts.Describe(device,color);
    Check(detail.find("extent=65x32")!=std::string::npos&&detail.find("extent=64x64x1")!=std::string::npos&&
        detail.find("swizzle=3,4,5,6")!=std::string::npos&&detail.find("range=1,0,1,0,1")!=std::string::npos,
        "diagnostic preserves distinct wrapper, image and view facts");
    color.Resource.ImageViewInfo.Width=32;
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_APPLICATION_INFO};colorCreate.pNext=&unknown;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"unknown image chain cannot grant rights");
    colorCreate.pNext=&formatList;colorView.components.a=VK_COMPONENT_SWIZZLE_ONE;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false,&accessReason)&&
        std::string(accessReason)=="source view alpha swizzle is unsupported","channel refusal identifies alpha instead of a combined format error");
    colorView.components.a=VK_COMPONENT_SWIZZLE_A;colorView.subresourceRange.levelCount=VK_REMAINING_MIP_LEVELS;
    facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    auto canonical=facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false,&accessReason);
    Check(canonical&&canonical->view.range.levelCount==1,
        "remaining-mip view and explicit single-mip wrapper identify the same observed subresource");
    Check(facts.Describe(device,color).find("range=1,0,4294967295,0,1")!=std::string::npos,"remaining-mip sentinel is visible in diagnostics");
    colorView.subresourceRange.levelCount=1;colorView.subresourceRange.layerCount=VK_REMAINING_ARRAY_LAYERS;
    color.Resource.ImageViewInfo.SubresourceRange.levelCount=VK_REMAINING_MIP_LEVELS;
    facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    canonical=facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false);
    Check(canonical&&canonical->view.range.levelCount==1&&canonical->view.range.layerCount==1,
        "wrapper and view remaining counts resolve independently against the same observed allocation");
    Check(ResolveVkNrInputLayout(true,canonical->view.range,std::nullopt,std::nullopt).accepted,
        "Native input-layout admission consumes the canonical observed single subresource");
    color.Resource.ImageViewInfo.SubresourceRange.layerCount=VK_REMAINING_ARRAY_LAYERS;
    Check(facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false).has_value(),"remaining layer counts are equivalent for a single-layer image");
    const auto singleCreate=colorCreate;const auto singleView=colorView;const auto singleWrapper=color;
    colorCreate.mipLevels=2;colorView.subresourceRange.levelCount=VK_REMAINING_MIP_LEVELS;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"remaining mip count cannot admit a multi-mip view");
    colorCreate=singleCreate;colorCreate.arrayLayers=2;colorView=singleView;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"remaining layer count cannot admit a multi-layer view");
    colorCreate=singleCreate;colorView=singleView;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    for(bool wrapper:{false,true})for(bool mip:{false,true})for(uint32_t invalid:{0u,2u}){
        color=singleWrapper;colorView=singleView;
        auto& range=wrapper?color.Resource.ImageViewInfo.SubresourceRange:colorView.subresourceRange;
        if(mip)range.levelCount=invalid;else range.layerCount=invalid;
        facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"empty or oversized explicit wrapper/view counts refuse");
    }
    for(bool wrapper:{false,true})for(bool mip:{false,true}){
        color=singleWrapper;colorView=singleView;
        auto& range=wrapper?color.Resource.ImageViewInfo.SubresourceRange:colorView.subresourceRange;
        if(mip){range.baseMipLevel=1;range.levelCount=VK_REMAINING_MIP_LEVELS;}
        else {range.baseArrayLayer=1;range.layerCount=VK_REMAINING_ARRAY_LAYERS;}
        facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
        Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"out-of-allocation remaining range base refuses");
    }
    color=singleWrapper;colorView=singleView;
    facts.ViewCreated(device,colorView,color.Resource.ImageViewInfo.ImageView,VK_SUCCESS);
    color.Resource.ImageViewInfo.SubresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"canonical counts cannot hide a mismatched aspect");
    color=singleWrapper;color.Resource.ImageViewInfo.Image=depth.Resource.ImageViewInfo.Image;
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"canonical ranges cannot grant a view of another image");
    color=singleWrapper;
    facts.Created(device,colorCreate,colorView.image,VK_SUCCESS);
    Check(!facts.Rights(device,color,VK_IMAGE_USAGE_SAMPLED_BIT,false),"remaining ranges cannot revive a view from an old image generation");
    facts.Destroyed(device,view.image);Check(!facts.Rights(device,depth,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true),"destroy revokes borrowed image rights");
    facts.Created(device,create,view.image,VK_SUCCESS);Check(!facts.Rights(device,depth,VK_IMAGE_USAGE_TRANSFER_SRC_BIT,true),"reused image handle does not revive stale view");
    // The old interval fixtures deliberately admitted before-acquire guide images.
    // Preserve image-rights coverage above; the embedded packet suite now tests
    // exact association, returned-call success, actual completion and retention.
    const auto packetResult=GuidePacketFixture::Run();
    if(!packetResult)std::puts("observed image rights and exact successful guide packet transport verified");
    return packetResult;
}
