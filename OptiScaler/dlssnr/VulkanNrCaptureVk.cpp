#include "pch.h"
#include "VulkanNrCaptureVk.h"
#include "DlssNrFeature_Vk.h"
#include "NrConfigSnapshot.h"
#include <Config.h>
#include "NrScreenshotPng.h"
#include "NrScreenshotContract.h"
#include "VulkanNrPixelProbe.h"
#include <resource.h>
#include <Util.h>
#include <fstream>
namespace DlssNr {
namespace {
std::mutex captureMutex;VkCommandBuffer recording=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;
bool stagesRequested=false;ULONGLONG captureAfter=0;uint32_t samplesRemaining=0,requestedRoute=0;bool requestedEnabled=true;bool saveOriginal=true,saveFinal=true;
DXGI_FORMAT PreviewFormat(VkFormat f){switch(f){
 case VK_FORMAT_R16G16B16A16_SFLOAT:return DXGI_FORMAT_R16G16B16A16_FLOAT;
 case VK_FORMAT_R32G32B32A32_SFLOAT:return DXGI_FORMAT_R32G32B32A32_FLOAT;
 case VK_FORMAT_R8G8B8A8_UNORM:case VK_FORMAT_R8G8B8A8_SRGB:return DXGI_FORMAT_R8G8B8A8_UNORM;
 case VK_FORMAT_B8G8R8A8_UNORM:case VK_FORMAT_B8G8R8A8_SRGB:return DXGI_FORMAT_B8G8R8A8_UNORM;
 default:return DXGI_FORMAT_UNKNOWN;}}
struct Readbacks final : VkNrCapturePayload {
 VkDevice device=VK_NULL_HANDLE;std::vector<VkBuffer> buffers;std::vector<VkDeviceMemory> memory;std::vector<void*> mapped;
 bool barrierRecorded=false;
 bool HostVisible() const override{return barrierRecorded&&!mapped.empty()&&std::all_of(mapped.begin(),mapped.end(),[](auto p){return p!=nullptr;});}
 bool Publish(const VkNrCaptureRequest& r) override {
  try {
   auto directory=Util::DllPath().remove_filename()/"NeuroticScreenshots"/("Vulkan-"+std::to_string(GetTickCount64())+"-use"+std::to_string(r.use.value));
   std::filesystem::create_directories(directory);
   std::string manifest="{\"schema\":2,\"backend\":\"Vulkan\",\"boundary\":\"completed GPU readback; not scanout\",\"use\":"+std::to_string(r.use.value)+
    ",\"deviceGeneration\":"+std::to_string(r.frame.deviceGeneration)+",\"routeEpoch\":"+std::to_string(r.frame.routeEpoch)+
    ",\"recipeRevision\":"+std::to_string(r.frame.representation.recipeRevision)+",\"format\":"+std::to_string(r.frame.representation.format)+
    ",\"colorSpace\":"+std::to_string(r.frame.representation.colorSpace)+",\"requestedPasses\":"+std::to_string(r.requestedPasses)+
    ",\"completedPasses\":"+std::to_string(r.completedPasses)+",\"build\":"+Screenshots::JsonString(r.buildIdentity)+
    ",\"settings\":"+Screenshots::JsonString(r.settings)+",\"preview\":\"SDR preview using fixed captured white point; alpha omitted\",\"images\":[";
   bool ok=true;
   for(size_t i=0;i<r.images.size();++i){const auto& image=r.images[i];auto format=PreviewFormat(image.format);auto pitch=Screenshots::PixelBytes(format)*image.extent.width;
    if(!r.stages&&((i==0&&!r.saveOriginal)||(i==1&&!r.saveFinal)))continue;
    auto png=directory/(image.name+".png");
    const bool written=Screenshots::WritePng(png,static_cast<const unsigned char*>(mapped[i]),image.extent.width,image.extent.height,pitch,format,image.whitePoint);
    if(manifest.back()!='[')manifest+=",";manifest+="{\"name\":"+Screenshots::JsonString(image.name)+",\"readbackFormat\":"+std::to_string(image.format)+
     ",\"width\":"+std::to_string(image.extent.width)+",\"height\":"+std::to_string(image.extent.height)+",\"whitePoint\":"+std::to_string(image.whitePoint)+"}";
    ok=written&&ok;
    if(r.stages){std::ofstream raw(directory/(image.name+".bin"),std::ios::binary);raw.write(static_cast<const char*>(mapped[i]),std::streamsize(uint64_t(pitch)*image.extent.height));ok=bool(raw)&&ok;}
   }
   manifest+="]}";for(size_t i=0;i<r.images.size();++i)if(r.stages||(i==0?r.saveOriginal:r.saveFinal))ok=Screenshots::EmbedPngManifest(directory/(r.images[i].name+".png"),manifest)&&ok;
   std::ofstream receipt(directory/"manifest.json",std::ios::binary);receipt<<manifest;return ok&&bool(receipt);
  }catch(...){return false;}
 }
};
bool Release(VkNrCapturePayload& payload,bool alive){auto& r=static_cast<Readbacks&>(payload);
 if(alive)for(size_t i=0;i<r.buffers.size();++i){if(r.mapped[i])vkUnmapMemory(r.device,r.memory[i]);
  if(r.buffers[i])vkDestroyBuffer(r.device,r.buffers[i],nullptr);if(r.memory[i])vkFreeMemory(r.device,r.memory[i],nullptr);}
 r.buffers.clear();r.memory.clear();r.mapped.clear();return true;
}
VkNrCaptureCreation Create(const VkNrCaptureRequest& r,VkImage original,VkImage final,uint64_t budget){
 if(r.images.size()<2||r.images.size()>6||r.images.front().image!=original||r.images[1].image!=final)
  return {{},false,"Capture private stage identities unavailable"};
 auto out=std::make_unique<Readbacks>();out->device=device;out->buffers.resize(r.images.size());out->memory.resize(r.images.size());out->mapped.resize(r.images.size());
 for(size_t i=0;i<r.images.size();++i){const auto& image=r.images[i];auto format=PreviewFormat(image.format);const auto bpp=Screenshots::PixelBytes(format);
  auto fail=[&]{Release(*out,true);return VkNrCaptureCreation{{},false,"Capture readback format, memory or actual capacity unavailable"};};
  if(!image.image||!bpp||!image.extent.width||!image.extent.height||image.extent.width>8192||image.extent.height>8192||image.layout==VK_IMAGE_LAYOUT_UNDEFINED)return fail();
  VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=uint64_t(image.extent.width)*image.extent.height*bpp;info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
  if(vkCreateBuffer(device,&info,nullptr,&out->buffers[i])!=VK_SUCCESS)return fail();VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,out->buffers[i],&req);
  if(req.size>budget-out->bytes)return fail();VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);uint32_t type=UINT32_MAX;
  for(uint32_t k=0;k<props.memoryTypeCount;++k)if((req.memoryTypeBits&(1u<<k))&&
   (props.memoryTypes[k].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=k;break;}
  if(type==UINT32_MAX)return fail();VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
  if(vkAllocateMemory(device,&alloc,nullptr,&out->memory[i])!=VK_SUCCESS||vkBindBufferMemory(device,out->buffers[i],out->memory[i],0)!=VK_SUCCESS||
   vkMapMemory(device,out->memory[i],0,info.size,0,&out->mapped[i])!=VK_SUCCESS)return fail();out->bytes+=req.size;
 }
 // Sources are generation-owned private images from the recorder. Foreign game images never enter this adapter.
 for(size_t i=0;i<r.images.size();++i){const auto& image=r.images[i];VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;
  b.oldLayout=image.layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={image.extent.width,image.extent.height,1};
  vkCmdCopyImageToBuffer(recording,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out->buffers[i],1,&copy);
  std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
  VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=out->buffers[i];host.size=VK_WHOLE_SIZE;
  host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
  vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
 }
 out->barrierRecorded=true;return {std::move(out),true,{}};
}
VulkanNrCapture& Owner(){static VulkanNrCapture capture(VulkanNrRecordings(),Create,Release);return capture;}
// A second owner keeps automatic log diagnostics independent of screenshot requests.
// Four images x sixteen pixels x at most sixteen bytes: one 1 KiB readback buffer.
constexpr VkDeviceSize ProbeImageStride=PixelProbe::SampleCount*16,ProbeBufferSize=4*ProbeImageStride;
PixelProbe::Budget probeBudget;
struct ProbeReadback final : VkNrCapturePayload {
 VkDevice device=VK_NULL_HANDLE;VkBuffer buffer=VK_NULL_HANDLE;VkDeviceMemory memory=VK_NULL_HANDLE;void* mapped=nullptr;
 bool barrierRecorded=false;
 void Dispose(bool alive) noexcept {
  if(alive){if(mapped)vkUnmapMemory(device,memory);if(buffer)vkDestroyBuffer(device,buffer,nullptr);if(memory)vkFreeMemory(device,memory,nullptr);}
  mapped=nullptr;buffer=VK_NULL_HANDLE;memory=VK_NULL_HANDLE;
 }
 ~ProbeReadback() override{Dispose(true);}
 bool HostVisible() const override{return barrierRecorded&&mapped;}
 bool Publish(const VkNrCaptureRequest& r) override {
  try {
   std::array<std::array<PixelProbe::Pixel,PixelProbe::SampleCount>,4> pixels{};
   std::array<PixelProbe::Summary,4> summaries{};
   for(size_t image=0;image<4;++image)for(uint32_t sample=0;sample<PixelProbe::SampleCount;++sample){
    const auto value=PixelProbe::Decode(r.images[image].format,static_cast<const unsigned char*>(mapped)+image*ProbeImageStride+sample*PixelProbe::PixelBytes(r.images[image].format));
    if(!value)return false;pixels[image][sample]=*value;summaries[image].Add(*value);
   }
   PixelProbe::Difference model,composed;
   for(uint32_t sample=0;sample<PixelProbe::SampleCount;++sample){model.Add(pixels[2][sample],pixels[3][sample]);composed.Add(pixels[0][sample],pixels[1][sample]);}
   LOG_INFO("Vulkan NR pixel probe: use={} route={} placement={} resume={} routeEpoch={} invocation={} work={}x{} output={}x{} passes={}/{} metadata=[{}] sampling=4x4-normalized-nearest storage-domain; sparse samples are not whole-image or scanout proof",
    r.use.value,static_cast<unsigned>(r.frame.route),static_cast<unsigned>(r.frame.placement),r.frame.resumeGeneration,r.frame.routeEpoch,r.frame.evaluation.invocation,
    r.frame.work.width,r.frame.work.height,r.frame.output.width,r.frame.output.height,r.completedPasses,r.requestedPasses,r.settings);
   for(size_t i=0;i<4;++i){const auto& s=summaries[i];LOG_INFO("Vulkan NR pixel probe: use={} stage={} format={} extent={}x{} rgbMin={} rgbMax={} alphaMin={} alphaMax={} finiteRGB={}/48 finiteAlpha={}/16",
    r.use.value,r.images[i].name,static_cast<unsigned>(r.images[i].format),r.images[i].extent.width,r.images[i].extent.height,
    s.rgbMin,s.rgbMax,s.alphaMin,s.alphaMax,s.finiteRgb,s.finiteAlpha);}
   LOG_INFO("Vulkan NR pixel probe: use={} proxyModelRgbAbsMean={} proxyModelRgbAbsMax={} finitePairs={}/48 originalComposedRgbAbsMean={} originalComposedRgbAbsMax={} finitePairs={}/48; differing rasters include resampling differences",
    r.use.value,model.Mean(),model.maximum,model.finiteChannels,composed.Mean(),composed.maximum,composed.finiteChannels);
   return true;
  }catch(...){return false;}
 }
};
VkNrCaptureCreation CreateProbe(const VkNrCaptureRequest& r,VkImage original,VkImage final,uint64_t budget){
 if(r.images.size()!=4||r.images[0].image!=original||r.images[1].image!=final)return {{},false,"Pixel probe requires four private stage images"};
 for(const auto& image:r.images)if(!image.image||!PixelProbe::PixelBytes(image.format)||!image.extent.width||!image.extent.height||
  image.extent.width>8192||image.extent.height>8192||image.layout==VK_IMAGE_LAYOUT_UNDEFINED)return {{},false,"Pixel probe image format or extent unavailable"};
 uint32_t familyCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&familyCount,nullptr);
 if(r.frame.queueFamily>=familyCount)return {{},false,"Pixel probe recording family unavailable"};
 std::vector<VkQueueFamilyProperties> families(familyCount);vkGetPhysicalDeviceQueueFamilyProperties(physical,&familyCount,families.data());
 const auto granularity=families[r.frame.queueFamily].minImageTransferGranularity;
 if(granularity.width!=1||granularity.height!=1||granularity.depth!=1)return {{},false,"Pixel probe queue requires larger image-copy regions"};
 auto out=std::make_unique<ProbeReadback>();out->device=device;
 VkBufferCreateInfo info{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};info.size=ProbeBufferSize;info.usage=VK_BUFFER_USAGE_TRANSFER_DST_BIT;info.sharingMode=VK_SHARING_MODE_EXCLUSIVE;
 if(vkCreateBuffer(device,&info,nullptr,&out->buffer)!=VK_SUCCESS)return {{},false,"Pixel probe buffer allocation failed"};
 VkMemoryRequirements req{};vkGetBufferMemoryRequirements(device,out->buffer,&req);
 if(req.size>std::min(budget,1024ull*1024))return {{},false,"Pixel probe memory cap exceeded"};
 VkPhysicalDeviceMemoryProperties props{};vkGetPhysicalDeviceMemoryProperties(physical,&props);uint32_t type=UINT32_MAX;
 for(uint32_t k=0;k<props.memoryTypeCount;++k)if((req.memoryTypeBits&(1u<<k))&&
  (props.memoryTypes[k].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=k;break;}
 if(type==UINT32_MAX)return {{},false,"Pixel probe coherent readback unavailable"};
 VkMemoryAllocateInfo alloc{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};alloc.allocationSize=req.size;alloc.memoryTypeIndex=type;
 if(vkAllocateMemory(device,&alloc,nullptr,&out->memory)!=VK_SUCCESS||vkBindBufferMemory(device,out->buffer,out->memory,0)!=VK_SUCCESS||
  vkMapMemory(device,out->memory,0,ProbeBufferSize,0,&out->mapped)!=VK_SUCCESS)return {{},false,"Pixel probe memory allocation or mapping failed"};
 out->bytes=req.size;
 // All potentially failing host preparation precedes the first recorded copy.
 for(size_t i=0;i<4;++i){const auto& image=r.images[i];VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.image=image.image;
  b.oldLayout=image.layout;b.newLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;b.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
  b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;b.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
  vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&b);
  std::array<VkBufferImageCopy,PixelProbe::SampleCount> copies{};
  for(uint32_t sample=0;sample<PixelProbe::SampleCount;++sample){auto& copy=copies[sample];copy.bufferOffset=i*ProbeImageStride+sample*PixelProbe::PixelBytes(image.format);
   copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1,1,1};
   copy.imageOffset={static_cast<int32_t>(PixelProbe::Coordinate(image.extent.width,sample%PixelProbe::GridSide)),
    static_cast<int32_t>(PixelProbe::Coordinate(image.extent.height,sample/PixelProbe::GridSide)),0};}
  vkCmdCopyImageToBuffer(recording,image.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,out->buffer,PixelProbe::SampleCount,copies.data());
  std::swap(b.oldLayout,b.newLayout);b.srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;
  vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
 }
 VkBufferMemoryBarrier host{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};host.buffer=out->buffer;host.size=VK_WHOLE_SIZE;
 host.srcQueueFamilyIndex=host.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;host.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;host.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
 vkCmdPipelineBarrier(recording,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,0,nullptr,1,&host,0,nullptr);
 out->barrierRecorded=true;return {std::move(out),true,{}};
}
VulkanNrCapture& ProbeOwner(){static VulkanNrCapture capture(VulkanNrRecordings(),CreateProbe,[](VkNrCapturePayload& payload,bool alive){static_cast<ProbeReadback&>(payload).Dispose(alive);return true;});return capture;}
}
void RequestVulkanNrCapture(bool stages){bool originalSelected,nativeSelected,presentSelected;{NrConfigSynchronization::Transaction transaction;originalSelected=Config::Instance()->ScreenshotNrOff.value_or_default();nativeSelected=Config::Instance()->ScreenshotNativeNr.value_or_default();presentSelected=Config::Instance()->ScreenshotPresentNr.value_or_default();}auto capabilities=CurrentVulkanNrCapabilities();auto cfg=TryNrConfigSnapshot(*Config::Instance());std::lock_guard lock(captureMutex);
 if(!capabilities.capture.available||!cfg){Owner().CancelNew(capabilities.capture.reason.empty()?"Vulkan configuration unavailable":capabilities.capture.reason);return;}
 if(!Owner().Busy()&&!samplesRemaining){stagesRequested=stages;samplesRemaining=stages?8:1;requestedRoute=cfg->DlssNrRoute.value_or_default();requestedEnabled=cfg->GetDlssNrRuntimeSnapshot().enabled;
 saveOriginal=originalSelected;saveFinal=requestedRoute?presentSelected:nativeSelected;
 if(!stages&&!saveOriginal&&!saveFinal){samplesRemaining=0;Owner().CancelNew("Select at least one Vulkan comparison image");return;}
 captureAfter=GetTickCount64()+5000;Owner().Arm(0);}}
