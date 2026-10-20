#define NOMINMAX
#include "fixtures/vulkan_nr_gpu_host.h"
#include "../OptiScaler/shaders/dlssnr/DlssNr_Vk.h"
#include "../OptiScaler/dlssnr/VulkanNrNativeOutput.h"
#include "../OptiScaler/dlssnr/VulkanNrExposureScan.h"
#include <DirectXPackedVector.h>
#include <cmath>
static void Check(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
static void CheckHdrNormalization(VkGpuHost& h){
 using namespace DlssNr;
 const VkExtent2D sceneExtent{3840,2160},meterExtent{kDlssNrMeterGrid,kDlssNrMeterGrid};
 h.extent=sceneExtent;const unsigned pixels=h.extent.width*h.extent.height,bytes=pixels*8;
 const unsigned meterPixels=meterExtent.width*meterExtent.height;
 const auto usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 auto& scene=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 auto& oldProxy=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 auto& proxy=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 auto& keep=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 h.extent=meterExtent;auto& meter=h.MakeImage(VK_FORMAT_R32_SFLOAT,usage);h.extent=sceneExtent;
 auto& composed=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 auto& externalTarget=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);
 auto& upload=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
 auto& oldRead=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 auto& proxyRead=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 auto& meterRead=h.MakeBuffer(meterPixels*4,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 auto& composedRead=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 auto& targetRead=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
 auto* original=static_cast<DirectX::PackedVector::HALF*>(upload.mapped);
 for(unsigned i=0;i<pixels;++i)for(unsigned c=0;c<4;++c){
  const bool emissive=i%sceneExtent.width>=sceneExtent.width-60&&i/sceneExtent.width>=sceneExtent.height-34;
  original[i*4+c]=DirectX::PackedVector::XMConvertFloatToHalf(c==3?.75f:emissive?6000.f:((i+i/64)%2?24.f:16.f));
 }
 DlssNr_Vk shader("Native HDR normalization fixture",h.device,h.physical,h.storageWriteWithoutFormatEnabled);
 Check(shader.CanRender(),"actual HDR composition pipeline");
 DlssNrConstants constants{};constants.Mode=DlssNrMode_Encode;constants.Width=sceneExtent.width;constants.Height=sceneExtent.height;
 constants.WhitePoint=1;constants.TransferStrength=1;constants.ColourStrength=1;constants.MaxRatio=2;constants.ApplyModel=1;
 VkNrFrameContract context{};context.deviceGeneration=VkNrCompletionDeviceGeneration(h.device);context.output=context.work=h.extent;
 VkNrAutomaticWhitePoint automatic;automatic.Configure(context);
 for(unsigned sample=0;sample<8;++sample){
  h.Begin();auto slots=shader.Reserve(h.use,sample==0?2:1);Check(bool(slots),"calibration reservation");
  if(sample==0){
   h.Upload(upload,scene);h.Move(scene,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
   h.Move(oldProxy,VK_IMAGE_LAYOUT_GENERAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);
   Check(shader.Dispatch(h.command,h.use,slots->slots[0],constants,sceneExtent.width,sceneExtent.height,scene.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,oldProxy.view,keep.view),"unscaled HDR encode");
   h.Read(oldProxy,oldRead);h.Move(keep,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  }
  h.Move(meter,VK_IMAGE_LAYOUT_GENERAL);auto measure=constants;measure.Mode=DlssNrMode_Calibrate;
  measure.Width=meterExtent.width;measure.Height=meterExtent.height;
  Check(shader.Dispatch(h.command,h.use,slots->slots[sample==0?1:0],measure,meterExtent.width,meterExtent.height,keep.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,meter.view,VK_NULL_HANDLE),"actual untouched full-resolution scene into independent meter grid");
  h.extent=meterExtent;h.Read(meter,meterRead);h.extent=sceneExtent;h.Submit();
  const bool admitted=automatic.ObserveCompleted(context,h.use.value,{static_cast<const float*>(meterRead.mapped),meterPixels});
  Check(admitted==(sample==7),"normalization requires eight actual completed grid samples");
 }
 Check(automatic.Value()&&std::abs(*automatic.Value()-24.f)<.01f,"measured scene percentile");
 Check(automatic.LastObservation().peak>5900&&automatic.LastObservation().lit==meterPixels,
  "real 4K shader meter tolerates sparse emissive peaks while retaining broad scene occupancy");
 ExposureGuard::WhitePointHold exposure;constants.WhitePoint=ResolveVkNrWhitePoint(1,1,0,1,1,{},exposure,automatic.Value());
 h.Begin();auto slots=shader.Reserve(h.use,3);Check(bool(slots),"normalized encode resolve and destination reservations");
 h.Move(proxy,VK_IMAGE_LAYOUT_GENERAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);
 Check(shader.Dispatch(h.command,h.use,slots->slots[0],constants,sceneExtent.width,sceneExtent.height,scene.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,proxy.view,keep.view),"normalized HDR encode");
 h.Read(proxy,proxyRead);h.Move(proxy,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(keep,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(composed,VK_IMAGE_LAYOUT_GENERAL);
 auto resolve=constants;resolve.Mode=DlssNrMode_Resolve;
 // Identity model answer isolates the color bridge; no proprietary model quality is claimed.
 Check(shader.Dispatch(h.command,h.use,slots->slots[1],resolve,sceneExtent.width,sceneExtent.height,proxy.view,proxy.view,keep.view,VK_NULL_HANDLE,composed.view,VK_NULL_HANDLE),"identity HDR resolve");
 h.Read(composed,composedRead);h.Move(composed,VK_IMAGE_LAYOUT_GENERAL);h.Move(externalTarget,VK_IMAGE_LAYOUT_GENERAL);
 auto display=constants;display.Mode=DlssNrMode_Present;
 Check(shader.Dispatch(h.command,h.use,slots->slots[2],display,sceneExtent.width,sceneExtent.height,composed.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,externalTarget.view,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_GENERAL),"external NGX-shaped target write");
 PublishVkNrNativeOutput(h.command,externalTarget.image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1});
 h.Read(externalTarget,targetRead);h.Submit();
 const auto unpack=[](const void* p,unsigned index){return DirectX::PackedVector::XMConvertHalfToFloat(static_cast<const DirectX::PackedVector::HALF*>(p)[index]);};
 const float oldContrast=std::abs(unpack(oldRead.mapped,0)-unpack(oldRead.mapped,4));
 const float contrast=std::abs(unpack(proxyRead.mapped,0)-unpack(proxyRead.mapped,4));
 Check(oldContrast<.001f,"missing exposure reproduces fp16 proxy detail collapse");
 Check(contrast>.05f,"derived normalization restores real shader proxy contrast");
 for(unsigned i=0;i<pixels*4;++i){
  const float encoded=unpack(proxyRead.mapped,i),delivered=unpack(targetRead.mapped,i),input=unpack(upload.mapped,i);
  Check(std::isfinite(encoded)&&encoded>0&&(input>1000?encoded<=1:encoded<1),
   "normalized body detail stays unclipped; extreme emissive proxy may saturate");
  Check(std::isfinite(delivered)&&std::abs(delivered-input)<std::max(.063f,std::abs(input)*.001f),"HDR identity bridge preserves scene light and alpha including emissive peaks");
 }
 Check(std::memcmp(composedRead.mapped,targetRead.mapped,bytes)==0,"external target exactly matches private composition");
 std::printf("PASS actual shader 3840x2160 HDR with sparse6000 emissive peaks to 64x64 calibration whitePoint=%.6f body contrast %.6f -> %.6f; external destination exact, HDR finite; emissive proxy saturation allowed\n",constants.WhitePoint,oldContrast,contrast);
}
int main(){try{
 VkGpuHost h;
 CheckHdrNormalization(h);
 h.extent={32,8};
 for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT}){
  const auto usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  auto& composed=h.MakeImage(format,usage);auto& target=h.MakeImage(format,usage);
  const auto bytes=h.extent.width*h.extent.height*(format==VK_FORMAT_R16G16B16A16_SFLOAT?8:4);
  auto& upload=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);auto& read=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  if(format==VK_FORMAT_R16G16B16A16_SFLOAT){auto* p=static_cast<DirectX::PackedVector::HALF*>(upload.mapped);for(unsigned i=0;i<bytes/2;++i)p[i]=DirectX::PackedVector::XMConvertFloatToHalf(i%4==3?0.75f:(float(i%33)-8.f)/4.f);}
  else {auto* p=static_cast<unsigned char*>(upload.mapped);for(unsigned i=0;i<bytes;++i)p[i]=static_cast<unsigned char>((i*37+7)%256);}
  DlssNr_Vk shader("final Native output fixture",h.device,h.physical,h.storageWriteWithoutFormatEnabled);
  Check(shader.CanRender(),"actual composition pipeline available");
  h.Begin();h.Upload(upload,composed);h.Move(composed,VK_IMAGE_LAYOUT_GENERAL);h.Move(target,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  const VkImageSubresourceRange range{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkClearColorValue black{};
  vkCmdClearColorImage(h.command,target.image,target.layout,&black,1,&range);h.Move(target,VK_IMAGE_LAYOUT_GENERAL);
  auto slots=shader.Reserve(h.use,1);Check(bool(slots),"final mode 6 reservation");
  DlssNrConstants display{};display.Mode=DlssNrMode_Present;display.Width=h.extent.width;display.Height=h.extent.height;
  Check(shader.Dispatch(h.command,h.use,slots->slots[0],display,h.extent.width,h.extent.height,composed.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,target.view,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_GENERAL),"actual final mode 6 dispatch");
  DlssNr::PublishVkNrNativeOutput(h.command,target.image,range);
  // Deliberately no fixture image barrier before the transfer: the product export must cover it.
  auto region=h.Region();vkCmdCopyImageToBuffer(h.command,target.image,VK_IMAGE_LAYOUT_GENERAL,read.buffer,1,&region);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.buffer=read.buffer;host.size=read.bytes;
  vkCmdPipelineBarrier(h.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
  h.Submit();Check(std::memcmp(upload.mapped,read.mapped,bytes)==0,"actual target bytes equal composed input including signed RGB and alpha");
  std::printf("PASS GPU final mode 6 format=%u target bytes match composed RGB/alpha through GENERAL transfer consumer\n",unsigned(format));
 }
 Check(h.validationErrors==0,"Vulkan validation errors");Check(h.submitted==h.completed,"all output work actually completed");
 return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL GPU %s\n",e.what());return 1;}}
