#include "VulkanNrImageFacts.h"
#include <algorithm>
#include <sstream>
namespace DlssNr
{
namespace
{
bool ResolveRange(VkImageSubresourceRange& range,uint32_t mips,uint32_t layers)
{
    const auto count=[](uint32_t base,uint32_t& value,uint32_t total,uint32_t remaining){
        if(base>=total)return false;
        const uint32_t available=total-base;
        if(value==remaining)value=available;
        return value&&value<=available;
    };
    return count(range.baseMipLevel,range.levelCount,mips,VK_REMAINING_MIP_LEVELS)&&
        count(range.baseArrayLayer,range.layerCount,layers,VK_REMAINING_ARRAY_LAYERS);
}
}
std::optional<VkNrObservedImage> VkNrImageFacts::Image(VkDevice device,VkImage image) const
{std::lock_guard lock(mutex_);auto i=images_.find({device,image});return i!=images_.end()&&i->second.device==device?std::optional<VkNrObservedImage>(i->second):std::nullopt;}
void VkNrImageFacts::Created(VkDevice device,const VkImageCreateInfo& c,VkImage image,VkResult result)
{
    if(result!=VK_SUCCESS||!device||!image)return;
    std::lock_guard lock(mutex_);VkNrObservedImage r;
    r.device=device;r.generation=next_++;r.format=c.format;r.extent=c.extent;r.usage=c.usage;r.flags=c.flags;
    r.type=c.imageType;r.tiling=c.tiling;r.samples=c.samples;r.mips=c.mipLevels;r.layers=c.arrayLayers;
    r.sharing=c.sharingMode;r.chainKnown=true;
    bool seenFormats=false;unsigned chainLength=0;
    for(auto* next=static_cast<const VkBaseInStructure*>(c.pNext);next;next=next->pNext) {
        if(++chainLength>8 || next->sType!=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO || seenFormats) {
            r.chainKnown=false;break;
        }
        seenFormats=true;
        const auto& list=*reinterpret_cast<const VkImageFormatListCreateInfo*>(next);
        if(list.viewFormatCount>256 || (list.viewFormatCount&&!list.pViewFormats)) {r.chainKnown=false;break;}
        if(list.viewFormatCount)r.viewFormats.assign(list.pViewFormats,list.pViewFormats+list.viewFormatCount);
    }
    if(c.sharingMode==VK_SHARING_MODE_CONCURRENT&&c.queueFamilyIndexCount&&c.pQueueFamilyIndices)
        r.families.assign(c.pQueueFamilyIndices,c.pQueueFamilyIndices+c.queueFamilyIndexCount);
    // A reused image handle starts a new generation; old views must not remain
    // attached to the replacement's reverse index.
    ForgetImageViews({device,image});
    images_[{device,image}]=std::move(r);
}
void VkNrImageFacts::ViewCreated(VkDevice device,const VkImageViewCreateInfo& c,VkImageView view,VkResult result)
{
    if(result!=VK_SUCCESS||!device||!view)return;
    std::lock_guard lock(mutex_);const auto image=images_.find({device,c.image});if(image==images_.end()||image->second.device!=device)return;
    ForgetView({device,view});
    views_[{device,view}]={device,c.image,image->second.generation,next_++,c.format,c.viewType,c.subresourceRange,c.components,c.pNext==nullptr};
    imageViews_[{device,c.image}].insert({device,view});
    auto& observed=views_[{device,view}];observed.chainKnown=true;unsigned chainLength=0;
    for(auto* next=static_cast<const VkBaseInStructure*>(c.pNext);next;next=next->pNext) {
        if(++chainLength>8 || next->sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO || observed.usage) {
            observed.chainKnown=false;break;
        }
        observed.usage=reinterpret_cast<const VkImageViewUsageCreateInfo*>(next)->usage;
    }
}
void VkNrImageFacts::ForgetImageViews(ImageKey image)
{
    const auto linked=imageViews_.find(image);if(linked==imageViews_.end())return;
    for(const auto view:linked->second)views_.erase(view);
    imageViews_.erase(linked);
}
void VkNrImageFacts::ForgetView(ViewKey view)
{
    const auto found=views_.find(view);if(found==views_.end())return;
    if(const auto linked=imageViews_.find({found->second.device,found->second.image});linked!=imageViews_.end()){
        linked->second.erase(view);
        if(linked->second.empty())imageViews_.erase(linked);
    }
    views_.erase(found);
}
void VkNrImageFacts::Destroyed(VkDevice device,VkImage image)
{
    std::lock_guard lock(mutex_);const auto i=images_.find({device,image});
    if(i==images_.end()||i->second.device!=device)return;
    ForgetImageViews({device,image});
    images_.erase(i);
}
void VkNrImageFacts::ViewDestroyed(VkDevice device,VkImageView view)
{ std::lock_guard lock(mutex_);const auto v=views_.find({device,view});if(v!=views_.end()&&v->second.device==device)ForgetView({device,view}); }
void VkNrImageFacts::DeviceDestroyed(VkDevice device)
{
    std::lock_guard lock(mutex_);
    for(auto i=images_.begin();i!=images_.end();)if(i->second.device==device){ForgetImageViews(i->first);i=images_.erase(i);}else ++i;
}
std::optional<VkNrImageRights> VkNrImageFacts::Rights(VkDevice device,const NVSDK_NGX_Resource_VK& resource,VkImageUsageFlags required,bool depth,const char** unavailable) const
{
    if(unavailable)*unavailable=nullptr;
    const auto reject=[&](const char* reason)->std::optional<VkNrImageRights>{if(unavailable)*unavailable=reason;return {};};
    if(resource.Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)return reject("source is not an image view");
    std::lock_guard lock(mutex_);const auto& w=resource.Resource.ImageViewInfo;
    const auto image=images_.find({device,w.Image});const auto view=views_.find({device,w.ImageView});
    if(image==images_.end())return reject("source image creation was not observed");
    if(view==views_.end())return reject("source image-view creation was not observed");
    const auto& i=image->second;const auto& v=view->second;auto range=v.range;
    if(i.device!=device||v.device!=device||v.image!=w.Image||v.imageGeneration!=i.generation)
        return reject("source device or image-view generation does not match");
    if(!i.chainKnown)return reject("source image extension chain is unsupported");
    if(!v.chainKnown)return reject("source image-view extension chain is unsupported");
    // Transfer operations address VkImage directly. A sampled-only view usage
    // override does not remove the underlying image's transfer permissions.
    const auto viewRequired=required&~(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
    if((i.usage&required)!=required)
        return reject("source image usage lacks the required access");
    if((v.usage.value_or(i.usage)&viewRequired)!=viewRequired)
        return reject("source image-view usage lacks the required access");
    if(!i.viewFormats.empty()&&std::find(i.viewFormats.begin(),i.viewFormats.end(),v.format)==i.viewFormats.end())
        return reject("source view format is absent from the image format list");
    if(i.type!=VK_IMAGE_TYPE_2D)return reject("source image type is not 2D");
    if(i.samples!=VK_SAMPLE_COUNT_1_BIT)return reject("source image is multisampled");
    if(i.tiling!=VK_IMAGE_TILING_OPTIMAL)return reject("source image tiling is not optimal");
    if(i.flags&(VK_IMAGE_CREATE_PROTECTED_BIT|VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_SPARSE_RESIDENCY_BIT))
        return reject("source image is protected or sparse");
    if(v.type!=VK_IMAGE_VIEW_TYPE_2D)return reject("source view type is not 2D");
    if(i.format!=v.format)return reject("source image and view formats differ");
    if(v.format!=w.Format)return reject("wrapper format differs from observed view format");
    if(!i.mips||!i.layers)return reject("source image has no mip levels or array layers");
    // Vulkan remaining-count sentinels describe the same range as explicit
    // counts when resolved against this exact observed image generation.
    if(!ResolveRange(range,i.mips,i.layers))return reject("source view range exceeds observed image subresources");
    auto wrapperRange=w.SubresourceRange;
    if(!ResolveRange(wrapperRange,i.mips,i.layers))return reject("wrapper range exceeds observed image subresources");
    if(range.baseMipLevel)return reject("source view starts at a nonzero mip level");
    if(range.levelCount!=1)return reject("source view mip count does not resolve to one");
    if(range.baseArrayLayer)return reject("source view starts at a nonzero array layer");
    if(range.layerCount!=1)return reject("source view layer count does not resolve to one");
    if(!w.Width||!w.Height)return reject("wrapper extent is empty");
    if(w.Width>i.extent.width||w.Height>i.extent.height)return reject("wrapper extent exceeds observed image extent");
    if(i.extent.depth!=1)return reject("source image extent depth is not one");
    if(wrapperRange.aspectMask!=range.aspectMask||wrapperRange.baseMipLevel!=range.baseMipLevel||
        wrapperRange.levelCount!=range.levelCount||wrapperRange.baseArrayLayer!=range.baseArrayLayer||
        wrapperRange.layerCount!=range.layerCount)
        return reject("wrapper subresource range differs from observed view range");
    if(v.components.r!=VK_COMPONENT_SWIZZLE_IDENTITY&&v.components.r!=VK_COMPONENT_SWIZZLE_R)
        return reject("source view red swizzle is unsupported");
    // Depth is scalar: CopyDepth reads only .x and direct depth-aspect copies
    // preserve that same value. DOOM's legal (R,0,0,1) view must not be rejected
    // for unused channels. Color and motion still require all original channels.
    if(!depth&&v.components.g!=VK_COMPONENT_SWIZZLE_IDENTITY&&v.components.g!=VK_COMPONENT_SWIZZLE_G)
        return reject("source view green swizzle is unsupported");
    if(!depth&&v.components.b!=VK_COMPONENT_SWIZZLE_IDENTITY&&v.components.b!=VK_COMPONENT_SWIZZLE_B)
        return reject("source view blue swizzle is unsupported");
    if(!depth&&v.components.a!=VK_COMPONENT_SWIZZLE_IDENTITY&&v.components.a!=VK_COMPONENT_SWIZZLE_A)
        return reject("source view alpha swizzle is unsupported");
    VkImageAspectFlags aspect=VK_IMAGE_ASPECT_COLOR_BIT,barrier=aspect;
    switch(i.format){
    case VK_FORMAT_D16_UNORM:case VK_FORMAT_X8_D24_UNORM_PACK32:case VK_FORMAT_D32_SFLOAT:
        if(!depth)return reject("depth format supplied for a color or motion input");aspect=barrier=VK_IMAGE_ASPECT_DEPTH_BIT;break;
    case VK_FORMAT_D16_UNORM_S8_UINT:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:
        if(!depth)return reject("depth format supplied for a color or motion input");aspect=VK_IMAGE_ASPECT_DEPTH_BIT;barrier=aspect|VK_IMAGE_ASPECT_STENCIL_BIT;break;
    default:if(depth&&i.format!=VK_FORMAT_R32_SFLOAT&&i.format!=VK_FORMAT_R16_SFLOAT&&i.format!=VK_FORMAT_R16_UNORM)return reject("source depth format is unsupported");
    }
    if(!(range.aspectMask&aspect)||(range.aspectMask&~barrier))return reject("source view aspect is incompatible with guide format");
    auto canonicalView=v;canonicalView.range=range;
    return VkNrImageRights{i,std::move(canonicalView),aspect,barrier};
}
std::optional<VkNrImageRights> VkNrImageFacts::NgxDepthRights(VkDevice device,
    const NVSDK_NGX_Resource_VK& resource,bool authenticatedInput,const char** unavailable) const
{
    if(!authenticatedInput||resource.Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)
        return Rights(device,resource,VK_IMAGE_USAGE_SAMPLED_BIT,true,unavailable);
    auto canonical=resource;auto& image=canonical.Resource.ImageViewInfo;
    const auto& range=image.SubresourceRange;
    const bool genericDefault=range.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&range.baseMipLevel==0&&
        range.levelCount==VK_REMAINING_MIP_LEVELS&&range.baseArrayLayer==0&&range.layerCount==VK_REMAINING_ARRAY_LAYERS;
    bool depthFormat=false;
    switch(image.Format) {
    case VK_FORMAT_D16_UNORM:case VK_FORMAT_X8_D24_UNORM_PACK32:case VK_FORMAT_D32_SFLOAT:
    case VK_FORMAT_D16_UNORM_S8_UINT:case VK_FORMAT_D24_UNORM_S8_UINT:case VK_FORMAT_D32_SFLOAT_S8_UINT:
        depthFormat=true;break;
    default:break;
    }
    // Do not repair arbitrary disagreements. Rights still validates device,
    // generation, format, usage, exact view range and component interpretation.
    if(genericDefault&&depthFormat)image.SubresourceRange.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    return Rights(device,canonical,VK_IMAGE_USAGE_SAMPLED_BIT,true,unavailable);
}
std::string VkNrImageFacts::Describe(VkDevice device,const NVSDK_NGX_Resource_VK& resource) const
{
    if(resource.Type!=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW)return "wrapper is not an image view";
    std::lock_guard lock(mutex_);const auto& w=resource.Resource.ImageViewInfo;
    std::ostringstream out;
    const auto range=[&](const VkImageSubresourceRange& r){out<<r.aspectMask<<','<<r.baseMipLevel<<','<<r.levelCount<<','<<r.baseArrayLayer<<','<<r.layerCount;};
    out<<"wrapper{format="<<w.Format<<" extent="<<w.Width<<'x'<<w.Height<<" range=";range(w.SubresourceRange);out<<'}';
    const auto image=images_.find({device,w.Image});const auto view=views_.find({device,w.ImageView});
    if(image!=images_.end()) {
        const auto& i=image->second;
        out<<" image{deviceMatch="<<(i.device==device)<<" type="<<i.type<<" format="<<i.format
           <<" extent="<<i.extent.width<<'x'<<i.extent.height<<'x'<<i.extent.depth
           <<" mips="<<i.mips<<" layers="<<i.layers<<" samples="<<i.samples<<" tiling="<<i.tiling
           <<" usage="<<i.usage<<" flags="<<i.flags<<" chainKnown="<<i.chainKnown<<'}';
    }else out<<" image{unobserved}";
    if(view!=views_.end()) {
        const auto& v=view->second;
        out<<" view{deviceMatch="<<(v.device==device)<<" imageMatch="<<(v.image==w.Image)
           <<" generationMatch="<<(image!=images_.end()&&v.imageGeneration==image->second.generation)
           <<" type="<<v.type<<" format="<<v.format<<" range=";range(v.range);
        out<<" swizzle="<<v.components.r<<','<<v.components.g<<','<<v.components.b<<','<<v.components.a
           <<" usage=";if(v.usage)out<<*v.usage;else out<<"inherited";
        out<<" chainKnown="<<v.chainKnown<<'}';
    }else out<<" view{unobserved}";
    return out.str();
}
VkNrImageFacts& VulkanNrImageFacts() { static VkNrImageFacts facts;return facts; }
}