void VulkanNrCaptureWaiting(const std::string& reason){std::lock_guard lock(captureMutex);Owner().Waiting(reason);}
void CancelVulkanNrCapture(){std::lock_guard lock(captureMutex);samplesRemaining=0;Owner().CancelNew("Vulkan capture cancelled; recorded readbacks remain owned");}
bool WantsVulkanNrCapture(){std::lock_guard lock(captureMutex);return samplesRemaining&&Owner().WantsFrame()&&GetTickCount64()>=captureAfter;}
bool VulkanNrCaptureBusy(){std::lock_guard lock(captureMutex);return samplesRemaining||Owner().Busy();}
void PollVulkanNrCaptures(){auto cfg=TryNrConfigSnapshot(*Config::Instance());std::lock_guard lock(captureMutex);Owner().PublishCompleted(VulkanNrRecordings());
 ProbeOwner().PublishCompleted(VulkanNrRecordings());
 if(samplesRemaining&&(!cfg||cfg->GetDlssNrRuntimeSnapshot().enabled!=requestedEnabled||cfg->DlssNrRoute.value_or_default()!=requestedRoute||GetTickCount64()>captureAfter+30000)){
 samplesRemaining=0;Owner().CancelNew("Vulkan capture stopped after route/enable change or no ready frame within 30 seconds");}
 if(samplesRemaining&&!Owner().Busy())Owner().Arm(0);
}
std::string VulkanNrCaptureStatus(){PollVulkanNrCaptures();std::lock_guard lock(captureMutex);return Owner().Status()+(samplesRemaining?"; "+std::to_string(samplesRemaining)+" sample(s) remaining":"");}
void RecordVulkanNrCapture(const VkNrCaptureRequest& request,VkCommandBuffer cb,VkPhysicalDevice gpu,VkDevice d){std::lock_guard lock(captureMutex);
 if(!samplesRemaining||!Owner().WantsFrame()||GetTickCount64()<captureAfter)return;recording=cb;physical=gpu;device=d;auto r=request;r.stages=stagesRequested;r.buildIdentity=VER_BUILD_COMMIT;r.saveOriginal=saveOriginal;r.saveFinal=saveFinal;
 if(!r.stages&&r.images.size()>2)r.images.resize(2);if(Owner().Record(r,r.images[0].image,r.images[1].image))--samplesRemaining;else samplesRemaining=0;recording=VK_NULL_HANDLE;}
