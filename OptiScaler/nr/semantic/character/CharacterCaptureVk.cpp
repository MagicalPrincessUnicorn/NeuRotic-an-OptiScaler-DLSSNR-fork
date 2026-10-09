#include "CharacterCaptureVk.h"
#include <bit>
#include <cstring>

namespace Neurotic::Semantic::Character {
namespace {
unsigned MemoryType(VkPhysicalDevice physical,unsigned bits,VkMemoryPropertyFlags flags){
    VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);
    for(unsigned i=0;i<p.memoryTypeCount;++i)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
    return UINT32_MAX;
}
void Barrier(VkCommandBuffer cmd,VkImage image,VkImageLayout from,VkImageLayout to,
             VkAccessFlags src,VkAccessFlags dst){
    VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.oldLayout=from;b.newLayout=to;
    b.srcAccessMask=src;b.dstAccessMask=dst;b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
    b.image=image;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
    vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
float Half(unsigned short h){
    const unsigned sign=unsigned(h&0x8000)<<16,exponent=(h>>10)&31,mantissa=h&1023;
    if(!exponent)return (sign?-1.f:1.f)*std::ldexp(float(mantissa),-24);
    return std::bit_cast<float>(sign|((exponent==31?255:exponent+112)<<23)|(mantissa<<13));
}
float Pq(float value){
    const double p=std::pow(std::clamp(double(value),0.,1.),32./2523.);
    return float(10000.*std::pow(std::max(p-3424./4096.,0.)/std::max(2413./128.-2392./128.*p,1e-6),16384./2610.)/203.);
}
unsigned char Byte(float value){return static_cast<unsigned char>(std::lround(std::clamp(std::isfinite(value)?value:0.f,0.f,1.f)*255.f));}
}
std::optional<unsigned> CharacterCaptureVk::ColorPolicy(VkFormat f,VkColorSpaceKHR c) noexcept {
    const bool eight=f==VK_FORMAT_R8G8B8A8_UNORM||f==VK_FORMAT_B8G8R8A8_UNORM||f==VK_FORMAT_R8G8B8A8_SRGB||f==VK_FORMAT_B8G8R8A8_SRGB;
    const bool ten=f==VK_FORMAT_A2R10G10B10_UNORM_PACK32||f==VK_FORMAT_A2B10G10R10_UNORM_PACK32;
    if(c==VK_COLOR_SPACE_SRGB_NONLINEAR_KHR&&(eight||ten))return 0u;
    if(c==VK_COLOR_SPACE_HDR10_ST2084_EXT&&ten)return 1u;
    if(c==VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT&&f==VK_FORMAT_R16G16B16A16_SFLOAT)return 2u;
    return {};
}
bool CharacterCaptureVk::Initialize(VkPhysicalDevice physical,VkDevice device,VkFormat format,VkColorSpaceKHR color,VkExtent2D extent){
    // Initialization belongs to the overlay resource-creation boundary, never a
    // live capture. An existing generation must first retire through its owner.
    if(device_||!physical||!device||!extent.width||!extent.height||extent.width>32768||extent.height>32768)return false;
    const auto policy=ColorPolicy(format,color);if(!policy)return false;
    VkFormatProperties properties{};vkGetPhysicalDeviceFormatProperties(physical,format,&properties);
    constexpr auto required=VK_FORMAT_FEATURE_BLIT_SRC_BIT|VK_FORMAT_FEATURE_BLIT_DST_BIT|VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT;
    if((properties.optimalTilingFeatures&required)!=required)return false;
    format_=format;policy_=*policy;source_=extent;device_=device;bytesPerPixel_=policy_==2?8u:4u;
    const auto scale=std::min(1.,960./double(std::max(extent.width,extent.height)));
    target_={std::max(1u,unsigned(extent.width*scale)),std::max(1u,unsigned(extent.height*scale))};
    const VkDeviceSize bytes=VkDeviceSize(target_.width)*target_.height*bytesPerPixel_;
    for(auto& s:slots_){
        VkImageCreateInfo image{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};image.imageType=VK_IMAGE_TYPE_2D;image.format=format;
        image.extent={target_.width,target_.height,1};image.mipLevels=image.arrayLayers=1;image.samples=VK_SAMPLE_COUNT_1_BIT;
        image.tiling=VK_IMAGE_TILING_OPTIMAL;image.usage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        if(vkCreateImage(device,&image,nullptr,&s.image)!=VK_SUCCESS){ReleaseAfterIdle();return false;}
        VkMemoryRequirements needs{};vkGetImageMemoryRequirements(device,s.image,&needs);
        VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=needs.size;
        alloc.memoryTypeIndex=MemoryType(physical,needs.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        if(alloc.memoryTypeIndex==UINT32_MAX||vkAllocateMemory(device,&alloc,nullptr,&s.imageMemory)!=VK_SUCCESS||
           vkBindImageMemory(device,s.image,s.imageMemory,0)!=VK_SUCCESS){ReleaseAfterIdle();return false;}
        VkBufferCreateInfo buffer{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};buffer.size=bytes;buffer.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        if(vkCreateBuffer(device,&buffer,nullptr,&s.buffer)!=VK_SUCCESS){ReleaseAfterIdle();return false;}
        vkGetBufferMemoryRequirements(device,s.buffer,&needs);alloc.allocationSize=needs.size;
        alloc.memoryTypeIndex=MemoryType(physical,needs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if(alloc.memoryTypeIndex==UINT32_MAX||vkAllocateMemory(device,&alloc,nullptr,&s.bufferMemory)!=VK_SUCCESS||
           vkBindBufferMemory(device,s.buffer,s.bufferMemory,0)!=VK_SUCCESS||
           vkMapMemory(device,s.bufferMemory,0,bytes,0,&s.mapped)!=VK_SUCCESS){ReleaseAfterIdle();return false;}
    }
    return true;
}
int CharacterCaptureVk::Record(VkCommandBuffer cmd,VkImage source,CpuFrame metadata,VkImageLayout layout){
    if(!device_||!cmd||!source||metadata.colorPolicy!=policy_||
       (layout!=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR&&layout!=VK_IMAGE_LAYOUT_GENERAL))return -1;
    for(unsigned i=0;i<slots_.size();++i){auto& s=slots_[i];if(s.state.phase!=CapturePhase::Free)continue;
        metadata.width=target_.width;metadata.height=target_.height;metadata.stride=target_.width*4;
        metadata.pixels.clear();s.frame=std::move(metadata);s.state.Record();
        Barrier(cmd,source,layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        Barrier(cmd,s.image,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT);
        VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]={int(source_.width),int(source_.height),1};blit.dstOffsets[1]={int(target_.width),int(target_.height),1};
        vkCmdBlitImage(cmd,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,VK_FILTER_LINEAR);
        Barrier(cmd,source,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,layout,VK_ACCESS_TRANSFER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT);
        Barrier(cmd,s.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_TRANSFER_READ_BIT);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={target_.width,target_.height,1};
        vkCmdCopyImageToBuffer(cmd,s.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,s.buffer,1,&copy);
        VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
        b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=s.buffer;b.size=VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&b,0,nullptr);
        return int(i);
    }
    return -1;
}
void CharacterCaptureVk::Cancel(int index) noexcept {
    if(index>=0&&index<int(slots_.size())){auto& s=slots_[index];if(s.state.Cancel()){s.frame={};s.fence={};}}
}
void CharacterCaptureVk::Submitted(int index,VkFence fence) noexcept {
    if(index>=0&&index<int(slots_.size())&&fence){auto& s=slots_[index];if(s.state.Submit())s.fence=fence;}
}
bool ValidCharacterVkPixels(const CpuFrame& frame) noexcept {
    const auto f=static_cast<VkFormat>(frame.externalVkFormat);
    const auto c=frame.colorPolicy==0?VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
        frame.colorPolicy==1?VK_COLOR_SPACE_HDR10_ST2084_EXT:VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT;
    const auto policy=CharacterCaptureVk::ColorPolicy(f,c);
    const unsigned bpp=frame.colorPolicy==2?8u:4u;
    return frame.externalVkFormat&&policy&&*policy==frame.colorPolicy&&frame.width&&frame.height&&
        frame.width<=960&&frame.height<=960&&frame.stride==frame.width*bpp&&
        frame.pixels.size()==size_t(frame.stride)*frame.height;
}
bool ConvertCharacterVkPixels(CpuFrame& frame){
    if(!ValidCharacterVkPixels(frame))return false;
    const auto format=static_cast<VkFormat>(frame.externalVkFormat);const auto policy=frame.colorPolicy;
    std::vector<unsigned char> pixels(size_t(frame.width)*frame.height*4);
    const auto* src=frame.pixels.data();
    // This runs on the Inspector worker owner. Present only copies the bounded
    // completed native thumbnail; it does no HDR decoding or tone mapping.
    for(size_t i=0;i<size_t(frame.width)*frame.height;++i){
        float r,g,b;
        if(policy==2){unsigned short half[4];std::memcpy(half,src+i*8,8);r=Half(half[0])*80.f/203.f;g=Half(half[1])*80.f/203.f;b=Half(half[2])*80.f/203.f;}
        else if(format==VK_FORMAT_A2B10G10R10_UNORM_PACK32||format==VK_FORMAT_A2R10G10B10_UNORM_PACK32){
            unsigned packed;std::memcpy(&packed,src+i*4,4);r=float(packed&1023)/1023.f;g=float((packed>>10)&1023)/1023.f;b=float((packed>>20)&1023)/1023.f;
            if(format==VK_FORMAT_A2R10G10B10_UNORM_PACK32)std::swap(r,b);
        }else{r=src[i*4]/255.f;g=src[i*4+1]/255.f;b=src[i*4+2]/255.f;if(format==VK_FORMAT_B8G8R8A8_UNORM||format==VK_FORMAT_B8G8R8A8_SRGB)std::swap(r,b);}
        if(policy==1){const float pr=Pq(r),pg=Pq(g),pb=Pq(b);r=1.660491f*pr-.587641f*pg-.072850f*pb;g=-.124550f*pr+1.132900f*pg-.008349f*pb;b=-.018151f*pr-.100579f*pg+1.118730f*pb;}
        if(policy){const auto tone=[](float v){v=std::isfinite(v)?std::max(v,0.f):0.f;return std::pow(v/(1.f+v),1.f/2.2f);};r=tone(r);g=tone(g);b=tone(b);}
        auto* dst=pixels.data()+i*4;dst[0]=Byte(r);dst[1]=Byte(g);dst[2]=Byte(b);dst[3]=255;
    }
    frame.pixels=std::move(pixels);frame.stride=frame.width*4;frame.externalVkFormat=0;return true;
}
std::optional<CpuFrame> CharacterCaptureVk::Poll(){
    if(!device_)return {};
    Slot* newest=nullptr;
    for(auto& s:slots_){
        if(s.state.phase!=CapturePhase::Submitted||!s.fence||vkGetFenceStatus(device_,s.fence)!=VK_SUCCESS)continue;
        s.state.Complete();s.fence={};
        if(!newest||s.frame.key.sequence>newest->frame.key.sequence)newest=&s;
    }
    std::optional<CpuFrame> frame;
    if(newest){
        frame=std::move(newest->frame);frame->externalVkFormat=unsigned(format_);
        frame->stride=target_.width*bytesPerPixel_;frame->pixels.resize(size_t(frame->stride)*target_.height);
        std::memcpy(frame->pixels.data(),newest->mapped,frame->pixels.size());
    }
    for(auto& s:slots_)if(s.state.phase==CapturePhase::Complete){s.state.Release();s.frame={};}
    return frame;
}
void CharacterCaptureVk::ReleaseAfterIdle() noexcept {
    if(device_)for(auto& s:slots_){
        if(s.mapped)vkUnmapMemory(device_,s.bufferMemory);
        if(s.buffer)vkDestroyBuffer(device_,s.buffer,nullptr);
        if(s.bufferMemory)vkFreeMemory(device_,s.bufferMemory,nullptr);
        if(s.image)vkDestroyImage(device_,s.image,nullptr);
        if(s.imageMemory)vkFreeMemory(device_,s.imageMemory,nullptr);
    }
    AbandonDestroyedDevice();
}
void CharacterCaptureVk::AbandonDestroyedDevice() noexcept {for(auto& s:slots_)s={};device_={};format_={};source_={};target_={};policy_=bytesPerPixel_=0;}
}
