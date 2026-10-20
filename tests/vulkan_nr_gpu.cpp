#define NOMINMAX
#include "fixtures/vulkan_nr_gpu_composition.h"
#include "../OptiScaler/shaders/format_transfer/PresentColor_Vk.h"
#include <DirectXPackedVector.h>
#include <cmath>
#include <algorithm>
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
// Independent CPU oracle from the committed flagship's documented PQ matrices.
static double PqDecode(double value){const double p=std::pow(std::clamp(value,0.0,1.0),32.0/2523.0);return 125.0*std::pow(std::max(p-3424.0/4096.0,0.0)/(2413.0/128.0-2392.0/128.0*p),16384.0/2610.0);}
static double PqEncode(double value){const double p=std::pow(std::clamp(value/125.0,0.0,1.0),2610.0/16384.0);return std::pow((3424.0/4096.0+2413.0/128.0*p)/(1.0+2392.0/128.0*p),2523.0/32.0);}
int main(){try{
    VkGpuHost host;QualifyCompositionGpu(host);QualifySecondaryGpu(host);QualifyGenerationGpu(host);DlssNr::PresentColor_Vk color(host.device,host.extent);Check(color.IsInit(),"actual product color pipelines");
    const auto pixels=host.extent.width*host.extent.height;
    for(auto format:{VK_FORMAT_A2B10G10R10_UNORM_PACK32,VK_FORMAT_A2R10G10B10_UNORM_PACK32}){
        auto& source=host.MakeImage(format,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);
        auto& linear=host.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto& encoded=host.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto& final=host.MakeImage(format,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
        auto& upload=host.MakeBuffer(pixels*4,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);auto& read=host.MakeBuffer(pixels*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto& decoded=host.MakeBuffer(pixels*8,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
        auto* original=static_cast<uint32_t*>(upload.mapped);
        for(uint32_t i=0;i<pixels;++i){const auto white=static_cast<uint32_t>(std::round(PqEncode(2.5)*1023));const auto r=i%4==0?white:(i*37)%1024,g=i%4==0?white:(i*11)%1024,b=i%4==0?white:(i*19)%1024;
            original[i]=format==VK_FORMAT_A2B10G10R10_UNORM_PACK32?r|(g<<10)|(b<<20)|((i%4)<<30):b|(g<<10)|(r<<20)|((i%4)<<30);}
        host.Begin();auto slots=color.Reserve(host.use);Check(slots.has_value(),"owned immutable color slots");
        host.Upload(upload,source);host.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);host.Move(linear,VK_IMAGE_LAYOUT_GENERAL);
        DlssNr::VkNrRepresentation representation{format,VK_COLOR_SPACE_HDR10_ST2084_EXT,2};
        Check(color.Decode(host.command,*slots,source.view,linear.view,representation),"record actual PQ decoder");
        host.Read(linear,decoded);host.Move(linear,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);host.Move(encoded,VK_IMAGE_LAYOUT_GENERAL);
        Check(color.Encode(host.command,*slots,source.view,linear.view,encoded.view,representation),"record actual inverse with original correction");
        host.Move(encoded,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL);host.Move(final,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        VkImageBlit blit{};blit.srcSubresource=blit.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};
        blit.srcOffsets[1]=blit.dstOffsets[1]={static_cast<int32_t>(host.extent.width),static_cast<int32_t>(host.extent.height),1};
        vkCmdBlitImage(host.command,encoded.image,encoded.layout,final.image,final.layout,1,&blit,VK_FILTER_NEAREST);
        host.Read(final,read);host.Submit();
        Check(std::memcmp(original,read.mapped,pixels*4)==0,"packed PQ identity and alpha must match original bit for bit");
        const double matrix[3][3]={{1.660491002108434,-0.587641138788550,-0.072849863319884},{-0.124550474521591,1.132899897125960,-0.008349422604369},{-0.018150763354905,-0.100578898008007,1.118729661362913}};
        const auto* values=static_cast<const DirectX::PackedVector::HALF*>(decoded.mapped);bool negative=false;
        for(uint32_t i=0;i<pixels;++i){double channels[3]={double(original[i]&1023)/1023.0,double((original[i]>>10)&1023)/1023.0,double((original[i]>>20)&1023)/1023.0};if(format==VK_FORMAT_A2R10G10B10_UNORM_PACK32)std::swap(channels[0],channels[2]);
            for(int c=0;c<3;++c){double expected=0;for(int k=0;k<3;++k)expected+=matrix[c][k]*PqDecode(channels[k]);const float actual=DirectX::PackedVector::XMConvertHalfToFloat(values[i*4+c]);
                Check(std::isfinite(actual)&&std::abs(actual-expected)<=std::max(0.002*std::abs(expected),0.001),"actual decode pixels vs committed donor oracle");negative|=actual<0;}}
        Check(negative,"negative wide-gamut values must survive the private signed carrier");
        std::printf("PASS GPU PQ format=%u identity/alpha/signed pixels use=%llu\n",static_cast<unsigned>(format),static_cast<unsigned long long>(host.use.value));
    }
    Check(host.validationErrors==0,"actual Vulkan validation errors");Check(host.submitted==host.completed,"every submitted use has actual completion");
    std::printf("PASS GPU submitted=%llu completed=%llu validationErrors=%u\n",static_cast<unsigned long long>(host.submitted),static_cast<unsigned long long>(host.completed),host.validationErrors.load());return 0;
}catch(const VkGpuUnavailable& e){std::printf("UNAVAILABLE GPU: %s\n",e.what());return 2;}catch(const std::exception& e){std::fprintf(stderr,"FAIL GPU: %s\n",e.what());return 1;}}
