#pragma once
#include "vulkan_nr_gpu_host.h"
#include "../../OptiScaler/shaders/dlssnr/DlssNr_Vk.h"
#include "../../OptiScaler/dlssnr/VulkanNrGeneration.h"
#include "../../OptiScaler/dlssnr/VulkanNrCapture.h"
#include "../../OptiScaler/dlssnr/VulkanNrPassRecorder.h"
#include <DirectXPackedVector.h>
inline void GpuAssert(bool value,const char* why){if(!value)throw std::runtime_error(why);}
inline NVSDK_NGX_Resource_VK GpuResource(VkGpuHost::Image& image,VkExtent2D extent){
 NVSDK_NGX_Resource_VK r{};r.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;r.Resource.ImageViewInfo={image.view,image.image,{VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1},image.format,extent.width,extent.height};return r;
}
struct GpuCapturePayload: DlssNr::VkNrCapturePayload {
 VkGpuHost::Buffer *original,*final;std::vector<unsigned char> expected;unsigned* published;
 bool HostVisible()const override{return original->mapped&&final->mapped;}
 bool Publish(const DlssNr::VkNrCaptureRequest& r) override{
  GpuAssert(r.use&&r.completedPasses==r.requestedPasses,"matched whole-chain capture identity");
  GpuAssert(std::memcmp(expected.data(),original->mapped,expected.size())==0,"captured original bytes equal donor identity oracle");
  if(std::memcmp(expected.data(),final->mapped,expected.size())!=0){
   const auto* actual=static_cast<const unsigned char*>(final->mapped);size_t differences=0,first=expected.size();
   for(size_t i=0;i<expected.size();++i)if(expected[i]!=actual[i]){if(first==expected.size())first=i;++differences;}
   std::fprintf(stderr,"GPU identity mismatch format=%u extent=%ux%u passes=%u bytes=%zu differing=%zu first=%zu expected=%u actual=%u\n",
    unsigned(r.frame.representation.format),r.frame.output.width,r.frame.output.height,r.requestedPasses,expected.size(),differences,first,unsigned(expected[first]),unsigned(actual[first]));
   for(size_t i=0;i<(std::min)(expected.size(),size_t{32});++i)std::fprintf(stderr,"%zu:%u/%u%s",i,unsigned(expected[i]),unsigned(actual[i]),i%8==7?"\n":" ");
  }
  GpuAssert(std::memcmp(expected.data(),final->mapped,expected.size())==0,"captured final signed/alpha bytes equal donor identity oracle");++*published;return true;
 }
};
inline void QualifyCompositionGpu(VkGpuHost& h){
 using namespace DlssNr;
 for(auto extent:{VkExtent2D{32,8},VkExtent2D{16,16}}){h.extent=extent;
  for(auto format:{VK_FORMAT_R8G8B8A8_UNORM,VK_FORMAT_R16G16B16A16_SFLOAT}){
   DlssNr_Vk refused("feature-disabled fixture",h.device,h.physical,false);GpuAssert(!refused.CanRender(),"unsupported feature prevents pipeline creation");
   DlssNr_Vk shader("non-proprietary parity fixture",h.device,h.physical,h.storageWriteWithoutFormatEnabled);GpuAssert(shader.CanRender(),"actual composition pipeline");
   const auto usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
   auto& source=h.MakeImage(format,usage);auto& ping=h.MakeImage(format,usage);auto& pong=h.MakeImage(format,usage);auto& keep=h.MakeImage(format,usage);
   const auto bytes=extent.width*extent.height*(format==VK_FORMAT_R16G16B16A16_SFLOAT?8:4);
   auto& upload=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);auto& original=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);auto& final=h.MakeBuffer(bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
   if(format==VK_FORMAT_R16G16B16A16_SFLOAT){auto* p=static_cast<DirectX::PackedVector::HALF*>(upload.mapped);for(unsigned i=0;i<bytes/2;++i)p[i]=DirectX::PackedVector::XMConvertFloatToHalf(i%4==3?float(i%8)/8.f:(float(i%33)-8.f)/4.f);}
   else {auto* p=static_cast<unsigned char*>(upload.mapped);for(unsigned i=0;i<bytes;++i)p[i]=static_cast<unsigned char>((i*37)%256);}
   std::vector<unsigned char> expected(static_cast<unsigned char*>(upload.mapped),static_cast<unsigned char*>(upload.mapped)+bytes);
   for(uint32_t passes:{1u,2u,4u,10u}){
    h.Begin();h.Upload(upload,source);h.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);
    auto a=GpuResource(source,extent),b=GpuResource(ping,extent),c=GpuResource(pong,extent);
    VkFrameRequest request;request.commandBuffer=h.command;request.use=h.use;request.color=&a;request.contract.deviceGeneration=VkNrCompletionDeviceGeneration(h.device);request.contract.output=request.contract.work=extent;request.contract.routeEpoch=1;request.contract.route=VkNrRoute::Present;
    request.contract.representation={format,format==VK_FORMAT_R16G16B16A16_SFLOAT?VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:VK_COLOR_SPACE_SRGB_NONLINEAR_KHR,format==VK_FORMAT_R16G16B16A16_SFLOAT?3u:1u};
    VkNrPassPlan plan;plan.requireComplete=true;for(uint32_t i=0;i<passes;++i){VkNrGenerationKey key;key.frame=request.contract;key.colorFormat=key.targetFormat=format;key.passIndex=i;key.upFilter=i%2;key.downFilter=i%8;plan.passes.push_back(key);}
    auto reservation=shader.Reserve(h.use,VkNrPassDemand(passes));GpuAssert(bool(reservation),"complete chain immutable reservation");
    VkNrPassCallbacks callbacks;
    callbacks.record=[&](uint32_t i,const VkFrameRequest& input,const VkNrGenerationKey&,const VkNrReservation& slots,uint64_t revision){
     auto& target=i%2?pong:ping;auto& out=i%2?c:b;h.Move(target,VK_IMAGE_LAYOUT_GENERAL);
     DlssNrConstants constants{};constants.Mode=DlssNrMode_Present;constants.WhitePoint=2.5f;constants.Width=extent.width;constants.Height=extent.height;
     const bool ok=shader.Dispatch(h.command,h.use,slots.slots[0],constants,extent.width,extent.height,input.color->Resource.ImageViewInfo.ImageView,input.color->Resource.ImageViewInfo.ImageView,source.view,input.color->Resource.ImageViewInfo.ImageView,target.view,keep.view);
     h.Move(target,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);VkRecordResult r{ok?VkRecordStatus::Complete:VkRecordStatus::NoWork,""};r.use=h.use;r.outputWriteRecorded=ok;return VkNrPassStep{r,&out,revision+1};
    };
    callbacks.deliver=[&](const VkNrPassStep& step,uint32_t){h.Read(step.output==&b?ping:pong,final);return true;};
    auto result=RecordVkNrPassChain(request,plan,*reservation,callbacks);GpuAssert(result.deliverable&&result.completed==passes,"actual serial pass chain final delivery");
    unsigned published=0;
    VulkanNrCapture capture(VulkanNrRecordings(),[&](const VkNrCaptureRequest&,VkImage,VkImage,uint64_t){
     h.Read(source,original);auto p=std::make_unique<GpuCapturePayload>();p->original=&original;p->final=&final;p->expected=expected;p->published=&published;p->bytes=bytes*2;return VkNrCaptureCreation{std::move(p),true,{}};
    },[](VkNrCapturePayload&,bool){return true;});
    VkNrCaptureRequest captureRequest;captureRequest.use=h.use;captureRequest.frame=request.contract;captureRequest.requestedPasses=captureRequest.completedPasses=passes;
    capture.Arm(1);GpuAssert(capture.Record(captureRequest,source.image,passes%2?ping.image:pong.image),"actual capture readback recorded");capture.PublishCompleted(VulkanNrRecordings());GpuAssert(published==0,"no early host publication");
    h.Submit();capture.PublishCompleted(VulkanNrRecordings());GpuAssert(published==1,"completed matched capture published");
    std::printf("PASS GPU composition format=%u extent=%ux%u chain=%u matched signed/alpha capture use=%llu\n",unsigned(format),extent.width,extent.height,passes,static_cast<unsigned long long>(h.use.value));
   }
   // Twenty independently retained dispatches on one not-yet-submitted recording.
   h.Begin();h.Upload(upload,source);h.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(ping,VK_IMAGE_LAYOUT_GENERAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);
   std::vector<VkNrUseId> uses;
   for(unsigned i=0;i<20;++i){auto use=VulkanNrRecordings().Reserve(h.command,VkNrCompletionDeviceGeneration(h.device));GpuAssert(bool(use)&&ReserveVkNrCompletion(h.device,*use),"pressure use reserved");uses.push_back(*use);auto slots=shader.Reserve(*use,1);GpuAssert(bool(slots),"twentieth pending dispatch preserves descriptors");
    DlssNrConstants constants{};constants.Mode=DlssNrMode_Present;constants.Width=extent.width;constants.Height=extent.height;constants.WhitePoint=2.5f;
    GpuAssert(shader.Dispatch(h.command,*use,slots->slots[0],constants,extent.width,extent.height,source.view,source.view,source.view,source.view,ping.view,keep.view),"pressure actual shader dispatch");GpuAssert(!VulkanNrRecordings().GpuComplete(*use),"recording isn't completion");}
   h.Read(ping,final);h.Submit();for(auto use:uses)GpuAssert(VulkanNrRecordings().GpuComplete(use),"all pressure uses have real completion");GpuAssert(std::memcmp(expected.data(),final.mapped,bytes)==0,"pressure kept immutable sampled input");
   if(format==VK_FORMAT_R8G8B8A8_UNORM){
    h.Begin();h.Upload(upload,source);h.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(ping,VK_IMAGE_LAYOUT_GENERAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);
    auto slots=shader.Reserve(h.use,1);GpuAssert(bool(slots),"active color crop reservation");
    DlssNrConstants crop{};crop.Mode=DlssNrMode_Encode;crop.Passthrough=1;
    crop.Width=extent.width/2;crop.Height=extent.height/2;crop.SourceX=extent.width/4;crop.SourceY=extent.height/4;
    GpuAssert(shader.Dispatch(h.command,h.use,slots->slots[0],crop,crop.Width,crop.Height,source.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,ping.view,keep.view),"actual shader records active render crop");
    h.Read(ping,final);h.Submit();auto* actual=static_cast<unsigned char*>(final.mapped);
    for(uint32_t y=0;y<crop.Height;++y)for(uint32_t x=0;x<crop.Width;++x)
     GpuAssert(std::memcmp(actual+(y*extent.width+x)*4,expected.data()+((y+crop.SourceY)*extent.width+x+crop.SourceX)*4,4)==0,"active crop RGB and alpha match source offset pixels");
    std::puts("PASS GPU active Pre-SR color origin and render raster crop");
   }
   std::printf("PASS GPU 20 pending dispatches format=%u\n",unsigned(format));
  }
 }
 h.extent={32,8};
 // The composition shader also targets the single-channel exposure meter.
 // Exercise that storage width with the same shader, not a separately typed fixture.
 {
  const auto usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
  auto& source=h.MakeImage(VK_FORMAT_R32_SFLOAT,usage);auto& target=h.MakeImage(VK_FORMAT_R32_SFLOAT,usage);
  auto& upload=h.MakeBuffer(h.extent.width*h.extent.height*sizeof(float),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);
  auto& read=h.MakeBuffer(upload.bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);
  auto* values=static_cast<float*>(upload.mapped);for(unsigned i=0;i<h.extent.width*h.extent.height;++i)values[i]=(static_cast<float>(i)-64.f)/16.f;
  DlssNr_Vk shader("R32 storage width fixture",h.device,h.physical,h.storageWriteWithoutFormatEnabled);
  h.Begin();h.Upload(upload,source);h.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(target,VK_IMAGE_LAYOUT_GENERAL);
  auto slots=shader.Reserve(h.use,1);GpuAssert(bool(slots),"R32 composition reservation");
  DlssNrConstants c{};c.Mode=DlssNrMode_Present;c.Width=h.extent.width;c.Height=h.extent.height;
  GpuAssert(shader.Dispatch(h.command,h.use,slots->slots[0],c,h.extent.width,h.extent.height,source.view,VK_NULL_HANDLE,VK_NULL_HANDLE,VK_NULL_HANDLE,target.view,VK_NULL_HANDLE),"R32 composition dispatch");
  h.Read(target,read);h.Submit();GpuAssert(std::memcmp(upload.mapped,read.mapped,static_cast<size_t>(read.bytes))==0,"R32 signed storage width matches byte oracle");
  std::puts("PASS GPU composition R32 signed storage width identity");
 }
}
inline void QualifySecondaryGpu(VkGpuHost& h){
 using namespace DlssNr;auto& owner=VulkanNrRecordings();
 const auto usage=VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 auto& source=h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,usage);auto& target=h.MakeImage(source.format,usage);auto& keep=h.MakeImage(source.format,usage);
 auto& upload=h.MakeBuffer(h.extent.width*h.extent.height*8,VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memset(upload.mapped,0,static_cast<size_t>(upload.bytes));
 h.Begin();h.Upload(upload,source);h.Move(source,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);h.Move(target,VK_IMAGE_LAYOUT_GENERAL);h.Move(keep,VK_IMAGE_LAYOUT_GENERAL);h.Submit();
 VkCommandBuffer secondary{},primaries[2]{};VkCommandBufferAllocateInfo allocation{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};allocation.commandPool=h.pool;allocation.level=VK_COMMAND_BUFFER_LEVEL_SECONDARY;allocation.commandBufferCount=1;
 VkGpuCheck(vkAllocateCommandBuffers(h.device,&allocation,&secondary),"secondary allocation");owner.OnAllocate(h.device,h.pool,secondary,h.family,0,VK_COMMAND_BUFFER_LEVEL_SECONDARY);
 VkCommandBufferInheritanceInfo inheritance{VK_STRUCTURE_TYPE_COMMAND_BUFFER_INHERITANCE_INFO};VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};begin.flags=VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;begin.pInheritanceInfo=&inheritance;
 VkGpuCheck(vkBeginCommandBuffer(secondary,&begin),"secondary begin");owner.OnBegin(secondary,begin.flags,VK_SUCCESS);auto use=*owner.Reserve(secondary,VkNrCompletionDeviceGeneration(h.device));GpuAssert(ReserveVkNrCompletion(h.device,use),"secondary checkpoint capacity");
 DlssNr_Vk shader("secondary replay fixture",h.device,h.physical,h.storageWriteWithoutFormatEnabled);auto slots=shader.Reserve(use,1);GpuAssert(bool(slots),"secondary immutable descriptor");DlssNrConstants constants{};constants.Mode=DlssNrMode_Present;constants.Width=h.extent.width;constants.Height=h.extent.height;constants.WhitePoint=2.5f;
 GpuAssert(shader.Dispatch(secondary,use,slots->slots[0],constants,h.extent.width,h.extent.height,source.view,source.view,source.view,source.view,target.view,keep.view),"actual secondary dispatch");VkGpuCheck(vkEndCommandBuffer(secondary),"secondary end");owner.OnEnd(secondary,VK_SUCCESS);
 allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;allocation.commandBufferCount=2;VkGpuCheck(vkAllocateCommandBuffers(h.device,&allocation,primaries),"two primary allocations");
 for(auto primary:primaries){owner.OnAllocate(h.device,h.pool,primary,h.family);VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};VkGpuCheck(vkBeginCommandBuffer(primary,&bi),"primary begin");owner.OnBegin(primary,0,VK_SUCCESS);vkCmdExecuteCommands(primary,1,&secondary);owner.OnExecute(primary,{&secondary,1});VkGpuCheck(vkEndCommandBuffer(primary),"primary end");owner.OnEnd(primary,VK_SUCCESS);
  VkSubmitInfo submit{VK_STRUCTURE_TYPE_SUBMIT_INFO};submit.commandBufferCount=1;submit.pCommandBuffers=&primary;VkGpuCheck(ObserveVkNrQueueSubmit(h.queue,{&primary,1},[&]{return vkQueueSubmit(h.queue,1,&submit,VK_NULL_HANDLE);}),"secondary actual submission");++h.submitted;
 }
 GpuAssert(!owner.Reusable(use),"two primary executions retain secondary descriptors");VkGpuCheck(vkQueueWaitIdle(h.queue),"secondary actual completion");PollVkNrCompletions();GpuAssert(owner.GpuComplete(use)&&!owner.Reusable(use),"GPU completion preserves replay references");h.completed+=2;
 for(auto primary:primaries){VkGpuCheck(vkResetCommandBuffer(primary,0),"primary terminal reset");owner.OnReset(primary,VK_SUCCESS);}GpuAssert(!owner.Reusable(use),"secondary itself remains replayable");VkGpuCheck(vkResetCommandBuffer(secondary,0),"secondary terminal reset");owner.OnReset(secondary,VK_SUCCESS);GpuAssert(owner.Reusable(use),"last replay reference releases immutable slot");
 auto& read=h.MakeBuffer(upload.bytes,VK_BUFFER_USAGE_TRANSFER_DST_BIT);h.Begin();h.Read(target,read);h.Submit();GpuAssert(std::memcmp(upload.mapped,read.mapped,static_cast<size_t>(read.bytes))==0,"actual secondary output matches identity oracle");
 vkFreeCommandBuffers(h.device,h.pool,2,primaries);for(auto cb:primaries)owner.OnFree(cb);vkFreeCommandBuffers(h.device,h.pool,1,&secondary);owner.OnFree(secondary);std::puts("PASS GPU secondary executes in two primaries, completion and replay retirement");
}
inline void QualifyGenerationGpu(VkGpuHost& h){
 using namespace DlssNr;auto& recording=VulkanNrRecordings();
 struct Payload:VkNrGenerationPayload{VkGpuHost::Image* image=nullptr;};
 VkNrGenerationOwner generations(recording,[&](const VkNrGenerationKey& key,VkNrUseId,uint64_t budget){
  auto p=std::make_unique<Payload>();const auto saved=h.extent;h.extent=key.frame.work;p->image=&h.MakeImage(VK_FORMAT_R16G16B16A16_SFLOAT,VK_IMAGE_USAGE_STORAGE_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT);h.extent=saved;VkMemoryRequirements requirements{};vkGetImageMemoryRequirements(h.device,p->image->image,&requirements);p->privateBytes=requirements.size;GpuAssert(p->privateBytes<=budget,"real generation allocation requirements");return VkNrGenerationCreation{std::move(p),true,false,{}};
 },[&](VkNrGenerationPayload& base){auto& p=static_cast<Payload&>(base);vkDestroyImageView(h.device,p.image->view,nullptr);vkDestroyImage(h.device,p.image->image,nullptr);vkFreeMemory(h.device,p.image->memory,nullptr);p.image->view=VK_NULL_HANDLE;p.image->image=VK_NULL_HANDLE;p.image->memory=VK_NULL_HANDLE;return true;});
 VkNrGenerationKey key;key.frame.deviceGeneration=VkNrCompletionDeviceGeneration(h.device);key.frame.routeEpoch=1;key.frame.output=key.frame.work=h.extent;key.frame.representation.format=key.colorFormat=key.targetFormat=VK_FORMAT_R16G16B16A16_SFLOAT;
 h.Begin(0);auto first=generations.Prepare(key,h.use);GpuAssert(first.ready&&generations.Commit(first.generation),"actual first generation");h.Submit(false);
 h.Begin();key.frame.work={16,16};key.upFilter=1;key.downFilter=3;key.tuning.intensity=1.1f;auto second=generations.Prepare(key,h.use);GpuAssert(second.ready&&generations.Commit(second.generation),"live raster/filter/model key creates actual resources");h.Submit();generations.RetireCompleted(recording);GpuAssert(generations.Count()==1,"old generation retires after its terminal reset");generations.Disable();generations.RetireCompleted(recording);GpuAssert(generations.Count()==0,"actual live generation release");std::puts("PASS GPU live generation raster/filter/model identity and private allocation retirement");
}