void RecordVulkanNrPixelProbe(const VkNrCaptureRequest& request,VkCommandBuffer cb,VkPhysicalDevice gpu,VkDevice d){
 try{std::lock_guard lock(captureMutex);if(!cb||!gpu||!d||!probeBudget.Try(GetTickCount64(),ProbeOwner().Busy()))return;
  recording=cb;physical=gpu;device=d;struct ClearRecording{~ClearRecording(){recording=VK_NULL_HANDLE;}}clear;
  ProbeOwner().Arm(request.frame.routeEpoch);
  if(!ProbeOwner().Record(request,request.images.size()>0?request.images[0].image:VK_NULL_HANDLE,request.images.size()>1?request.images[1].image:VK_NULL_HANDLE)){
   ProbeOwner().CancelNew("Pixel probe attempt unavailable");LOG_INFO("Vulkan NR pixel probe unavailable: use={} attempt={}/{}; rendering continues",request.use.value,probeBudget.attempts,PixelProbe::Budget::MaximumAttempts);}
 }catch(...){/* Diagnostic failure must not fail the rendered frame. */}
}
void RevokeVulkanNrCaptures(uint64_t epoch){std::lock_guard lock(captureMutex);samplesRemaining=0;Owner().RevokeNewRequests(epoch);ProbeOwner().RevokeNewRequests(epoch);}
void AbandonVulkanNrCaptures(){std::lock_guard lock(captureMutex);samplesRemaining=0;Owner().AbandonDevice();ProbeOwner().AbandonDevice();}
uint64_t VulkanNrCaptureBytes(){std::lock_guard lock(captureMutex);return Owner().PrivateBytes()+ProbeOwner().PrivateBytes();}
}
