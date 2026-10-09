#include <map>
#include "NativeVulkanGuides.h"
#include "NativeGuideTiming.h"
#include "NativeGuideCapture.h"
#include "NativeGuideRoute.h"
#include "connections/ConnectionPolicy.h"
#include "PreparedGuideStatusV2.h"
#include "NativeFgVulkan.h"
#include "NativeFgLayouts.h"
#include "VulkanNrImageFacts.h"
#include "VulkanNrRecording.h"
#include "VulkanNrCompletion.h"
#include "VulkanPresentRegistry.h"
#include "PreparedVkDx12Transport.h"
#include "VulkanPresent.h"
#include <Windows.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <cmath>
#include <memory>
#include <map>
#include <mutex>
#include <unordered_map>
#include <vector>

namespace DlssNr::NativeVulkanGuides {
namespace {
// Saved device addresses may still alias detoured loader entry points. Only
// suppress native-guide callbacks; recording/submission ownership stays active.
thread_local unsigned injecting=0;
std::atomic<bool> captureActive{false};
struct InjectionScope {InjectionScope(){++injecting;}~InjectionScope(){--injecting;}};
constexpr size_t MaxCaptures=8;
constexpr VkDeviceSize MaxBytes=128ull*1024*1024;
#define NATIVE_FUNCTIONS(X) \
 X(CreateBuffer) X(DestroyBuffer) X(GetBufferMemoryRequirements) X(AllocateMemory) X(FreeMemory) \
 X(BindBufferMemory) X(MapMemory) X(UnmapMemory) X(CmdPipelineBarrier) X(CmdCopyImageToBuffer) \
 X(CmdCopyBufferToImage) X(CmdCopyBuffer) X(CreateCommandPool) X(DestroyCommandPool) X(AllocateCommandBuffers) \
 X(BeginCommandBuffer) X(EndCommandBuffer) X(ResetCommandPool) X(CreateFence) X(DestroyFence) \
 X(ResetFences) X(WaitForFences) X(QueueSubmit) X(CreateRenderPass) X(CreateRenderPass2) \
 X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer) \
 X(CreateQueryPool) X(DestroyQueryPool) X(CmdResetQueryPool) X(CmdWriteTimestamp) X(GetQueryPoolResults) X(CreateImageView) X(DestroyImageView)
struct Buffer { VkBuffer buffer{}; VkDeviceMemory memory{}; void* mapped=nullptr; VkDeviceSize bytes=0,capacity=0; VkMemoryPropertyFlags properties=0; };
struct Depth {
 Buffer buffer; VkNrUseId use; VkImage image{},color{}; uint64_t imageGeneration=0,serial=0,workSerial=0,colorGeneration=0;
 VkCommandBuffer recording{};uint64_t incarnation=0;
 VkNrFrameContract colorContext{};
 VkExtent2D extent{}; VkFormat format{}; bool inverted=false,consumed=false,swapchainBound=false; uint64_t draws=0;
 uint32_t mip=0,layer=0;bool directionKnown=false,observedSource=false;
};
// Clear convention is recording-local metadata. It never owns GPU work or
// survives a reset/re-record, and is frozen into a capture before later writes.
struct Direction {VkImage image{};uint64_t generation=0,incarnation=0;
 VkCommandBuffer recording{};uint32_t mip=0,layer=0;bool inverted=false;};
struct SuppressedDepth {VkImage image{};uint64_t generation=0;uint32_t mip=0,layer=0;};
struct CapturedImages {
 VkDevice device{};PFN_vkDestroyImageView destroy=nullptr;
 std::array<NVSDK_NGX_Resource_VK,2> images{};
 ~CapturedImages(){if(device&&destroy)for(auto& image:images)if(image.Resource.ImageViewInfo.ImageView)destroy(device,image.Resource.ImageViewInfo.ImageView,nullptr);}
};
struct Device {
 VkDevice device{}; VkPhysicalDevice physical{}; uint64_t luid=0,generation=0,frame=0,lastSubmission=0,captures=0,lastCaptureFrame=0;
 VkPhysicalDeviceMemoryProperties memory{}; std::vector<Depth> depths;
 VkCommandPool pool{}; VkCommandBuffer command{}; VkFence fence{}; uint32_t family=UINT32_MAX;
 Buffer color; bool unsafe=false,resetPending=false; VkDeviceSize allocated=0; VkExtent2D lastExtent{};
 bool previousEstimated=false,processorBlocked=false;
 NativeGuides::Outcome lastOutcome=NativeGuides::Outcome::Unavailable;
 PreparedGuides::StatusV2 effective;
 uint64_t overrideConflictSerial=UINT64_MAX;
 std::vector<SuppressedDepth> suppressedDepths;bool suppressionOverflow=false;
 std::vector<Direction> directions;
 PreparedTransport::Dispatch transportDispatch{};
 std::unique_ptr<PreparedTransport::Session> shared;
 VkExtent2D sharedExtent{},previousDepthExtent{};VkDeviceSize sharedDepthBytes=0;uint64_t sharedSerial=0;VkQueue previousQueue{};
 bool sharedUnsupported=false,sharedIdle=true;
 std::string transportReason;
 bool pending=false;
 bool vulkanRenderer=false;std::shared_ptr<CapturedImages> capturedImages;
 VkQueryPool timestamps{};bool timestampsTried=false;
 float timestampPeriod=0;std::vector<uint32_t> timestampBits;
 bool generatedReady=false;std::uint64_t generatedFrame=0,generatedCopies=0;
 double lastCaptureMs=0;
#define MEMBER(Name) PFN_vk##Name Name=nullptr;
 NATIVE_FUNCTIONS(MEMBER)
#undef MEMBER
};
struct View { VkDevice device{}; VkImageViewCreateInfo info{}; uint64_t generation=0; };
struct Pass { VkDevice device{}; uint32_t attachment=VK_ATTACHMENT_UNUSED; VkAttachmentDescription depth{}; bool valid=false; std::vector<uint32_t> colors;
 const char* rejection="Render pass structure unsupported"; };
struct Framebuffer { VkDevice device{}; VkRenderPass pass{}; std::vector<VkImageView> views; uint32_t width=0,height=0,layers=0; };
struct Scope { VkImageView view{}; VkImageLayout layout=VK_IMAGE_LAYOUT_UNDEFINED; VkRect2D area{};
 uint64_t incarnation=0,draws=0; bool valid=false,inverted=false; std::vector<VkImage> colors;
 bool directionKnown=false; };
std::mutex mutex;
std::unordered_map<VkDevice,std::unique_ptr<Device>> devices;
std::map<std::pair<VkDevice,VkImageView>,View> views;
std::map<std::pair<VkDevice,VkRenderPass>,Pass> passes;
std::map<std::pair<VkDevice,VkFramebuffer>,Framebuffer> framebuffers;
std::unordered_map<VkCommandBuffer,Scope> scopes;
Snapshot status;
std::string imageDetails;
// Only fixed labels are inserted: bounded diagnostic memory, independent of the
// number of frames. Later UI/postprocess scopes cannot erase earlier refusals.
std::map<std::string,uint64_t> rejections;
uint64_t passObservations=0,framebufferObservations=0,viewObservations=0;
void Reject(const char* reason){++rejections[reason];status.captureReason=reason;}
int DirectionOverride() {
 const int direction=NativeGuides::depthDirection.load(std::memory_order_relaxed);
 if(direction==0||direction==1)return direction;
 wchar_t value[3]{}; const auto n=GetEnvironmentVariableW(L"NEUROTIC_NATIVE_DEPTH_INVERTED",value,3);
 return n==1&&(value[0]==L'0'||value[0]==L'1')?value[0]-L'0':-1;
}
bool Orientation(VkAttachmentLoadOp load,float clear,bool& inverted) {
 if(load!=VK_ATTACHMENT_LOAD_OP_CLEAR||(clear!=0.f&&clear!=1.f))return false;
 inverted=clear==0.f;return true;
}
unsigned DepthBytes(VkFormat f) { return (f==VK_FORMAT_D16_UNORM||f==VK_FORMAT_D16_UNORM_S8_UINT)?2:
 (f==VK_FORMAT_D32_SFLOAT||f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_X8_D24_UNORM_PACK32)?4:0; }
bool Stencil(VkFormat f) { return f==VK_FORMAT_D32_SFLOAT_S8_UINT||f==VK_FORMAT_D24_UNORM_S8_UINT||f==VK_FORMAT_D16_UNORM_S8_UINT; }
constexpr VkImageCreateFlags CopyFlags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_EXTENDED_USAGE_BIT|VK_IMAGE_CREATE_ALIAS_BIT;
bool FormatChain(const void* chain){unsigned count=0;bool seen=false;
 for(auto* n=static_cast<const VkBaseInStructure*>(chain);n;n=n->pNext){
  if(++count>8||seen||n->sType!=VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO)return false;seen=true;
  const auto& f=*reinterpret_cast<const VkImageFormatListCreateInfo*>(n);if(f.viewFormatCount>256||(f.viewFormatCount&&!f.pViewFormats))return false;
 }return true;
}
bool SingleRange(const VkNrObservedImage& image,const VkImageViewCreateInfo& view,uint32_t& mip,uint32_t& layer,VkExtent2D& extent){
 const auto& r=view.subresourceRange;mip=r.baseMipLevel;layer=r.baseArrayLayer;
 if(mip>=image.mips||layer>=image.layers||mip>=32)return false;
 const auto levels=r.levelCount==VK_REMAINING_MIP_LEVELS?image.mips-mip:r.levelCount;
 const auto layers=r.layerCount==VK_REMAINING_ARRAY_LAYERS?image.layers-layer:r.layerCount;
 extent={(std::max)(1u,image.extent.width>>mip),(std::max)(1u,image.extent.height>>mip)};
 return levels==1&&layers==1&&(r.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)&&view.format==image.format;
}
void ForgetDirections(VkCommandBuffer cb) {
 const auto device=VulkanNrRecordings().CommandDevice(cb);auto d=devices.find(device);
 if(d!=devices.end())std::erase_if(d->second->directions,[&](const auto& e){return e.recording==cb;});
}
void RecordDirection(Device& d,VkCommandBuffer cb,VkImage image,uint64_t generation,uint32_t mip,uint32_t layer,bool known,bool inverted){
 auto& owner=VulkanNrRecordings();
 std::erase_if(d.directions,[&](const auto& e){return e.incarnation!=owner.Incarnation(e.recording)||
  (e.recording==cb&&e.image==image&&e.mip==mip&&e.layer==layer);});
 if(!known)return;
 if(d.directions.size()>=256){ForgetDirections(cb);Reject("Depth clear metadata capacity reached");return;}
 d.directions.push_back({image,generation,owner.Incarnation(cb),cb,mip,layer,inverted});
}
bool PriorDirection(VkCommandBuffer cb,VkImageView view,bool& inverted){
 const auto device=VulkanNrRecordings().CommandDevice(cb);auto d=devices.find(device);auto v=views.find({device,view});
 if(d==devices.end()||v==views.end())return false;
 const auto image=VulkanNrImageFacts().Image(device,v->second.info.image);uint32_t mip=0,layer=0;VkExtent2D extent{};
 if(!image||image->generation!=v->second.generation||!SingleRange(*image,v->second.info,mip,layer,extent))return false;
 for(const auto& e:d->second->directions)if(e.recording==cb&&e.incarnation==VulkanNrRecordings().Incarnation(cb)&&
  e.image==v->second.info.image&&e.generation==image->generation&&e.mip==mip&&e.layer==layer){inverted=e.inverted;return true;}
 return false;
}
// Unsupported rendering must not leave an older clear available to a later LOAD.
struct ScopeHistoryGuard {VkCommandBuffer cb;~ScopeHistoryGuard(){ForgetDirections(cb);}};
bool CombinedLayout(VkImageLayout layout) {return layout==VK_IMAGE_LAYOUT_GENERAL||
 layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL||layout==VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL||
 layout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;}
void Free(Device& d,Buffer& b) {
 if(b.mapped)d.UnmapMemory(d.device,b.memory);
 if(b.buffer)d.DestroyBuffer(d.device,b.buffer,nullptr);
 if(b.memory)d.FreeMemory(d.device,b.memory,nullptr);
 d.allocated-=b.bytes;b={};
}
bool Allocate(Device& d,VkDeviceSize bytes,Buffer& out,bool gpu=false) {
 if(!bytes||bytes>MaxBytes||d.allocated+bytes>MaxBytes)return false;
 VkBufferCreateInfo ci{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};ci.size=bytes;
 ci.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;
 if(d.CreateBuffer(d.device,&ci,nullptr,&out.buffer)!=VK_SUCCESS)return false;
 VkMemoryRequirements req{};d.GetBufferMemoryRequirements(d.device,out.buffer,&req);
 uint32_t type=UINT32_MAX;
 for(unsigned cached=0;cached<2&&type==UINT32_MAX;++cached)for(uint32_t i=0;i<d.memory.memoryTypeCount;++i)
  if((req.memoryTypeBits&(1u<<i))&&
   (gpu?(d.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT)!=0:
    ((d.memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==
     (VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)&&
     (cached||(d.memory.memoryTypes[i].propertyFlags&VK_MEMORY_PROPERTY_HOST_CACHED_BIT))))){type=i;break;}
 VkMemoryAllocateInfo ai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};ai.allocationSize=req.size;ai.memoryTypeIndex=type;
 if(type==UINT32_MAX||req.size>MaxBytes||d.allocated+req.size>MaxBytes||d.AllocateMemory(d.device,&ai,nullptr,&out.memory)!=VK_SUCCESS){Free(d,out);return false;}
 out.bytes=req.size;out.capacity=bytes;out.properties=d.memory.memoryTypes[type].propertyFlags;d.allocated+=out.bytes;
 if(d.BindBufferMemory(d.device,out.buffer,out.memory,0)!=VK_SUCCESS||
    (!gpu&&d.MapMemory(d.device,out.memory,0,VK_WHOLE_SIZE,0,&out.mapped)!=VK_SUCCESS)){Free(d,out);return false;}
 return true;
}
// Match the upper bounds of the packed CPU mainline before allocating any
// staging. Small recorder fixtures need not satisfy the flow SDK minimum size.
constexpr VkDeviceSize MaxColorBytes=160ull*1024*1024/20*4;
bool CaptureExtent(VkExtent2D extent){return extent.width&&extent.height&&extent.width<=4096&&extent.height<=2160&&uint64_t(extent.width)*extent.height*4<=MaxColorBytes;}
bool CompatibleDepthExtent(VkExtent2D depth,VkExtent2D color){
 return CaptureExtent(depth)&&CaptureExtent(color)&&uint64_t(depth.width)*color.height==uint64_t(color.width)*depth.height;
}
bool EnsureColor(Device& d,VkDeviceSize bytes){
 if(!bytes||bytes>MaxColorBytes)return false;
 if(d.color.capacity>=bytes)return true;
 Free(d,d.color);return Allocate(d,bytes,d.color);
}
bool DecodeDepth(const Depth& source,VkExtent2D extent,std::vector<float>& target){
 if(!CompatibleDepthExtent(source.extent,extent)||target.size()!=uint64_t(extent.width)*extent.height||!source.buffer.mapped)return false;
 const auto pixels=size_t(source.extent.width)*source.extent.height;
 // Never run scalar decoding against potentially uncached mapped GPU memory.
 std::vector<uint8_t> packed(pixels*DepthBytes(source.format));std::memcpy(packed.data(),source.buffer.mapped,packed.size());
 std::vector<float> decoded(pixels);
 for(size_t i=0;i<pixels;++i){
  if(DepthBytes(source.format)==2){uint16_t v;std::memcpy(&v,packed.data()+i*2,2);decoded[i]=v/65535.f;}
  else if(source.format==VK_FORMAT_D32_SFLOAT||source.format==VK_FORMAT_D32_SFLOAT_S8_UINT)std::memcpy(&decoded[i],packed.data()+i*4,4);
  else{uint32_t v;std::memcpy(&v,packed.data()+i*4,4);decoded[i]=(v&0xffffffu)/16777215.f;}
  if(!std::isfinite(decoded[i])||decoded[i]<0||decoded[i]>1)return false;
 }
 for(uint32_t y=0;y<extent.height;++y)for(uint32_t x=0;x<extent.width;++x){
  const auto sx=((2*x+1)*source.extent.width)/(2*extent.width),sy=((2*y+1)*source.extent.height)/(2*extent.height);
  target[size_t(y)*extent.width+x]=decoded[size_t(sy)*source.extent.width+sx];
 }return true;
}
void Move(Device& d,VkCommandBuffer cb,VkImage image,VkImageLayout from,VkImageLayout to,VkImageAspectFlags aspect,uint32_t mip=0,uint32_t layer=0) {
 VkImageMemoryBarrier b{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
 b.dstAccessMask=VK_ACCESS_MEMORY_READ_BIT|VK_ACCESS_MEMORY_WRITE_BIT;b.oldLayout=from;b.newLayout=to;
 b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.image=image;b.subresourceRange={aspect,mip,1,layer,1};
 d.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
}
void ToHost(Device& d,VkCommandBuffer cb,Buffer& buffer) {
 VkBufferMemoryBarrier b{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};b.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;b.dstAccessMask=buffer.mapped?VK_ACCESS_HOST_READ_BIT:VK_ACCESS_TRANSFER_READ_BIT;
 b.srcQueueFamilyIndex=b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.buffer=buffer.buffer;b.size=VK_WHOLE_SIZE;
 d.CmdPipelineBarrier(cb,VK_PIPELINE_STAGE_TRANSFER_BIT,buffer.mapped?VK_PIPELINE_STAGE_HOST_BIT:VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&b,0,nullptr);
}
void Collect(Device& d) {
 // Original producer release is insufficient while our private submission
 // can still read its depth buffer. Timeout is not retirement evidence.
 if(d.pending||d.unsafe)return;
 auto& owner=VulkanNrRecordings();
 std::erase_if(d.directions,[&](const auto& e){const auto image=VulkanNrImageFacts().Image(d.device,e.image);
  return e.incarnation!=owner.Incarnation(e.recording)||!image||image->generation!=e.generation;});
 for(auto it=d.depths.begin();it!=d.depths.end();) {
  if((it->consumed||it->serial+2<d.frame||!owner.ProducerProof(it->use))&&owner.RecordingReleased(it->use)&&owner.Reusable(it->use)) {
   Free(d,it->buffer);owner.ReleaseUse(it->use);it=d.depths.erase(it);
  } else ++it;
 }
}
bool Commands(Device& d,uint32_t family) {
 if(d.pool)return d.family==family&&d.command&&d.fence;
 VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};ci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;ci.queueFamilyIndex=family;
 if(d.CreateCommandPool(d.device,&ci,nullptr,&d.pool)!=VK_SUCCESS)return false;
 d.family=family;VkCommandBufferAllocateInfo ai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};ai.commandPool=d.pool;ai.commandBufferCount=1;
 if(d.AllocateCommandBuffers(d.device,&ai,&d.command)!=VK_SUCCESS)return false;
 VkFenceCreateInfo fi{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};return d.CreateFence(d.device,&fi,nullptr,&d.fence)==VK_SUCCESS;
}
bool Begin(Device& d) {
 if(d.pending||d.unsafe)return false;
 if(!d.command||!d.fence||d.ResetCommandPool(d.device,d.pool,0)!=VK_SUCCESS)return false;
 if(d.ResetFences(d.device,1,&d.fence)!=VK_SUCCESS)return false;
 VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
 return d.BeginCommandBuffer(d.command,&bi)==VK_SUCCESS;
}
VkResult CompleteSubmission(Device& d){
 if(!d.pending)return d.unsafe?VK_ERROR_DEVICE_LOST:VK_SUCCESS;
 const auto result=d.WaitForFences(d.device,1,&d.fence,VK_TRUE,2'000'000'000ull);
 if(result!=VK_SUCCESS)d.unsafe=true;else d.pending=false;
 return result;
}
VkResult Submit(Device& d,VkQueue queue,VkPresentInfoKHR* present,VkSemaphore signal=VK_NULL_HANDLE,uint64_t signalValue=0,VkSemaphore wait=VK_NULL_HANDLE,uint64_t waitValue=0,bool hostWait=true) {
 if(d.pending||d.unsafe)return VK_ERROR_DEVICE_LOST;
 if(d.EndCommandBuffer(d.command)!=VK_SUCCESS){
  // Shared ownership transitions may already be recorded, but their timeline
  // was never signalled. Never reuse that logical state on the next frame.
  d.unsafe=true;return VK_ERROR_INITIALIZATION_FAILED;
 }
 std::vector<VkSemaphore> waits;if(present&&present->waitSemaphoreCount)waits.assign(present->pWaitSemaphores,present->pWaitSemaphores+present->waitSemaphoreCount);
 std::vector<uint64_t> values(waits.size(),0);if(wait){waits.push_back(wait);values.push_back(waitValue);}
 std::vector<VkPipelineStageFlags> stages(waits.size(),VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
 VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.commandBufferCount=1;si.pCommandBuffers=&d.command;
 si.waitSemaphoreCount=uint32_t(waits.size());si.pWaitSemaphores=waits.data();si.pWaitDstStageMask=stages.data();
 VkTimelineSemaphoreSubmitInfo timeline{VK_STRUCTURE_TYPE_TIMELINE_SEMAPHORE_SUBMIT_INFO};
 if(signal||wait){timeline.waitSemaphoreValueCount=si.waitSemaphoreCount;timeline.pWaitSemaphoreValues=values.data();
  si.signalSemaphoreCount=signal?1u:0u;si.pSignalSemaphores=signal?&signal:nullptr;
  timeline.signalSemaphoreValueCount=si.signalSemaphoreCount;timeline.pSignalSemaphoreValues=&signalValue;si.pNext=&timeline;}
 auto result=NativeFg::QueueCall(d.QueueSubmit,queue,1,&si,d.fence);
 if(result!=VK_SUCCESS){d.unsafe=true;return result;}
 d.pending=true;
 if(present){present->waitSemaphoreCount=0;present->pWaitSemaphores=nullptr;}
 // Shared input queues its GPU consumer first. The owner still joins this
 // exact fence before command reuse, retirement, or returning to Present.
 return hostWait?CompleteSubmission(d):VK_SUCCESS;
}
bool BeginCaptureTiming(Device& d){
 if(!d.timestampsTried){
  d.timestampsTried=true;
  if(d.family<d.timestampBits.size()&&d.timestampBits[d.family]>0&&d.timestampBits[d.family]<=64&&
     std::isfinite(d.timestampPeriod)&&d.timestampPeriod>0&&d.CreateQueryPool&&d.DestroyQueryPool&&
     d.CmdResetQueryPool&&d.CmdWriteTimestamp&&d.GetQueryPoolResults){
   VkQueryPoolCreateInfo ci{VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO};ci.queryType=VK_QUERY_TYPE_TIMESTAMP;ci.queryCount=2;
   if(d.CreateQueryPool(d.device,&ci,nullptr,&d.timestamps)!=VK_SUCCESS)d.timestamps=VK_NULL_HANDLE;
  }
 }
 if(!d.timestamps)return false;
 d.CmdResetQueryPool(d.command,d.timestamps,0,2);
 d.CmdWriteTimestamp(d.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,d.timestamps,0);return true;
}
double CaptureGpuMs(Device& d){
 if(!d.timestamps||d.pending||d.unsafe)return -1;
 uint64_t ticks[2]{};
 // Never add a profiling wait; only read this recording after its fence.
 if(d.GetQueryPoolResults(d.device,d.timestamps,0,2,sizeof(ticks),ticks,sizeof(uint64_t),VK_QUERY_RESULT_64_BIT)!=VK_SUCCESS)return -1;
 const auto bits=d.timestampBits[d.family];const auto mask=bits==64?UINT64_MAX:(uint64_t(1)<<bits)-1;
 return double((ticks[1]-ticks[0])&mask)*d.timestampPeriod/1e6;
}
bool EnsureShared(Device& d,const VkObservedPresent& observed,const Depth& depth){
 const auto extent=observed.request.extent;
 const auto depthBytes=(uint64_t(depth.extent.width)*depth.extent.height*DepthBytes(depth.format)+3)&~3ull;
 if(d.shared&&d.sharedExtent.width==extent.width&&d.sharedExtent.height==extent.height&&d.sharedDepthBytes>=depthBytes)return true;
 if(d.sharedUnsupported)return false;
 if(d.shared){
  if(!NativeFg::DrainDevice(d.device)){d.processorBlocked=true;d.transportReason="Standalone FG readers remain active during shared transport resize";return false;}
  if(!d.sharedIdle||d.processorBlocked||d.unsafe||d.ResetCommandPool(d.device,d.pool,0)!=VK_SUCCESS){d.processorBlocked=true;d.transportReason="Shared GPU retirement incomplete";return false;}
  if(d.capturedImages&&d.capturedImages.use_count()!=1){d.processorBlocked=true;d.transportReason="Captured Vulkan guides remain leased";return false;}
  d.capturedImages.reset();
  if(!d.shared->Retire({true,true,true,true})){d.processorBlocked=true;return false;}
  d.shared.reset();
 }
 PreparedVulkan::DeviceInfo info;info.device=d.device;info.queue=observed.queue;
 if(!GetVulkanPresentRegistry().PreparedDevice(info)||info.physical!=d.physical||info.family!=observed.queueFamily||
    !info.externalMemoryWin32||!info.externalSemaphoreWin32||!info.timeline){d.sharedUnsupported=true;d.transportReason="CPU fallback: Vulkan sharing was not enabled at device creation";return false;}
 auto* device=NativeGuides::SharedDevice(d.luid,d.transportReason);
 if(!device){d.sharedUnsupported=true;d.transportReason="CPU fallback: "+d.transportReason;return false;}
 auto session=std::make_unique<PreparedTransport::Session>();
 PreparedTransport::ImageSpec raw{};raw.rawBytes=uint64_t(extent.width)*extent.height*4;
 const bool fg=NativeFg::Owns(observed.swapchain);
 d.vulkanRenderer=NativeGuides::VulkanRendererSelected()&&!NativeFg::Selected();
 auto rawDepth=raw;rawDepth.rawBytes=depthBytes;
 const PreparedTransport::ImageSpec sampledDepth{extent.width,extent.height,DXGI_FORMAT_R32_FLOAT,false,0};
 const PreparedTransport::ImageSpec sampledMotion{extent.width,extent.height,DXGI_FORMAT_R16G16_FLOAT,false,0};
 const std::array<PreparedTransport::ImageSpec,4> specs{raw,rawDepth,d.vulkanRenderer?sampledDepth:raw,d.vulkanRenderer?sampledMotion:raw};
 const auto initialized=session->Initialize(d.physical,d.device,observed.queue,observed.queueFamily,device,d.transportDispatch,{true,true,true},std::span(specs).first(fg||d.vulkanRenderer?4:3));
 if(!initialized){d.sharedUnsupported=true;d.transportReason="CPU fallback: "+initialized.reason;return false;}
 if(d.vulkanRenderer){
  if(!d.CreateImageView||!d.DestroyImageView){d.sharedUnsupported=true;d.transportReason="Captured guide image-view dispatch unavailable";return false;}
  auto guides=std::make_shared<CapturedImages>();guides->device=d.device;guides->destroy=d.DestroyImageView;
  for(unsigned i=0;i<2;++i){const auto image=session->Image(i+2);
   VkImageViewCreateInfo view{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};view.image=image.image;view.viewType=VK_IMAGE_VIEW_TYPE_2D;
   view.format=i?VK_FORMAT_R16G16_SFLOAT:VK_FORMAT_R32_SFLOAT;view.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
   auto& ngx=guides->images[i];ngx.Type=NVSDK_NGX_RESOURCE_VK_TYPE_VK_IMAGEVIEW;ngx.ReadWrite=false;
   auto& resource=ngx.Resource.ImageViewInfo;resource.Image=image.image;resource.Format=view.format;resource.SubresourceRange=view.subresourceRange;resource.Width=extent.width;resource.Height=extent.height;
   if(d.CreateImageView(d.device,&view,nullptr,&resource.ImageView)!=VK_SUCCESS){d.sharedUnsupported=true;d.transportReason="Captured guide image-view creation failed";return false;}
  }
  d.capturedImages=std::move(guides);
 }
 d.shared=std::move(session);d.sharedExtent=extent;d.sharedDepthBytes=depthBytes;d.sharedSerial=0;d.sharedIdle=true;Free(d,d.color);
 d.transportReason=d.vulkanRenderer?"Shared depth/motion textures -> Vulkan NR (no CPU pixel transfers)":"Shared GPU transport (no full-frame CPU transfers)";return true;
}
VkResult RecordGenerated(void* context,VkCommandBuffer command,VkImage image,unsigned width,unsigned height,std::uint64_t frame){
 InjectionScope injection;std::lock_guard lock(mutex);auto& d=*static_cast<Device*>(context);
 if(!d.shared||d.shared->Count()!=4||!d.generatedReady||d.generatedFrame!=frame||d.sharedExtent.width!=width||d.sharedExtent.height!=height||!command||!image||d.processorBlocked||d.unsafe)return VK_ERROR_INITIALIZATION_FAILED;
 // SDK submits this callback on exactly the same game queue before returning
 // virtual Present. The next native input signal is therefore after this read.
 if(!d.shared->Acquire(command,3)){d.processorBlocked=true;return VK_ERROR_INITIALIZATION_FAILED;}
 Move(d,command,image,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
 VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={width,height,1};
 d.CmdCopyBufferToImage(command,d.shared->Buffer(3),image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 Move(d,command,image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_GENERAL,VK_IMAGE_ASPECT_COLOR_BIT);
 if(!d.shared->Release(command,3)){d.processorBlocked=true;return VK_ERROR_INITIALIZATION_FAILED;}
 d.generatedReady=false;++d.generatedCopies;return VK_SUCCESS;
}
VkResult SharedFrame(Device& d,const VkObservedPresent& observed,VkPresentInfoKHR& present,Depth& depth,uint64_t newest,bool estimated,bool bgra,const CapturedConsumer& consume){
 const auto frameStart=NativeGuides::NowMs();
 status.transportTimings.clear();
 auto& shared=*d.shared;const auto& r=observed.request;
 if(d.sharedSerial>=UINT64_MAX-1){d.processorBlocked=true;status.reason="Shared transport timeline exhausted";return VK_SUCCESS;}
 const bool initial=d.sharedSerial==0;const auto previous=d.sharedSerial;
 if(!Begin(d)){status.reason="Shared input recording unavailable";return VK_SUCCESS;}
 const auto serial=++d.sharedSerial;
 const bool timed=BeginCaptureTiming(d);
 d.sharedIdle=false;
 for(unsigned i=0;i<(initial?shared.Count():2u);++i)if(!shared.Acquire(d.command,i,initial)){d.processorBlocked=true;status.reason="Shared input ownership refused";return VK_SUCCESS;}
 Move(d,d.command,observed.image,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
 VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={r.extent.width,r.extent.height,1};
 d.CmdCopyImageToBuffer(d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,shared.Buffer(0),1,&region);
 VkBufferMemoryBarrier ready{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER};ready.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;ready.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
 ready.srcQueueFamilyIndex=ready.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;ready.buffer=depth.buffer.buffer;ready.size=VK_WHOLE_SIZE;
 d.CmdPipelineBarrier(d.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,1,&ready,0,nullptr);
 VkBufferCopy copy{0,0,(uint64_t(depth.extent.width)*depth.extent.height*DepthBytes(depth.format)+3)&~3ull};
 d.CmdCopyBuffer(d.command,depth.buffer.buffer,shared.Buffer(1),1,&copy);
 Move(d,d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_ASPECT_COLOR_BIT);
 for(unsigned i=0;i<(initial?shared.Count():2u);++i)if(!shared.Release(d.command,i)){d.processorBlocked=true;return VK_SUCCESS;}
 if(timed)d.CmdWriteTimestamp(d.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,d.timestamps,1);
 const auto start=NativeGuides::NowMs();
 const auto submitted=Submit(d,observed.queue,&present,shared.InputSemaphore(),serial,initial?VK_NULL_HANDLE:shared.OutputSemaphore(),previous,false);
 status.readbackMs=NativeGuides::NowMs()-start;status.unpackMs=0;status.colorCached=status.depthCached=false;
 if(submitted!=VK_SUCCESS){status.reason="Shared Vulkan input completion uncertain";return submitted;}
 d.lastSubmission=newest;
 for(auto& item:d.depths){const auto proof=VulkanNrRecordings().ProducerProof(item.use);if(proof&&proof->submission.value<=newest)item.consumed=true;}
 const bool reset=d.resetPending||!d.captures||d.lastCaptureFrame+1!=d.frame||d.previousEstimated!=estimated;
 d.resetPending=false;d.previousEstimated=estimated;d.lastCaptureFrame=d.frame;if(estimated)++status.estimatedAssociations;++status.captured;
 NativeGuides::Capture capture{d.luid,reinterpret_cast<uintptr_t>(observed.swapchain),r.swapchainGeneration,++d.captures,r.extent.width,r.extent.height,reset,depth.inverted,{},{}};
 const auto captureMs=NativeGuides::NowMs();capture.deltaMs=d.lastCaptureMs?captureMs-d.lastCaptureMs:16.667;d.lastCaptureMs=captureMs;
 const unsigned bits=DepthBytes(depth.format)==2?16u:(depth.format==VK_FORMAT_D32_SFLOAT||depth.format==VK_FORMAT_D32_SFLOAT_S8_UINT)?32u:24u;
 NativeGuides::SharedCapture packet{shared.Image(0).resource,shared.Image(1).resource,shared.Image(d.vulkanRenderer?0:2).resource,shared.InputFence(),shared.OutputFence(),serial,bits,bgra};
 packet.depthWidth=depth.extent.width;packet.depthHeight=depth.extent.height;
 d.generatedReady=false;
 if(!d.vulkanRenderer&&shared.Count()==4){packet.generated=shared.Image(3).resource;packet.generatedReady=&d.generatedReady;}
 if(d.vulkanRenderer){
  packet.guideDepth=shared.Image(2).resource;packet.guideMotion=shared.Image(3).resource;
  packet.consumeGuides=[&](const NativeGuides::ExportedGuides& facts,std::string& detail){
   if(!consume||!d.capturedImages){detail="Captured Vulkan consumer unavailable";return NativeGuides::Outcome::Unavailable;}
   if(CompleteSubmission(d)!=VK_SUCCESS||!Begin(d)||!shared.Acquire(d.command,2)||!shared.Acquire(d.command,3)||
      Submit(d,observed.queue,nullptr,VK_NULL_HANDLE,0,shared.OutputSemaphore(),serial)!=VK_SUCCESS){detail="Captured guide acquisition completion uncertain";return NativeGuides::Outcome::Unsafe;}
   auto input=std::make_shared<VkCapturedGuideInput>();
   input->identity={reinterpret_cast<uintptr_t>(d.device),r.deviceGeneration,reinterpret_cast<uintptr_t>(observed.swapchain),r.swapchainGeneration,r.acquireGeneration,
     reinterpret_cast<uintptr_t>(observed.image),facts.stream,facts.capture,facts.previous,r.extent.width,r.extent.height,true};
   input->depth=d.capturedImages->images[0];input->motion=d.capturedImages->images[1];input->lease=d.capturedImages;
   input->depthInverted=facts.depthInverted;input->reset=facts.reset;
   input->bind=[device=d.device,family=observed.queueFamily](VkNrUseId use){return use&&VulkanNrRecordings().UseDevice(use)==device&&VulkanNrRecordings().UseFamily(use)==family;};
   const auto result=consume(std::move(input),present,detail);
   if(result==NativeGuides::Outcome::Unsafe)return result;
   // Consumer joined its exact one-time model recording. Restore external
   // ownership before a later D3D12 export can overwrite these textures.
   if(!Begin(d)||!shared.Release(d.command,2)||!shared.Release(d.command,3)||Submit(d,observed.queue,nullptr)!=VK_SUCCESS){detail="Captured guide release completion uncertain";return NativeGuides::Outcome::Unsafe;}
   return result;
  };
 }
 std::string reason;const auto processorStart=NativeGuides::NowMs();const auto outcome=NativeGuides::ProcessShared(capture,packet,reason);
 d.lastOutcome=outcome;
 status.processorMs=NativeGuides::NowMs()-processorStart;
 status.processorReason=reason+" [depth "+std::to_string(depth.extent.width)+"x"+std::to_string(depth.extent.height)+
  " -> "+std::to_string(r.extent.width)+"x"+std::to_string(r.extent.height)+"; nearest normalization]";status.reason=reason;
 const auto drainStart=NativeGuides::NowMs();
 const auto drained=CompleteSubmission(d);const auto drainMs=NativeGuides::NowMs()-drainStart;
 if(drained!=VK_SUCCESS){d.processorBlocked=true;status.reason="Shared input completion uncertain after consumer";return drained;}
 const auto gpuMs=CaptureGpuMs(d);
 status.transportTimings="Shared scheduling (ms): input submit="+std::to_string(status.readbackMs)+
  " input GPU="+(gpuMs<0?std::string("unavailable"):std::to_string(gpuMs))+" input join="+std::to_string(drainMs)+
  " consumer (includes producer wait)="+std::to_string(status.processorMs);
 const auto complete=shared.OutputFence()->GetCompletedValue();
 if(outcome==NativeGuides::Outcome::Unsafe||complete==UINT64_MAX||complete<serial){d.processorBlocked=true;status.reason="Shared GPU owner quarantined: "+reason;return VK_SUCCESS;}
 if(outcome!=NativeGuides::Outcome::Delivered){d.sharedIdle=true;return VK_SUCCESS;}
 if(d.vulkanRenderer){d.sharedIdle=true;++status.delivered;status.reason="Captured depth + estimated motion delivered to Vulkan NR";
  status.transportTimings+=" frame total="+std::to_string(NativeGuides::NowMs()-frameStart);return VK_SUCCESS;}
 if(!Begin(d)||!shared.Acquire(d.command,2)){d.processorBlocked=true;status.reason="Shared output recording/ownership unavailable";return VK_SUCCESS;}
 Move(d,d.command,observed.image,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
 d.CmdCopyBufferToImage(d.command,shared.Buffer(2),observed.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 Move(d,d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_ASPECT_COLOR_BIT);
 if(!shared.Release(d.command,2)){d.processorBlocked=true;return VK_SUCCESS;}
 const auto copyStart=NativeGuides::NowMs();
 const auto copied=Submit(d,observed.queue,nullptr,VK_NULL_HANDLE,0,shared.OutputSemaphore(),serial);
 status.transportTimings+=" copyback/wait="+std::to_string(NativeGuides::NowMs()-copyStart)+" frame total="+std::to_string(NativeGuides::NowMs()-frameStart);
 if(copied!=VK_SUCCESS){d.resetPending=true;status.reason="Shared output copyback completion uncertain";return copied;}
 if(d.generatedReady){d.generatedFrame=NativeFg::Frame(observed.swapchain);
  if(!NativeFg::Publish(observed.swapchain,RecordGenerated,&d)){d.generatedReady=false;d.resetPending=true;}
 }
 d.sharedIdle=true;++status.delivered;status.reason=estimated?"Native depth + estimated motion delivered (shared GPU; estimated offscreen association)":"Native depth + estimated motion delivered (shared GPU; exact association)";
 return VK_SUCCESS;
}

}

void SetCaptureActive(bool active) noexcept {
 active=active&&(NativeGuides::SelectedSource()==0||NativeGuides::SelectedSource()==2);
 const bool wasActive=captureActive.exchange(active,std::memory_order_relaxed);
 if(!active){
  std::lock_guard lock(mutex);
  // A paused hook skips BeforePresent, so its frame serial does not advance.
  // Invalidate selection without retiring any pending GPU-owned allocation.
  for(auto& [key,d]:devices){(void)key;
   if(wasActive){for(auto& depth:d->depths)depth.consumed=true;d->directions.clear();d->resetPending=true;}
   // Keep ordinary retirement progressing during a gated Present. Collect
   // releases only real completed, non-replayable uses; never waits or resets.
   Collect(*d);
   d->effective.guideReady=d->effective.modelPreparing=d->effective.outputValid=0;d->effective.stage=PreparedGuides::Stage::Idle;
   d->effective.updatedTickMs=GetTickCount64();PreparedGuides::PublishStatusV2(d->effective);}
  if(wasActive)scopes.clear();
 }
}
bool CanYieldOutput(std::string& reason){
 std::lock_guard lock(mutex);
 const auto refuse=[&](const char* text){reason=text;return false;};
 if(captureActive.load())return refuse("Vulkan capture is still enabled");
 for(const auto& [_,owner]:devices){const auto& d=*owner;
  if(d.unsafe||d.processorBlocked)return refuse("Vulkan capture completion is quarantined");
  if(d.pending||!d.sharedIdle)return refuse("Vulkan capture submission remains pending");
  for(const auto& depth:d.depths)
   if(!VulkanNrRecordings().RecordingReleased(depth.use)||!VulkanNrRecordings().Reusable(depth.use))
    return refuse("Vulkan depth recording completion or release remains pending");
  if(d.capturedImages&&d.capturedImages.use_count()!=1)return refuse("Vulkan captured guide lease remains active");
  // Shared consumers and generated-frame callbacks retain independent model
  // owners. This observer cannot prove those owners idle by reading counters.
  if(d.shared||d.captures)return refuse("Vulkan model or frame-generation owner retirement is not verified");
 }
 reason.clear();return true;
}
bool CanSwitchInput(std::string& reason){
 // Keep allocations and handles alive. Global model/executor and processor
 // consumers are proved separately, without holding this capture mutex.
 if(!VulkanNrRecordings().CanYieldOutput(reason))return false;
 std::unique_lock lock(mutex,std::try_to_lock);
 const auto refuse=[&](const char* text){reason=text;return false;};
 if(!lock.owns_lock())return refuse("Vulkan capture owner is busy");
 if(captureActive.load())return refuse("Vulkan capture is still enabled");
 for(const auto& [_,owner]:devices){const auto& d=*owner;
  if(d.unsafe||d.processorBlocked)return refuse("Vulkan capture completion is quarantined");
  if(d.pending||!d.sharedIdle)return refuse("Vulkan capture submission remains pending");
  if(d.capturedImages&&d.capturedImages.use_count()!=1)return refuse("Vulkan captured guide lease remains active");
 }
 reason.clear();return true;
}
bool Enabled() noexcept { return !injecting&&!NativeFg::Internal()&&NativeGuides::ObserveBuiltIn(); }
Snapshot Status(){std::lock_guard lock(mutex);auto out=status;out.selected=Enabled();
 if(out.selected){
  out.reason+="\nNative observations: passes="+std::to_string(passObservations)+" framebuffers="+std::to_string(framebufferObservations)+" views="+std::to_string(viewObservations);
  if(!rejections.empty())out.reason+="\nCapture refusals (cumulative): ";
  bool first=true;for(const auto& [reason,count]:rejections){if(!first)out.reason+="; ";first=false;out.reason+=reason+"="+std::to_string(count);}
  if(!out.processorReason.empty())out.reason+="\nLast processor result: "+out.processorReason+
   std::string("\nLast frame timings (ms): ")+(out.transportTimings.empty()?"Vulkan capture=":"Vulkan input submit=")+std::to_string(out.readbackMs)+" CPU unpack="+std::to_string(out.unpackMs)+" guides/NR="+std::to_string(out.processorMs)+" | CPU cached readback: color="+(out.colorCached?"yes":"no")+" depth="+(out.depthCached?"yes":"no");
  if(!out.transportTimings.empty())out.reason+="\n"+out.transportTimings;
  uint64_t memory=0,retained=0;for(const auto& [key,d]:devices){(void)key;memory+=d->allocated;retained+=d->depths.size();}
  out.reason+="\nCapture buffer memory bytes="+std::to_string(memory)+" | Retained depth candidates="+std::to_string(retained);
  std::uint64_t generated=0;for(const auto& [key,d]:devices){(void)key;generated+=d->generatedCopies;}
  if(NativeFg::Selected())out.reason+="\nGenerated output copies recorded "+std::to_string(generated);
  for(const auto& [key,d]:devices){(void)key;if(!d->transportReason.empty())out.reason+="\n"+d->transportReason;}
  if(!imageDetails.empty())out.reason+="\n"+imageDetails;
 }
 return out;}
void Unavailable(const char* reason){std::lock_guard lock(mutex);status.reason=reason;for(auto& [handle,device]:devices){(void)handle;device->resetPending=true;
 device->effective.guideReady=device->effective.modelPreparing=device->effective.outputValid=0;device->effective.stage=PreparedGuides::Stage::Waiting;
 device->effective.updatedTickMs=GetTickCount64();strncpy_s(device->effective.reason,reason,_TRUNCATE);PreparedGuides::PublishStatusV2(device->effective);}}
void AfterPresent(VkDevice device,VkResult result){
 if(!Enabled()||result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)return;
 std::lock_guard lock(mutex);auto it=devices.find(device);if(it!=devices.end()){
  it->second->resetPending=true;auto& effective=it->second->effective;
  effective.guideReady=effective.modelPreparing=effective.outputValid=0;effective.stage=PreparedGuides::Stage::Waiting;
  strncpy_s(effective.reason,"Native copyback history reset after unsuccessful original Present",_TRUNCATE);
  effective.updatedTickMs=GetTickCount64();PreparedGuides::PublishStatusV2(effective);
 }
 status.reason="Native copyback history reset after unsuccessful original Present";
}
void ModelDimensions(const VkObservedPresent& observed,unsigned width,unsigned height){
 std::lock_guard lock(mutex);auto it=devices.find(observed.device);if(it==devices.end())return;
 auto& s=it->second->effective;
 if(!width||!height||!s.outputValid||s.session!=reinterpret_cast<uintptr_t>(observed.swapchain)||
    s.generation!=observed.request.swapchainGeneration||s.candidateId!=observed.request.acquireGeneration)return;
 s.workWidth=width;s.workHeight=height;PreparedGuides::PublishStatusV2(s);
}
void DeviceCreated(VkDevice device,VkPhysicalDevice physical,PFN_vkGetDeviceProcAddr get,PFN_vkGetPhysicalDeviceProperties2 getProperties) {
 if(!Enabled()||!device||!get)return;
 auto d=std::make_unique<Device>();d->device=device;d->physical=physical;
#define LOAD(Name) d->Name=reinterpret_cast<PFN_vk##Name>(get(device,"vk" #Name));
 NATIVE_FUNCTIONS(LOAD)
#undef LOAD
 if(!d->CreateRenderPass2)d->CreateRenderPass2=reinterpret_cast<PFN_vkCreateRenderPass2>(get(device,"vkCreateRenderPass2KHR"));
 if(!d->CreateBuffer||!d->CmdCopyImageToBuffer||!d->WaitForFences||!d->QueueSubmit)return;
 vkGetPhysicalDeviceMemoryProperties(physical,&d->memory);
 VkPhysicalDeviceIDProperties id{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES};
 VkPhysicalDeviceProperties2 properties{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};properties.pNext=&id;
 if(getProperties){getProperties(physical,&properties);if(id.deviceLUIDValid)std::memcpy(&d->luid,id.deviceLUID,sizeof(d->luid));}
 d->timestampPeriod=properties.properties.limits.timestampPeriod;
 uint32_t familyCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physical,&familyCount,nullptr);
 if(familyCount&&familyCount<=256){std::vector<VkQueueFamilyProperties> families(familyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(physical,&familyCount,families.data());
  for(const auto& family:families)d->timestampBits.push_back(family.timestampValidBits);}
 auto& td=d->transportDispatch;td.getProperties2=getProperties;
 HMODULE loader=GetModuleHandleW(L"vulkan-1.dll");
#define PHYSICAL(Field,Name) td.Field=reinterpret_cast<PFN_vk##Name>(loader?GetProcAddress(loader,"vk" #Name):nullptr)
 PHYSICAL(getImageFormatProperties2,GetPhysicalDeviceImageFormatProperties2);PHYSICAL(getExternalSemaphoreProperties,GetPhysicalDeviceExternalSemaphoreProperties);
 PHYSICAL(getExternalBufferProperties,GetPhysicalDeviceExternalBufferProperties);PHYSICAL(getMemoryProperties,GetPhysicalDeviceMemoryProperties);
#undef PHYSICAL
#define TRANSPORT(Field,Name) td.Field=reinterpret_cast<PFN_vk##Name>(get(device,"vk" #Name))
 TRANSPORT(getHandleProperties,GetMemoryWin32HandlePropertiesKHR);TRANSPORT(createImage,CreateImage);TRANSPORT(destroyImage,DestroyImage);
 TRANSPORT(getImageMemoryRequirements,GetImageMemoryRequirements);TRANSPORT(allocateMemory,AllocateMemory);TRANSPORT(freeMemory,FreeMemory);TRANSPORT(bindImageMemory,BindImageMemory);
 TRANSPORT(createSemaphore,CreateSemaphore);TRANSPORT(destroySemaphore,DestroySemaphore);TRANSPORT(importSemaphore,ImportSemaphoreWin32HandleKHR);
 TRANSPORT(barrier,CmdPipelineBarrier);TRANSPORT(copyImage,CmdCopyImage);TRANSPORT(createBuffer,CreateBuffer);TRANSPORT(destroyBuffer,DestroyBuffer);
 TRANSPORT(getBufferMemoryRequirements,GetBufferMemoryRequirements);TRANSPORT(bindBufferMemory,BindBufferMemory);
#undef TRANSPORT
 d->generation=VkNrCompletionDeviceGeneration(device);
 std::lock_guard lock(mutex);devices.emplace(device,std::move(d));status.reason="Native capture waiting for a stored depth attachment";
}
void DeviceDestroyed(VkDevice device) {
 std::lock_guard lock(mutex);auto it=devices.find(device);if(it==devices.end())return;
 auto& d=*it->second;Collect(d);
 d.effective.guideReady=d.effective.modelPreparing=d.effective.outputValid=d.effective.creationReady=0;
 strncpy_s(d.effective.reason,"Native Vulkan device destroyed",_TRUNCATE);
 d.effective.updatedTickMs=GetTickCount64();d.effective.stage=PreparedGuides::Stage::Idle;PreparedGuides::PublishStatusV2(d.effective);
 // These handles are only freed here if our exact private submissions completed.
 // Recorded/in-flight depth buffers remain device children until real destruction.
 if(!d.unsafe&&!d.pending){Free(d,d.color);if(d.fence)d.DestroyFence(device,d.fence,nullptr);if(d.pool)d.DestroyCommandPool(device,d.pool,nullptr);if(d.timestamps)d.DestroyQueryPool(device,d.timestamps,nullptr);}
 if(d.capturedImages){if(d.unsafe||d.pending||d.processorBlocked||!d.sharedIdle)d.capturedImages->device=VK_NULL_HANDLE;d.capturedImages.reset();}
 if(d.shared&&!d.unsafe&&!d.pending&&!d.processorBlocked&&d.sharedIdle)d.shared->Retire({true,true,true,true});
 for(auto& depth:d.depths)VulkanNrRecordings().ReleaseUse(depth.use);
 devices.erase(it);std::erase_if(views,[&](const auto& v){return v.second.device==device;});
 std::erase_if(passes,[&](const auto& v){return v.second.device==device;});
 std::erase_if(framebuffers,[&](const auto& v){return v.second.device==device;});
 std::erase_if(scopes,[&](const auto& value){return VulkanNrRecordings().CommandDevice(value.first)==device;});
}
void ViewCreated(VkDevice device,const VkImageViewCreateInfo& ci,VkImageView view) {
 if(!Enabled())return;
 const auto image=VulkanNrImageFacts().Image(device,ci.image);
 std::lock_guard lock(mutex);++viewObservations;
 bool chainKnown=true;unsigned chainLength=0;
 for(auto* n=static_cast<const VkBaseInStructure*>(ci.pNext);n;n=n->pNext)
  if(++chainLength>1||n->sType!=VK_STRUCTURE_TYPE_IMAGE_VIEW_USAGE_CREATE_INFO){chainKnown=false;break;}
 if(!chainKnown||(ci.viewType!=VK_IMAGE_VIEW_TYPE_2D&&ci.viewType!=VK_IMAGE_VIEW_TYPE_2D_ARRAY)){Reject("View extension or non-2D view");return;}
 if(views.size()>=4096){Reject("View tracking limit");return;}
 View v{device,ci,image?image->generation:0};v.info.pNext=nullptr;views[{device,view}]=v;
}
void ViewDestroyed(VkDevice device,VkImageView view){if(!Enabled())return;std::lock_guard lock(mutex);views.erase({device,view});}
VkImageCreateInfo PrepareImage(VkDevice device,const VkImageCreateInfo& original) {
 auto ci=original;if(!Enabled()||!FormatChain(ci.pNext)||(ci.flags&~CopyFlags)||ci.imageType!=VK_IMAGE_TYPE_2D||!ci.arrayLayers||!ci.mipLevels||
  ci.samples!=VK_SAMPLE_COUNT_1_BIT||!(ci.usage&VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)||!DepthBytes(ci.format)||
  (ci.usage&VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT))return ci;
 const auto physical=GetVulkanPresentRegistry().PhysicalDevice(device);if(!physical)return ci;
 VkImageFormatProperties properties{};const auto usage=ci.usage|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
 if(vkGetPhysicalDeviceImageFormatProperties(physical,ci.format,ci.imageType,ci.tiling,usage,ci.flags,&properties)==VK_SUCCESS&&
    ci.extent.width<=properties.maxExtent.width&&ci.extent.height<=properties.maxExtent.height&&
    ci.mipLevels<=properties.maxMipLevels&&ci.arrayLayers<=properties.maxArrayLayers)ci.usage=usage;
 return ci;
}
void BeginLegacy(VkCommandBuffer cb,const VkRenderPassBeginInfo* begin) {
 if(!Enabled())return;std::lock_guard lock(mutex);scopes.erase(cb);++status.scopesSeen;ScopeHistoryGuard history{cb};
 if(!begin||begin->pNext){Reject("Render begin extension or missing info");return;}
 if(scopes.size()>=256){Reject("Render scope tracking limit");return;}
 const auto device=VulkanNrRecordings().CommandDevice(cb);
 auto p=passes.find({device,begin->renderPass});auto fb=framebuffers.find({device,begin->framebuffer});
 // Vulkan permits a framebuffer created with a different compatible pass.
 // Attachment operations/layouts come from the pass actually being begun.
 if(p==passes.end()){Reject("Render pass not observed");return;}
 if(fb==framebuffers.end()){Reject("Framebuffer not observed or unsupported");return;}
 if(!p->second.valid){Reject(p->second.rejection);return;}
 if(fb->second.layers!=1||fb->second.device!=p->second.device){Reject("Framebuffer layers or device mismatch");return;}
 const auto index=p->second.attachment;if(index>=fb->second.views.size()){Reject("Depth attachment index unavailable");return;}
 std::vector<VkImage> colorImages;
 for(const auto attachment:p->second.colors){
  if(attachment==VK_ATTACHMENT_UNUSED)continue;
  if(attachment>=fb->second.views.size()){Reject("Color attachment index unavailable");return;}
  const auto color=views.find({device,fb->second.views[attachment]});if(color==views.end()||color->second.device!=p->second.device){Reject("Color view not observed or device mismatch");return;}
  colorImages.push_back(color->second.info.image);
 }
 bool inverted=false;float clear=-1.f;if(index<begin->clearValueCount&&begin->pClearValues)clear=begin->pClearValues[index].depthStencil.depth;
 const bool known=Orientation(p->second.depth.loadOp,clear,inverted)||
  (p->second.depth.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&p->second.depth.initialLayout!=VK_IMAGE_LAYOUT_UNDEFINED&&PriorDirection(cb,fb->second.views[index],inverted));
 scopes[cb]={fb->second.views[index],p->second.depth.finalLayout,begin->renderArea,VulkanNrRecordings().Incarnation(cb),0,true,inverted,std::move(colorImages)};
 scopes[cb].directionKnown=known;
}
void BeginDynamic(VkCommandBuffer cb,const VkRenderingInfo* info) {
 if(!Enabled())return;std::lock_guard lock(mutex);scopes.erase(cb);++status.scopesSeen;ScopeHistoryGuard history{cb};
 if(!info||info->pNext||info->flags||info->viewMask||info->layerCount!=1||!info->pDepthAttachment||scopes.size()>=256){Reject("Dynamic rendering structure unsupported");return;}
 if(info->colorAttachmentCount>32){Reject("Dynamic color attachment limit");return;}
 std::vector<VkImage> colorImages;
 for(uint32_t i=0;i<info->colorAttachmentCount;++i){
  if(!info->pColorAttachments[i].imageView)continue;
  const auto color=views.find({VulkanNrRecordings().CommandDevice(cb),info->pColorAttachments[i].imageView});if(color==views.end()||info->pColorAttachments[i].resolveMode!=VK_RESOLVE_MODE_NONE){Reject("Dynamic color view unavailable or resolve unsupported");return;}
  colorImages.push_back(color->second.info.image);
 }
 const auto& a=*info->pDepthAttachment;bool inverted=false;
 if(a.pNext||!a.imageView||a.resolveMode!=VK_RESOLVE_MODE_NONE){Reject("Dynamic depth extension, view or resolve unsupported");return;}
 if(a.storeOp!=VK_ATTACHMENT_STORE_OP_STORE){Reject("Dynamic depth store operation discards contents");return;}
 const bool known=Orientation(a.loadOp,a.clearValue.depthStencil.depth,inverted)||
  (a.loadOp==VK_ATTACHMENT_LOAD_OP_LOAD&&PriorDirection(cb,a.imageView,inverted));
 const auto view=views.find({VulkanNrRecordings().CommandDevice(cb),a.imageView});if(view==views.end()){Reject("Dynamic depth view not observed");return;}
 if(Stencil(view->second.info.format)&&(!CombinedLayout(a.imageLayout)||!info->pStencilAttachment||
  info->pStencilAttachment->imageView!=a.imageView||info->pStencilAttachment->imageLayout!=a.imageLayout)){Reject("Dynamic stencil layout unsupported");return;}
 scopes[cb]={a.imageView,a.imageLayout,info->renderArea,VulkanNrRecordings().Incarnation(cb),0,true,inverted,std::move(colorImages)};
 scopes[cb].directionKnown=known;
}
void Draw(VkCommandBuffer cb) {if(!Enabled())return;std::lock_guard lock(mutex);auto it=scopes.find(cb);if(it!=scopes.end())++it->second.draws;}
void ClearAttachments(VkCommandBuffer cb,uint32_t count,const VkClearAttachment* attachments,uint32_t rectCount,const VkClearRect* rects) {
 if(!Enabled()||!attachments)return;std::lock_guard lock(mutex);auto it=scopes.find(cb);if(it==scopes.end())return;
 for(uint32_t i=0;i<count;++i)if(attachments[i].aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT){
  auto& scope=it->second;bool inverted=false;
  const bool full=rectCount==1&&rects&&!rects[0].rect.offset.x&&!rects[0].rect.offset.y&&
   rects[0].rect.extent.width==scope.area.extent.width&&rects[0].rect.extent.height==scope.area.extent.height&&rects[0].baseArrayLayer==0&&rects[0].layerCount==1;
  const bool known=full&&Orientation(VK_ATTACHMENT_LOAD_OP_CLEAR,attachments[i].clearValue.depthStencil.depth,inverted);
  // A full clear before any draw can establish the convention. A conflicting
  // later clear must not relabel already recorded geometry.
  if(scope.draws&&(!known||!scope.directionKnown||inverted!=scope.inverted))scope.valid=false;
  scope.directionKnown=known;scope.inverted=inverted;
 }
}
void InvalidateDepthHistory(VkCommandBuffer cb){if(!Enabled())return;std::lock_guard lock(mutex);ForgetDirections(cb);
 auto scope=scopes.find(cb);if(scope!=scopes.end()){scope->second.valid=false;scope->second.directionKnown=false;}}
void Barrier(VkCommandBuffer cb,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t count,const VkImageMemoryBarrier* barriers){
 if(!Enabled())return;for(uint32_t i=0;barriers&&i<count;++i)if(barriers[i].oldLayout==VK_IMAGE_LAYOUT_UNDEFINED){InvalidateDepthHistory(cb);return;}
}
void Barrier(VkCommandBuffer cb,const VkDependencyInfo* info){
 if(!Enabled()||!info)return;if(info->pNext){InvalidateDepthHistory(cb);return;}
 for(uint32_t i=0;info->pImageMemoryBarriers&&i<info->imageMemoryBarrierCount;++i)if(info->pImageMemoryBarriers[i].oldLayout==VK_IMAGE_LAYOUT_UNDEFINED){InvalidateDepthHistory(cb);return;}
}
void ClearDepthImage(VkCommandBuffer cb,VkImage image,VkImageLayout,const VkClearDepthStencilValue* value,uint32_t count,const VkImageSubresourceRange* ranges){
 if(!Enabled())return;std::lock_guard lock(mutex);ForgetDirections(cb);if(!value||!ranges)return;
 const auto device=VulkanNrRecordings().CommandDevice(cb);auto d=devices.find(device);const auto fact=VulkanNrImageFacts().Image(device,image);
 if(d==devices.end()||!fact||!DepthBytes(fact->format))return;
 Collect(*d->second);bool inverted=false;const bool known=Orientation(VK_ATTACHMENT_LOAD_OP_CLEAR,value->depth,inverted);
 for(uint32_t i=0;i<count;++i){const auto& r=ranges[i];if(!(r.aspectMask&VK_IMAGE_ASPECT_DEPTH_BIT)||r.baseMipLevel>=fact->mips||r.baseArrayLayer>=fact->layers)continue;
  const auto levels=r.levelCount==VK_REMAINING_MIP_LEVELS?fact->mips-r.baseMipLevel:r.levelCount;
  const auto layers=r.layerCount==VK_REMAINING_ARRAY_LAYERS?fact->layers-r.baseArrayLayer:r.layerCount;
  if(levels>fact->mips-r.baseMipLevel||layers>fact->layers-r.baseArrayLayer||uint64_t(levels)*layers>256){ForgetDirections(cb);Reject("Depth clear range exceeds tracked history");return;}
  for(uint32_t mip=r.baseMipLevel;mip<r.baseMipLevel+levels&&mip<32;++mip){
   const VkExtent2D extent{(std::max)(1u,fact->extent.width>>mip),(std::max)(1u,fact->extent.height>>mip)};
   if(d->second->lastExtent.width&&!CompatibleDepthExtent(extent,d->second->lastExtent))continue;
   for(uint32_t layer=r.baseArrayLayer;layer<r.baseArrayLayer+layers;++layer)RecordDirection(*d->second,cb,image,fact->generation,mip,layer,known,inverted);
  }
 }
}
void End(VkCommandBuffer cb) {
 if(!Enabled())return;InjectionScope injection;std::lock_guard lock(mutex);auto it=scopes.find(cb);if(it==scopes.end())return;
 const auto scope=it->second;scopes.erase(it);if(!captureActive.load(std::memory_order_relaxed))return;auto& owner=VulkanNrRecordings();
 const auto v=views.find({owner.CommandDevice(cb),scope.view});if(v==views.end()){Reject("Depth view not observed");return;}
 const auto device=owner.CommandDevice(cb);auto di=devices.find(device);if(di==devices.end()||v->second.device!=device){Reject("Depth device or command device not observed");return;}
 auto& d=*di->second;if(d.processorBlocked)return;if(d.unsafe){Reject("Device completion quarantined");return;}Collect(d);
 const auto& view=v->second.info;const auto image=VulkanNrImageFacts().Image(device,view.image);
 if(!image||image->generation!=v->second.generation){Reject("Depth image not observed or stale generation");return;}
 const auto rejectImage=[&](const char* reason){Reject(reason);imageDetails="Last refused depth: format="+std::to_string(image->format)+" flags="+std::to_string(image->flags)+
  " usage="+std::to_string(image->usage)+" samples="+std::to_string(image->samples)+" image="+std::to_string(image->extent.width)+"x"+std::to_string(image->extent.height)+
  " mips="+std::to_string(image->mips)+" layers="+std::to_string(image->layers)+" viewMip="+std::to_string(view.subresourceRange.baseMipLevel)+" viewLayer="+std::to_string(view.subresourceRange.baseArrayLayer)+
  " area="+std::to_string(scope.area.extent.width)+"x"+std::to_string(scope.area.extent.height)+" sharing="+std::to_string(image->sharing)+
  " presentation="+std::to_string(d.lastExtent.width)+"x"+std::to_string(d.lastExtent.height);};
 uint32_t mip=0,layer=0;VkExtent2D extent{};
 if(!SingleRange(*image,view,mip,layer,extent)){rejectImage("Depth view must select one matching mip and layer");return;}
 const bool whole=!scope.area.offset.x&&!scope.area.offset.y&&scope.area.extent.width==extent.width&&scope.area.extent.height==extent.height;
 const bool current=scope.incarnation==owner.Incarnation(cb);
 if(current)
  RecordDirection(d,cb,view.image,image->generation,mip,layer,whole&&scope.valid&&scope.directionKnown,scope.inverted);
 if(!scope.valid||!scope.draws||!current||!whole||scope.layout==VK_IMAGE_LAYOUT_UNDEFINED||scope.layout==VK_IMAGE_LAYOUT_PREINITIALIZED){rejectImage("Empty draws, conflicting clear, stale scope or partial render area");return;}
 if(!image->chainKnown){rejectImage("Depth image extension unsupported");return;}
 if(!(image->usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT)){rejectImage("Depth image lacks transfer-source usage");return;}
 if(image->flags&~CopyFlags){rejectImage("Depth image has unsupported creation flags");return;}
 if(image->samples!=VK_SAMPLE_COUNT_1_BIT){rejectImage("Depth image requires multisample resolve");return;}
 if(image->type!=VK_IMAGE_TYPE_2D||!DepthBytes(image->format)){rejectImage("Depth image format or dimension unsupported");return;}
 if(image->sharing!=VK_SHARING_MODE_EXCLUSIVE){
  const auto family=owner.CommandFamily(cb);
  if(std::find(image->families.begin(),image->families.end(),family)==image->families.end()){rejectImage("Concurrent depth image excludes recording queue family");return;}
 }
 if(Stencil(image->format)&&!CombinedLayout(scope.layout)){Reject("Separate stencil layout unsupported");return;}
 if(d.lastExtent.width&&!CompatibleDepthExtent(extent,d.lastExtent)){rejectImage("Depth aspect or bounded extent incompatible with presentation");return;}
 const int directionOverride=DirectionOverride();
 if(!scope.directionKnown&&directionOverride<0){Reject("Depth direction unknown: select Forward or Reversed, or use an observed clear");return;}
 // Source ranking is separate from this version's depth convention. A later
 // qualified copy of the same source keeps its evidence rank; it must never
 // lose to older contents merely because its own recording uses LOAD.
 const bool sourceObserved=scope.directionKnown||std::any_of(d.depths.begin(),d.depths.end(),[&](const auto& prior){
  return prior.serial==d.frame&&prior.observedSource&&prior.image==view.image&&prior.imageGeneration==image->generation&&prior.mip==mip&&prior.layer==layer&&
         prior.recording==cb&&prior.incarnation==scope.incarnation;
 });
 // A manual convention is not evidence that this is the scene source. Keep
 // at most one override-only fallback copy per interval, preserving budget
 // for sources backed by an observed clear. Selection ranks that evidence.
 if(!sourceObserved){
  const auto prior=std::find_if(d.depths.begin(),d.depths.end(),[&](const auto& item){return item.serial==d.frame&&!item.observedSource;});
  if(prior!=d.depths.end()){
   // Even the same image may now contain later geometry. Without another
   // qualified copy we must not present the earlier fallback as current.
   if(d.overrideConflictSerial!=d.frame){d.suppressedDepths.clear();d.suppressionOverflow=false;}
   d.overrideConflictSerial=d.frame;
   // Recording order need not equal submission order. Keep bounded tombstones
   // for the interval, including sources whose clear-backed copy arrives later.
   const auto suppressed=std::find_if(d.suppressedDepths.begin(),d.suppressedDepths.end(),[&](const auto& item){
    return item.image==view.image&&item.generation==image->generation&&item.mip==mip&&item.layer==layer;
   });
   if(suppressed==d.suppressedDepths.end()){
    if(d.suppressedDepths.size()<MaxCaptures)d.suppressedDepths.push_back({view.image,image->generation,mip,layer});
    else d.suppressionOverflow=true;
   }
   Reject("Override-only depth fallback already captured this interval");return;
  }
 }
 if(!CaptureExtent(extent)){rejectImage("Native depth extent exceeds CPU processor limits");return;}
 // Reserve the current color before admitting more depth buffers. At 4K,
 // four depth copies otherwise exhaust the 128 MiB budget before Present.
 const auto colorExtent=d.lastExtent.width?d.lastExtent:extent;
 const bool gpuDepth=NativeGuides::CaptureDepthGpuOnly(d.shared!=nullptr);
 if(!gpuDepth&&!EnsureColor(d,uint64_t(colorExtent.width)*colorExtent.height*4)){Reject("Color readback reservation unavailable or bounded budget reached");return;}
 if(d.depths.size()>=MaxCaptures){Reject("Capture retention limit: pending command lifetimes");return;}
 auto use=owner.ReserveCopy(cb,d.generation);if(!use){Reject("Depth recording lifetime or queue family not qualified");return;}
 if(!owner.SingleUsePrimary(*use)){owner.CancelBeforeRecording(*use);Reject("Native depth requires an observed one-time primary command buffer");return;}
 Depth depth{};depth.use=*use;depth.image=view.image;depth.color=scope.colors.size()==1?scope.colors[0]:VK_NULL_HANDLE;depth.imageGeneration=image->generation;depth.recording=cb;depth.incarnation=scope.incarnation;
 depth.extent=scope.area.extent;depth.format=image->format;depth.inverted=directionOverride>=0?directionOverride==1:scope.inverted;depth.draws=scope.draws;depth.serial=d.frame;
 depth.mip=mip;depth.layer=layer;depth.directionKnown=scope.directionKnown||directionOverride>=0;depth.observedSource=sourceObserved;
 const uint64_t bytes=uint64_t(depth.extent.width)*depth.extent.height*DepthBytes(depth.format);
 if(!Allocate(d,(bytes+3)&~3ull,depth.buffer,gpuDepth)){owner.CancelBeforeRecording(*use);Reject("Capture memory unavailable or bounded budget reached");return;}
 if(!ReserveVkNrCompletion(device,*use)){Free(d,depth.buffer);owner.CancelBeforeRecording(*use);Reject("Depth completion reservation unavailable");return;}
 owner.RetainUse(*use);
 const auto aspect=VK_IMAGE_ASPECT_DEPTH_BIT|(Stencil(depth.format)?VK_IMAGE_ASPECT_STENCIL_BIT:0);
 Move(d,cb,view.image,scope.layout,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,aspect,mip,layer);
 VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_DEPTH_BIT,mip,layer,1};region.imageExtent={depth.extent.width,depth.extent.height,1};
 // vkCmdCopyImageToBuffer executes inside the real producer's submitted recording.
 d.CmdCopyImageToBuffer(cb,view.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,depth.buffer.buffer,1,&region);
 ToHost(d,cb,depth.buffer);Move(d,cb,view.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,scope.layout,aspect,mip,layer);
 depth.workSerial=owner.CommandSerial(cb);
 if(auto colorImage=VulkanNrImageFacts().Image(device,depth.color))depth.colorGeneration=colorImage->generation;
 unsigned swapchainColors=0;
 for(auto color:scope.colors)if(GetVulkanPresentRegistry().IsSwapchainImage(device,color)){
  depth.swapchainBound=true;++swapchainColors;depth.color=color;
  if(auto context=GetVulkanPresentRegistry().TaggedImageContext(color,VK_FORMAT_UNDEFINED,depth.extent))depth.colorContext=*context;
 }
 if(swapchainColors>1)depth.colorContext={};
 d.depths.push_back(std::move(depth));++status.depthCopies;status.captureReason="Depth copy recorded; awaiting submitted producer and Present";
}

namespace {
bool FullColor(const VkImageSubresourceLayers& s){return s.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&!s.mipLevel&&!s.baseArrayLayer&&s.layerCount==1;}
bool Origin(const VkOffset3D& o){return !o.x&&!o.y&&!o.z;}
void LinkColor(VkCommandBuffer cb,VkImage from,VkImage to,VkExtent3D extent) {
 if(!Enabled()||from==to||extent.depth!=1)return;
 auto& owner=VulkanNrRecordings();const auto device=owner.CommandDevice(cb);const auto serial=owner.CommandSerial(cb);
 std::lock_guard lock(mutex);const auto it=devices.find(device);if(it==devices.end())return;
 const auto source=VulkanNrImageFacts().Image(device,from);if(!source||source->flags||source->samples!=VK_SAMPLE_COUNT_1_BIT||source->layers!=1||source->mips!=1||
  source->extent.width!=extent.width||source->extent.height!=extent.height)return;
 const auto context=GetVulkanPresentRegistry().TaggedImageContext(to,VK_FORMAT_UNDEFINED,{extent.width,extent.height});if(!context)return;
 for(auto& depth:it->second->depths){
  // Only the next observed image work in this same primary incarnation may
  // establish a full, unflipped pixel correspondence. Shader postprocess is
  // deliberately not inferred from draw order, descriptor names, or dimensions.
  if(depth.consumed||depth.color!=from||depth.colorGeneration!=source->generation||
   !owner.UseOnRecording(depth.use,cb)||serial!=depth.workSerial+1||
   depth.extent.width!=extent.width||depth.extent.height!=extent.height)continue;
  depth.color=to;depth.colorContext=*context;depth.swapchainBound=true;depth.workSerial=serial;
 }
}
}
void ColorCopy(VkCommandBuffer cb,VkImage from,VkImageLayout,VkImage to,VkImageLayout,uint32_t count,const VkImageCopy* r){
 if(count==1&&r&&FullColor(r->srcSubresource)&&FullColor(r->dstSubresource)&&Origin(r->srcOffset)&&Origin(r->dstOffset))LinkColor(cb,from,to,r->extent);
}
void ColorCopy(VkCommandBuffer cb,const VkCopyImageInfo2* info){
 if(!info||info->pNext||info->regionCount!=1||!info->pRegions)return;const auto& r=*info->pRegions;
 if(!r.pNext&&FullColor(r.srcSubresource)&&FullColor(r.dstSubresource)&&Origin(r.srcOffset)&&Origin(r.dstOffset))LinkColor(cb,info->srcImage,info->dstImage,r.extent);
}
void ColorBlit(VkCommandBuffer cb,VkImage from,VkImageLayout,VkImage to,VkImageLayout,uint32_t count,const VkImageBlit* r,VkFilter){
 if(count!=1||!r||!FullColor(r->srcSubresource)||!FullColor(r->dstSubresource)||!Origin(r->srcOffsets[0])||!Origin(r->dstOffsets[0]))return;
 const auto a=r->srcOffsets[1],b=r->dstOffsets[1];if(a.x>0&&a.y>0&&a.z==1&&a.x==b.x&&a.y==b.y&&a.z==b.z)LinkColor(cb,from,to,{uint32_t(a.x),uint32_t(a.y),1});
}
void ColorBlit(VkCommandBuffer cb,const VkBlitImageInfo2* info){
 if(!info||info->pNext||info->regionCount!=1||!info->pRegions)return;const auto& r=*info->pRegions;
 if(r.pNext)return;VkImageBlit legacy{r.srcSubresource,{r.srcOffsets[0],r.srcOffsets[1]},r.dstSubresource,{r.dstOffsets[0],r.dstOffsets[1]}};
 ColorBlit(cb,info->srcImage,info->srcImageLayout,info->dstImage,info->dstImageLayout,1,&legacy,info->filter);
}

VkResult BeforePresent(const VkObservedPresent& observed,VkPresentInfoKHR& present,const CapturedConsumer& consume,bool* handled) {
 if(handled)*handled=false;
 if(!Enabled())return VK_SUCCESS;InjectionScope injection;std::lock_guard lock(mutex);
 auto di=devices.find(observed.device);if(di==devices.end()){status.reason="Native Vulkan device observation unavailable";return VK_SUCCESS;}
 auto& d=*di->second;const auto& r=observed.request;const auto& c=observed.capabilities;
 d.lastOutcome=NativeGuides::Outcome::Unavailable;
 // Current-frame facts use the same aggregate as the other built-in APIs.
 // Refusals clear active flags; historical totals remain diagnostic.
 struct FrameStatus {
  Device& device;const VkObservedPresent& observed;uint64_t captured,delivered;
  ~FrameStatus(){
   auto& s=device.effective;s=PreparedGuides::StatusV2{};
   s.updatedTickMs=GetTickCount64();s.sourceApi=0x20000;s.producerBits=64;
   s.producerIdentity=reinterpret_cast<uintptr_t>(&device);s.session=reinterpret_cast<uintptr_t>(observed.swapchain);
   s.generation=observed.request.swapchainGeneration;s.candidateId=observed.request.acquireGeneration;
   s.capture=device.captures;s.inputFrames=status.captured;s.modelCompletions=s.copybackCompletions=status.delivered;
   s.creationReady=!device.unsafe&&!device.processorBlocked;s.restartRequired=device.unsafe||device.processorBlocked;
   const bool frameCaptured=status.captured>captured;
   s.outputValid=status.delivered>delivered;s.modelPreparing=frameCaptured&&device.lastOutcome==NativeGuides::Outcome::Preparing;
   s.guideReady=frameCaptured&&(s.outputValid||s.modelPreparing);
   s.selectedSource=frameCaptured||s.restartRequired?Connections::Source::BuiltIn:Connections::Source::Automatic;
   if(frameCaptured){
    s.effectiveTransport=device.shared?Connections::Transport::GPUOnly:Connections::Transport::CPU;
    s.captureWidth=observed.request.extent.width;s.captureHeight=observed.request.extent.height;
    s.depthOrigin=PreparedGuides::Origin::Observed;s.motionOrigin=PreparedGuides::Origin::Derived;
    if(s.outputValid){s.outputWidth=s.captureWidth;s.outputHeight=s.captureHeight;}
   }
   s.stage=s.outputValid?PreparedGuides::Stage::Delivered:s.modelPreparing?PreparedGuides::Stage::Processing:
    s.restartRequired?PreparedGuides::Stage::Blocked:PreparedGuides::Stage::Waiting;
   strncpy_s(s.reason,status.reason.c_str(),_TRUNCATE);PreparedGuides::PublishStatusV2(s);
  }
 } frameStatus{d,observed,status.captured,status.delivered};
 if(d.unsafe){status.reason="Native capture quarantined after uncertain GPU completion";return VK_ERROR_DEVICE_LOST;}
 if(d.previousQueue&&d.previousQueue!=observed.queue)d.resetPending=true;
 d.previousQueue=observed.queue;d.lastExtent=r.extent;++d.frame;Collect(d);
 if(!captureActive.load(std::memory_order_relaxed)){status.reason="Native capture paused: NR disabled or route inactive";return VK_SUCCESS;}
 if(NativeGuides::SelectedSource()==0&&Connections::NativeUsable()){
  d.resetPending=true;status.reason="Automatic input: game-supplied guides available; built-in capture deferred";return VK_SUCCESS;
 }
 if(d.processorBlocked){status.reason="Native processing stopped after processor refusal; restart required";return VK_SUCCESS;}
 const bool bgra=r.format==VK_FORMAT_B8G8R8A8_UNORM||r.format==VK_FORMAT_B8G8R8A8_SRGB;
 const bool rgba=r.format==VK_FORMAT_R8G8B8A8_UNORM||r.format==VK_FORMAT_R8G8B8A8_SRGB;
 if(!IsVkPresentWaitReplacementChainKnown(present.pNext)||present.swapchainCount!=1||!r.acquiredObserved||r.arrayLayers!=1||r.flags||r.fgKnownActive||!r.fgStateKnown||
  !c.queueObserved||!c.presentQueueAccessQualified||!(c.queueFlags&VK_QUEUE_GRAPHICS_BIT)||!c.swapchainPNextKnown||
  r.colorSpace!=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR||(!bgra&&!rgba)||
  (r.imageUsage&(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT))!=(VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT)) {
  status.reason="Native capture requires acquired single SDR swapchain with graphics-queue transfer rights";return VK_SUCCESS;
 }
 Depth* chosen=nullptr;uint64_t newest=0;bool ambiguous=false,estimated=false;
 uint64_t submitted=0,fresh=0,linked=0;
 for(auto& depth:d.depths){
  if(depth.consumed||depth.serial+1!=d.frame||depth.extent.width!=r.extent.width||depth.extent.height!=r.extent.height)continue;
  const auto proof=VulkanNrRecordings().ProducerProof(depth.use);
  if(!proof||proof->queue!=observed.queue||proof->deviceGeneration!=d.generation)continue;
  ++submitted;if(proof->submission.value<=d.lastSubmission)continue;++fresh;
  if(!depth.directionKnown){status.captureReason="Depth copied; direction unknown: no clear in this recording or explicit override";continue;}
  if(depth.color!=observed.image)continue;
  if(depth.colorContext.swapchain!=observed.swapchain||depth.colorContext.deviceGeneration!=r.deviceGeneration||
   depth.colorContext.swapchainGeneration!=r.swapchainGeneration||depth.colorContext.acquireGeneration!=r.acquireGeneration||
   depth.colorContext.swapchainImageIndex!=r.imageIndex)continue;
  ++linked;
  if(chosen&&chosen->observedSource&&!depth.observedSource)continue;
  if(chosen&&!chosen->observedSource&&depth.observedSource){chosen=nullptr;newest=0;ambiguous=false;}
  if(chosen&&(chosen->image!=depth.image||chosen->imageGeneration!=depth.imageGeneration||chosen->mip!=depth.mip||chosen->layer!=depth.layer||chosen->inverted!=depth.inverted))ambiguous=true;
  if(chosen&&proof->submission.value==newest&&(chosen->recording!=depth.recording||chosen->incarnation!=depth.incarnation))ambiguous=true;
  if(proof->submission.value>=newest){newest=proof->submission.value;chosen=&depth;}
 }
 const auto current=GetVulkanPresentRegistry().TaggedImageContext(observed.image,r.format,r.extent);
 const bool estimatedOwner=GetVulkanPresentRegistry().OnlySwapchain(observed.device,observed.swapchain)&&current&&
  current->swapchain==observed.swapchain&&current->deviceGeneration==r.deviceGeneration&&
  current->swapchainGeneration==r.swapchainGeneration&&current->acquireGeneration==r.acquireGeneration&&current->swapchainImageIndex==r.imageIndex;
 if(!chosen&&!ambiguous&&estimatedOwner){
  // Scene association is a heuristic, not proof of shader/pixel correspondence.
  // Only one distinct full-coverage, same-aspect offscreen depth from this interval
  // and this exact submitted queue is accepted. Never downgrade a stale or
  // different swapchain binding into an estimated match.
  // Preserve the previously accepted matching-size tier. A newly admitted
  // scaled auxiliary buffer must not make that tier ambiguous.
  for(bool matching:{true,false}){
  if(chosen||ambiguous)break;
  for(auto& depth:d.depths){
   if(depth.consumed||depth.swapchainBound||depth.serial+1!=d.frame||
      !CompatibleDepthExtent(depth.extent,r.extent))continue;
   if((depth.extent.width==r.extent.width&&depth.extent.height==r.extent.height)!=matching)continue;
   const auto proof=VulkanNrRecordings().ProducerProof(depth.use);
   if(!proof||proof->queue!=observed.queue||proof->deviceGeneration!=d.generation||proof->submission.value<=d.lastSubmission)continue;
   if(!depth.directionKnown){status.captureReason="Depth copied; direction unknown: no clear in this recording or explicit override";continue;}
   if(chosen&&chosen->observedSource&&!depth.observedSource)continue;
   if(chosen&&!chosen->observedSource&&depth.observedSource){chosen=nullptr;newest=0;ambiguous=false;}
   if(chosen&&(chosen->image!=depth.image||chosen->imageGeneration!=depth.imageGeneration||chosen->mip!=depth.mip||chosen->layer!=depth.layer||chosen->inverted!=depth.inverted))ambiguous=true;
   if(chosen&&proof->submission.value==newest&&(chosen->recording!=depth.recording||chosen->incarnation!=depth.incarnation))ambiguous=true;
   if(proof->submission.value>=newest){newest=proof->submission.value;chosen=&depth;estimated=true;}
  }
  }
 }
 // Ranking may prefer a source, never an obsolete version of that source.
 // Cross-recording evidence is not inherited: refuse when a later qualified
 // copy of the selected subresource has weaker source evidence.
 if(chosen&&chosen->observedSource)for(const auto& depth:d.depths){
  if(depth.consumed||depth.observedSource||depth.serial+1!=d.frame||depth.image!=chosen->image||
     depth.imageGeneration!=chosen->imageGeneration||depth.mip!=chosen->mip||depth.layer!=chosen->layer)continue;
  const auto proof=VulkanNrRecordings().ProducerProof(depth.use);
  if(!proof||proof->queue!=observed.queue||proof->deviceGeneration!=d.generation||proof->submission.value<=d.lastSubmission)continue;
  if(proof->submission.value>newest||(proof->submission.value==newest&&
     (depth.recording!=chosen->recording||depth.incarnation!=chosen->incarnation)))ambiguous=true;
 }
 if(chosen&&d.overrideConflictSerial!=UINT64_MAX&&d.overrideConflictSerial+1==d.frame){
  if(!chosen->observedSource||d.suppressionOverflow||std::any_of(d.suppressedDepths.begin(),d.suppressedDepths.end(),[&](const auto& item){
   return item.image==chosen->image&&item.generation==chosen->imageGeneration&&item.mip==chosen->mip&&item.layer==chosen->layer;
  }))ambiguous=true;
 }
 if(!chosen||ambiguous){
  status.reason=ambiguous?"Native depth ambiguous: multiple scene-depth candidates; original frame preserved":
   "Native depth unavailable: submitted="+std::to_string(submitted)+" fresh="+std::to_string(fresh)+" linked="+std::to_string(linked)+
    (estimatedOwner?". ":"; estimated selection requires one live swapchain and current acquisition. ")+status.captureReason;
  return VK_SUCCESS;
 }
 const uint64_t pixels=uint64_t(r.extent.width)*r.extent.height,bytes=pixels*4;
 if(!CaptureExtent(r.extent)||!Commands(d,observed.queueFamily)){status.reason="Native capture extent or command queue unavailable";return VK_SUCCESS;}
 if(d.previousDepthExtent.width!=chosen->extent.width||d.previousDepthExtent.height!=chosen->extent.height)d.resetPending=true;
 d.previousDepthExtent=chosen->extent;
 const auto transport=NativeGuides::SelectedTransport();
 // This frame's depth, acquisition and queue have qualified. Discovery alone
 // never takes the renderer from native inputs or another prepared producer.
 struct FrameClaim {
  Device& device;bool claimed=false;
  uint64_t Token() const {return reinterpret_cast<uintptr_t>(&device);}
  bool Claim(uint32_t selected){const bool accepted=NeuRotic_ClaimPreparedConnectionV1(2,selected,Token())!=0;claimed=claimed||accepted;return accepted;}
  ~FrameClaim(){if(claimed)NeuRotic_RetirePreparedConnectionV1(2,Token(),
   !device.unsafe&&!device.pending&&!device.processorBlocked&&device.sharedIdle?1:0);}
 } claim{d};
 if(transport!=2){
  if(!claim.Claim(1)){status.reason="Built-in Vulkan capture deferred: another input owner or transport policy";return VK_SUCCESS;}
  if(EnsureShared(d,observed,*chosen)){
   if(handled)*handled=true;
   return SharedFrame(d,observed,present,*chosen,newest,estimated,bgra,consume);
  }
 }
 if(NativeGuides::VulkanRendererSelected()&&!NativeFg::Selected()){
  status.reason="Captured Vulkan NR requires usable shared GPU guide textures: "+d.transportReason;return VK_SUCCESS;
 }
 if(transport==1||(transport==0&&!NativeGuides::SelectedCpuFallback())){
  status.reason="Native shared GPU transport unavailable; CPU fallback disabled by input policy";return VK_SUCCESS;
 }
 if(d.processorBlocked||!chosen->buffer.mapped){status.reason="Native GPU packet cannot fall back after capture ownership changed";return VK_SUCCESS;}
 if(!claim.Claim(2)){status.reason="Built-in Vulkan CPU capture deferred: another input owner or transport policy";return VK_SUCCESS;}
 if(!EnsureColor(d,bytes)){status.reason="Native color readback memory unavailable";return VK_SUCCESS;}
 if(!Begin(d)){status.reason="Native color readback recording failed";return VK_SUCCESS;}
 Move(d,d.command,observed.image,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
 VkBufferImageCopy region{};region.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};region.imageExtent={r.extent.width,r.extent.height,1};
 d.CmdCopyImageToBuffer(d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,d.color.buffer,1,&region);
 ToHost(d,d.command,d.color);Move(d,d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_ASPECT_COLOR_BIT);
 const auto readbackStart=NativeGuides::NowMs();
 if(handled)*handled=true; // Present waits may now be consumed even on refusal.
 const auto result=Submit(d,observed.queue,&present);status.readbackMs=NativeGuides::NowMs()-readbackStart;if(result!=VK_SUCCESS){status.reason="Native color readback submission/completion uncertain";return result;}
 // Same queue fence is after the chosen accepted producer and the current Present waits.
 const auto unpackStart=NativeGuides::NowMs();
 std::vector<uint8_t> color(static_cast<size_t>(bytes));std::memcpy(color.data(),d.color.mapped,color.size());
 if(rgba)for(size_t i=0;i<color.size();i+=4)std::swap(color[i],color[i+2]);
 std::vector<float> depth(static_cast<size_t>(pixels));
 if(!DecodeDepth(*chosen,r.extent,depth)){chosen->consumed=true;d.resetPending=true;status.reason="Captured source depth outside finite [0,1] or incompatible extent";return VK_SUCCESS;}
 status.colorCached=(d.color.properties&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)!=0;status.depthCached=(chosen->buffer.properties&VK_MEMORY_PROPERTY_HOST_CACHED_BIT)!=0;status.unpackMs=NativeGuides::NowMs()-unpackStart;
 d.lastSubmission=newest;
 for(auto& item:d.depths){const auto proof=VulkanNrRecordings().ProducerProof(item.use);if(proof&&proof->submission.value<=newest)item.consumed=true;}
 const bool reset=d.resetPending||d.captures==0||d.lastCaptureFrame+1!=d.frame||
  d.previousEstimated!=estimated;d.resetPending=false;
 d.previousEstimated=estimated;
 if(estimated)++status.estimatedAssociations;
 d.lastCaptureFrame=d.frame;++status.captured;
 NativeGuides::Capture capture{d.luid,reinterpret_cast<uintptr_t>(observed.swapchain),r.swapchainGeneration,++d.captures,
  r.extent.width,r.extent.height,reset,chosen->inverted,color,depth};
 std::vector<uint8_t> output;std::string reason;
 const auto processorStart=NativeGuides::NowMs();
 const auto outcome=NativeGuides::Process(capture,output,reason);status.processorMs=NativeGuides::NowMs()-processorStart;
 d.lastOutcome=outcome;
 status.processorReason=reason;d.processorBlocked=outcome==NativeGuides::Outcome::Unsafe;
 status.reason=(estimated?"Estimated offscreen scene-depth association. ":"Exact depth/color link. ")+reason;
 if(outcome!=NativeGuides::Outcome::Delivered)return VK_SUCCESS;
 if(output.size()!=bytes){d.resetPending=true;status.reason="Native processor output size refused";return VK_SUCCESS;}
 if(rgba)for(size_t i=0;i<output.size();i+=4)std::swap(output[i],output[i+2]);
 std::memcpy(d.color.mapped,output.data(),output.size());
 if(!Begin(d)){d.resetPending=true;status.reason="Native output copy recording failed";return VK_SUCCESS;}
 Move(d,d.command,observed.image,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
 // vkCmdCopyBufferToImage is permitted only for a completed Delivered processor result.
 d.CmdCopyBufferToImage(d.command,d.color.buffer,observed.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&region);
 Move(d,d.command,observed.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,NativeFg::Layouts::Translate(d.device,observed.image,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR),VK_IMAGE_ASPECT_COLOR_BIT);
 const auto copied=Submit(d,observed.queue,nullptr);if(copied!=VK_SUCCESS){d.resetPending=true;status.reason="Native copyback GPU completion uncertain";return copied;}
 ++status.delivered;status.reason=estimated?"Native depth + estimated motion delivered (estimated offscreen scene-depth association)":"Native depth + estimated motion delivered (exact depth/color link)";return VK_SUCCESS;
}

void RenderPassCreated(VkDevice device,const VkRenderPassCreateInfo& ci,VkRenderPass out) {
 if(!Enabled())return;std::lock_guard lock(mutex);++passObservations;if(passes.size()>=1024){Reject("Render pass tracking limit");return;}
 Pass p{};p.device=device;
 if(ci.pNext)p.rejection="Render pass extension unsupported";
 else if(ci.subpassCount!=1)p.rejection="Multiple subpasses unsupported";
 else if(ci.pSubpasses&&!ci.pSubpasses[0].pDepthStencilAttachment)p.rejection="Render pass has no depth attachment";
 if(!ci.pNext&&ci.subpassCount==1&&ci.pSubpasses&&ci.pSubpasses[0].colorAttachmentCount<=32&&
    !ci.pSubpasses[0].flags&&ci.pSubpasses[0].pDepthStencilAttachment){
  p.attachment=ci.pSubpasses[0].pDepthStencilAttachment->attachment;
  for(uint32_t i=0;i<ci.pSubpasses[0].colorAttachmentCount;++i)p.colors.push_back(ci.pSubpasses[0].pColorAttachments[i].attachment);
  if(p.attachment<ci.attachmentCount){p.depth=ci.pAttachments[p.attachment];p.valid=p.depth.storeOp==VK_ATTACHMENT_STORE_OP_STORE&&!p.depth.flags;
   p.rejection=p.depth.flags?"Depth attachment flags unsupported":"Depth store operation discards contents";
  }else p.rejection="Render pass has no depth attachment";
 }
 passes[{device,out}]=p;
}
void RenderPassCreated2(VkDevice device,const VkRenderPassCreateInfo2& ci,VkRenderPass out) {
 if(!Enabled())return;std::lock_guard lock(mutex);++passObservations;if(passes.size()>=1024){Reject("Render pass tracking limit");return;}
 Pass p{};p.device=device;
 if(ci.pNext)p.rejection="Render pass extension unsupported";
 else if(ci.subpassCount!=1)p.rejection="Multiple subpasses unsupported";
 else if(ci.pSubpasses&&!ci.pSubpasses[0].pDepthStencilAttachment)p.rejection="Render pass has no depth attachment";
 if(!ci.pNext&&ci.subpassCount==1&&ci.pSubpasses&&ci.pSubpasses[0].colorAttachmentCount<=32&&
   !ci.pSubpasses[0].pNext&&!ci.pSubpasses[0].flags&&!ci.pSubpasses[0].viewMask&&ci.pSubpasses[0].pDepthStencilAttachment){
  const auto& ref=*ci.pSubpasses[0].pDepthStencilAttachment;p.attachment=ref.attachment;
  for(uint32_t i=0;i<ci.pSubpasses[0].colorAttachmentCount;++i)p.colors.push_back(ci.pSubpasses[0].pColorAttachments[i].attachment);
  if(!ref.pNext&&p.attachment<ci.attachmentCount){const auto& a=ci.pAttachments[p.attachment];
   p.depth={a.flags,a.format,a.samples,a.loadOp,a.storeOp,a.stencilLoadOp,a.stencilStoreOp,a.initialLayout,a.finalLayout};
   p.valid=!a.pNext&&!a.flags&&a.storeOp==VK_ATTACHMENT_STORE_OP_STORE;
   p.rejection=a.pNext?"Depth attachment extension unsupported":a.flags?"Depth attachment flags unsupported":"Depth store operation discards contents";
  }else p.rejection=ref.pNext?"Depth reference extension unsupported":"Render pass has no depth attachment";
 }
 passes[{device,out}]=p;
}
void RenderPassDestroyed(VkDevice device,VkRenderPass pass){if(!Enabled())return;std::lock_guard lock(mutex);passes.erase({device,pass});}
void FramebufferCreated(VkDevice device,const VkFramebufferCreateInfo& ci,VkFramebuffer out) {
 if(!Enabled())return;std::lock_guard lock(mutex);++framebufferObservations;
 if(!ci.pNext&&!ci.flags&&ci.attachmentCount<=32&&framebuffers.size()<2048){
  Framebuffer fb{device,ci.renderPass,{},ci.width,ci.height,ci.layers};
  if(ci.pAttachments)fb.views.assign(ci.pAttachments,ci.pAttachments+ci.attachmentCount);framebuffers[{device,out}]=std::move(fb);
 }else Reject(ci.pNext?"Framebuffer extension unsupported":ci.flags?"Framebuffer flags unsupported":ci.attachmentCount>32?"Framebuffer attachment limit":"Framebuffer tracking limit");
}
void FramebufferDestroyed(VkDevice device,VkFramebuffer fb){if(!Enabled())return;std::lock_guard lock(mutex);framebuffers.erase({device,fb});}
namespace {
VkResult VKAPI_CALL CreateRenderPass(VkDevice d,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
 PFN_vkCreateRenderPass f;{std::lock_guard lock(mutex);auto it=devices.find(d);if(it==devices.end())return VK_ERROR_INITIALIZATION_FAILED;f=it->second->CreateRenderPass;}
 auto r=f(d,ci,a,out);if(r==VK_SUCCESS&&ci&&out)RenderPassCreated(d,*ci,*out);return r;
}
VkResult VKAPI_CALL CreateRenderPass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
 PFN_vkCreateRenderPass2 f;{std::lock_guard lock(mutex);auto it=devices.find(d);if(it==devices.end())return VK_ERROR_INITIALIZATION_FAILED;f=it->second->CreateRenderPass2;}
 if(!f)return VK_ERROR_INITIALIZATION_FAILED;auto r=f(d,ci,a,out);if(r==VK_SUCCESS&&ci&&out)RenderPassCreated2(d,*ci,*out);return r;
}
void VKAPI_CALL DestroyRenderPass(VkDevice d,VkRenderPass p,const VkAllocationCallbacks* a){
 PFN_vkDestroyRenderPass f;{std::lock_guard lock(mutex);auto it=devices.find(d);if(it==devices.end())return;f=it->second->DestroyRenderPass;}
 RenderPassDestroyed(d,p);f(d,p,a);
}
VkResult VKAPI_CALL CreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo* ci,const VkAllocationCallbacks* a,VkFramebuffer* out){
 PFN_vkCreateFramebuffer f;{std::lock_guard lock(mutex);auto it=devices.find(d);if(it==devices.end())return VK_ERROR_INITIALIZATION_FAILED;f=it->second->CreateFramebuffer;}
 auto r=f(d,ci,a,out);if(r==VK_SUCCESS&&ci&&out)FramebufferCreated(d,*ci,*out);return r;
}
void VKAPI_CALL DestroyFramebuffer(VkDevice d,VkFramebuffer p,const VkAllocationCallbacks* a){
 PFN_vkDestroyFramebuffer f;{std::lock_guard lock(mutex);auto it=devices.find(d);if(it==devices.end())return;f=it->second->DestroyFramebuffer;}
 FramebufferDestroyed(d,p);f(d,p,a);
}
}
PFN_vkVoidFunction HookAddress(const char* name,PFN_vkVoidFunction original) {
 if(!Enabled()||!name||!original)return original;
#define HOOK(Name) if(std::strcmp(name,"vk" #Name)==0)return reinterpret_cast<PFN_vkVoidFunction>(Name);
 HOOK(CreateRenderPass) HOOK(CreateRenderPass2) HOOK(DestroyRenderPass) HOOK(CreateFramebuffer) HOOK(DestroyFramebuffer)
#undef HOOK
 if(std::strcmp(name,"vkCreateRenderPass2KHR")==0)return reinterpret_cast<PFN_vkVoidFunction>(CreateRenderPass2);
 return original;
}
}
