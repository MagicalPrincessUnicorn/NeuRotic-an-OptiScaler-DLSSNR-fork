#define NOMINMAX
#include "fixtures/vulkan_nr_gpu_host.h"
#include "../OptiScaler/dlssnr/VulkanNrCapture.h"
#include "../OptiScaler/dlssnr/VulkanNrPixelProbe.h"
#include <DirectXPackedVector.h>
static unsigned logged=0;
template<class... T> void ProbeLog(const char*,const T&...){++logged;}
#define LOG_INFO(...) ProbeLog(__VA_ARGS__)
#include "pixel_probe_actual.inc"
static void Check(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int main(){try{
 using namespace DlssNr;
 VkGpuHost host;
 constexpr std::array<VkFormat,4> formats{VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_B8G8R8A8_UNORM};
 constexpr std::array<VkExtent2D,4> extents{{{32,24},{24,16},{16,12},{8,8}}};
 std::array<VkGpuHost::Image*,4> images{};
 std::array<VkGpuHost::Buffer*,4> uploads{},after{};
 std::array<std::vector<unsigned char>,4> expected;
 const auto usage=VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 for(size_t stage=0;stage<4;++stage){host.extent=extents[stage];images[stage]=&host.MakeImage(formats[stage],usage);
  const auto bpp=PixelProbe::PixelBytes(formats[stage]);const auto count=extents[stage].width*extents[stage].height;
  uploads[stage]=&host.MakeBuffer(count*bpp,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);after[stage]=&host.MakeBuffer(count*bpp,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  auto* bytes=static_cast<unsigned char*>(uploads[stage]->mapped);
  for(uint32_t p=0;p<count;++p){
   if(bpp>=8){const float values[]={float(p%29)-7.5f,float(p%11)*16.f,float(p%17)/16.f,p%2?1.f:0.f};
    if(bpp==16)std::memcpy(bytes+p*bpp,values,16);
    else for(size_t c=0;c<4;++c){const auto value=DirectX::PackedVector::XMConvertFloatToHalf(values[c]);std::memcpy(bytes+p*bpp+c*2,&value,2);}}
   else {bytes[p*4]=static_cast<unsigned char>((p*17)%256);bytes[p*4+1]=static_cast<unsigned char>((p*31)%256);bytes[p*4+2]=static_cast<unsigned char>((p*7)%256);bytes[p*4+3]=p%2?255:0;}
  }
  expected[stage].assign(bytes,bytes+count*bpp);
 }
 // Keep replay rights until explicit reset so publication's second gate is exercised.
 host.Begin(0);VkNrCaptureRequest request;request.use=host.use;request.frame.queueFamily=host.family;request.frame.routeEpoch=1;request.requestedPasses=request.completedPasses=1;
 for(size_t stage=0;stage<4;++stage){host.extent=extents[stage];host.Upload(*uploads[stage],*images[stage]);
  const auto layout=stage%2?VK_IMAGE_LAYOUT_GENERAL:VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;host.Move(*images[stage],layout);
  request.images.push_back({std::to_string(stage),images[stage]->image,formats[stage],layout,extents[stage],0});}
 recording=host.command;device=host.device;physical=host.physical;
 auto invalid=request;invalid.images[2].format=VK_FORMAT_UNDEFINED;
 Check(!CreateProbe(invalid,images[0]->image,images[1]->image,1024*1024).payload,"unsupported format refuses before recording");
 Check(!CreateProbe(request,images[0]->image,images[1]->image,0).payload,"insufficient allocation budget refuses before recording");
 ProbeReadback* readback=nullptr;unsigned released=0;
 VulkanNrCapture owner(VulkanNrRecordings(),[&](const auto& r,VkImage a,VkImage b,uint64_t budget){auto result=CreateProbe(r,a,b,budget);readback=static_cast<ProbeReadback*>(result.payload.get());return result;},
  [&](VkNrCapturePayload& payload,bool alive){Check(alive,"completed probe releases on live device");static_cast<ProbeReadback&>(payload).Dispose(alive);++released;return true;});
 owner.Arm(1);Check(owner.Record(request,images[0]->image,images[1]->image),"actual sparse-copy recorder succeeds");
 Check(readback&&readback->HostVisible()&&readback->bytes<=1024*1024,"actual mapped coherent readback and recorded host barrier");
 owner.PublishCompleted(VulkanNrRecordings());Check(!logged&&!released,"recorded copies cannot publish early");
 // Read each entire source after the production recorder restored its original layout.
 for(size_t stage=0;stage<4;++stage){host.extent=extents[stage];host.Read(*images[stage],*after[stage]);}
 host.Submit(false);
 for(size_t stage=0;stage<4;++stage){const auto bpp=PixelProbe::PixelBytes(formats[stage]);
  Check(std::memcmp(after[stage]->mapped,expected[stage].data(),expected[stage].size())==0,"source remains unchanged and readable after restored layout");
  for(uint32_t sample=0;sample<16;++sample){
   // Independent expected coordinates: cell centers at 1/8,3/8,5/8,7/8.
   const uint32_t x=((2*(sample%4)+1)*extents[stage].width)/8,y=((2*(sample/4)+1)*extents[stage].height)/8;
   const auto* actual=static_cast<const unsigned char*>(readback->mapped)+stage*256+sample*bpp;
   Check(std::memcmp(actual,expected[stage].data()+(y*extents[stage].width+x)*bpp,bpp)==0,"all sixteen actual GPU sample bytes match independent source offsets");
  }
  std::printf("PASS sparse GPU format=%u extent=%ux%u samples=16 unchanged source bytes=%zu\n",unsigned(formats[stage]),extents[stage].width,extents[stage].height,expected[stage].size());
 }
 owner.PublishCompleted(VulkanNrRecordings());Check(!logged&&!released,"completed replayable recording still cannot publish");
 VkGpuCheck(vkResetCommandBuffer(host.command,0),"release recording");VulkanNrRecordings().OnReset(host.command,VK_SUCCESS);
 owner.PublishCompleted(VulkanNrRecordings());Check(logged==6&&released==1&&owner.PrivateBytes()==0,"actual statistics publish only after GPU completion plus recording release");
 Check(host.validationErrors==0,"reported Vulkan validation errors");
 std::printf("PASS sparse GPU submitted=%llu completed=%llu validation=%s logLines=%u; provider not invoked\n",static_cast<unsigned long long>(host.submitted),static_cast<unsigned long long>(host.completed),host.validation?"enabled":"UNAVAILABLE",logged);
 return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL sparse GPU: %s\n",e.what());return 1;}}
