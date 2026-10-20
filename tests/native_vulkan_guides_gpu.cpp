// Real Vulkan WSI/depth/readback/copyback fixture. The model processor is an
// explicit recorder; this validates acquisition, never proprietary model quality.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define VK_USE_PLATFORM_WIN32_KHR
#include <Windows.h>
#include "../OptiScaler/dlssnr/NativeVulkanGuides.cpp"
#include "../OptiScaler/dlssnr/PreparedGuideStatusV2.h"
#include <cstdio>
#include <cmath>
#include <stdexcept>

using namespace DlssNr;
static int calls=0,sharedAttempts=0;
static bool expectedReverse=false,forceUnsafe=false,expectQueueReset=false;
static PreparedGuides::StatusV2 preparingStatus;
static float expectedDepth=1.f;
static uint32_t selectedMemoryType=UINT32_MAX;
namespace DlssNr::NativeGuides {
ID3D12Device* SharedDevice(std::uint64_t,std::string& reason){++sharedAttempts;reason="CPU fallback fixture; GPU conversion tested separately";return nullptr;}
Outcome ProcessShared(const Capture&,const SharedCapture&,std::string&){throw std::runtime_error("CPU fallback fixture unexpectedly entered shared GPU path");}
Outcome Process(const Capture& capture,std::vector<uint8_t>& out,std::string& reason) {
 ++calls;
 if(expectQueueReset&&!capture.reset)throw std::runtime_error("queue change did not reset temporal history");
 if(capture.width!=64||capture.height!=64||capture.depth.size()!=4096||capture.bgra.size()!=16384)
  throw std::runtime_error("completed capture dimensions");
 if(capture.depthInverted!=expectedReverse)throw std::runtime_error("captured depth convention");
 for(const auto z:capture.depth)if(z!=expectedDepth)throw std::runtime_error("real stored depth bytes");
 if(capture.bgra[0]<190||capture.bgra[0]>192||capture.bgra[1]<127||capture.bgra[1]>129||capture.bgra[2]<63||capture.bgra[2]>65)
  throw std::runtime_error("real current color bytes");
 if(forceUnsafe){reason="fixture permanent core failure";return Outcome::Unsafe;}
 if(calls==1){if(!capture.reset)throw std::runtime_error("initial reset");reason="fixture preparation";return Outcome::Preparing;}
 if(calls==2&&capture.reset)throw std::runtime_error("continuous capture incorrectly reset");
 if(calls==4&&!capture.reset)throw std::runtime_error("missed Present interval did not reset");
 out.resize(capture.bgra.size());for(size_t i=0;i<out.size();i+=4){out[i]=10;out[i+1]=20;out[i+2]=30;out[i+3]=255;}
 reason="explicit fixture processor output";return Outcome::Delivered;
}
}
static void Check(bool ok,const char* what){if(!ok)throw std::runtime_error(what);}
static void VkCheck(VkResult r,const char* what){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(what)+" VkResult="+std::to_string(r));}
static uint32_t Mem(VkPhysicalDevice physical,uint32_t bits,VkMemoryPropertyFlags flags){
 VkPhysicalDeviceMemoryProperties p{};vkGetPhysicalDeviceMemoryProperties(physical,&p);
 for(uint32_t i=0;i<p.memoryTypeCount;++i)if((bits&(1u<<i))&&(p.memoryTypes[i].propertyFlags&flags)==flags)return i;
 throw std::runtime_error("memory type");
}
static PFN_vkCmdCopyBufferToImage actualCopyback=nullptr;
static void VKAPI_CALL ObservedCopyback(VkCommandBuffer cb,VkBuffer buffer,VkImage image,VkImageLayout layout,uint32_t count,const VkBufferImageCopy* regions){
 // Explicit nested observer seam around the real GPU copy. A loader/device
 // alias must not enter NativeVulkanGuides while its owner mutex is held.
 Check(!NativeVulkanGuides::Enabled(),"private copyback suppresses recursive native observation");
 NativeVulkanGuides::InvalidateDepthHistory(cb);
 actualCopyback(cb,buffer,image,layout,count,regions);
}
int main(int argc,char** argv){try {
 const int selectedTransport=argc>1?std::atoi(argv[1]):0;
 const bool fallback=argc>2?std::atoi(argv[2])!=0:true;
 const int selectedSource=argc>3?std::atoi(argv[3]):2;
 NativeGuides::ConfigureInput(selectedSource,selectedTransport,fallback);
 Check(NativeGuides::SelectedSource()==selectedSource,"explicit source preference preserved");
 if(selectedSource!=0&&selectedSource!=2){
  Check(!NativeVulkanGuides::Enabled(),"explicit Native/ReShade/External excludes built-in discovery");
  std::puts("PASS explicit source excludes built-in Vulkan observation");return 0;
 }
 Check(NativeVulkanGuides::Enabled(),"Automatic or Built-in permits Vulkan guide discovery");
 Check(!NativeFg::Selected(),"guide discovery never automatically enables standalone FG");
 const bool liveFallback=argc>4&&std::atoi(argv[4])!=0;
 if(liveFallback){
  Check(!NativeGuides::SelectedCpuFallback(),"fixture begins with fallback disabled");
 }
 const bool cpuRefused=selectedTransport==1||(selectedTransport==0&&!fallback&&!liveFallback);
 {
  VulkanPresentRegistry registry;const auto device=reinterpret_cast<VkDevice>(uintptr_t(901));
  const auto physical=reinterpret_cast<VkPhysicalDevice>(uintptr_t(902));
  const auto first=reinterpret_cast<VkQueue>(uintptr_t(903)),second=reinterpret_cast<VkQueue>(uintptr_t(904));
  registry.DeviceCreated(device,physical,true,true,{0});
  registry.QueueObserved(device,first,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
  registry.QueueObserved(device,second,0,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
  VkQueue selected{};uint32_t family=UINT32_MAX;
  Check(registry.FramegenQueue(device,selected,family)&&family==0&&(selected==first||selected==second),
    "multiple game queues in one graphics family permit provisional FG creation");
  registry.QueueObserved(device,reinterpret_cast<VkQueue>(uintptr_t(905)),1,VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT);
  Check(!registry.FramegenQueue(device,selected,family)&&!selected&&family==UINT32_MAX,
    "multiple qualified families remain ambiguous");
 }
 {
  float source[16];for(unsigned i=0;i<16;++i)source[i]=float(i)/16.f;
  NativeVulkanGuides::Depth d;d.extent={4,4};d.format=VK_FORMAT_D32_SFLOAT;d.buffer.mapped=source;
  std::vector<float> normalized(4);Check(NativeVulkanGuides::DecodeDepth(d,{2,2},normalized),"valid mixed depth");
  Check(normalized==std::vector<float>({5.f/16,7.f/16,13.f/16,15.f/16}),"mixed-size depth samples source pixel centers instead of prefix");
  source[0]=NAN;Check(!NativeVulkanGuides::DecodeDepth(d,{2,2},normalized),"unsampled nonfinite source rejected");
  Check(!NativeVulkanGuides::CompatibleDepthExtent({3840,2160},{2560,1600}),"aspect mismatch refused");
  Check(NativeVulkanGuides::CompatibleDepthExtent({3840,2160},{2560,1440}),"returned game dimensions supported");
 }
 {
  NativeVulkanGuides::Device d;static unsigned waits=0;
  d.EndCommandBuffer=[](VkCommandBuffer){return VK_SUCCESS;};
  d.QueueSubmit=[](VkQueue,uint32_t,const VkSubmitInfo*,VkFence){return VK_SUCCESS;};
  d.WaitForFences=[](VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){++waits;return VK_SUCCESS;};
  VkSemaphore semaphore=(VkSemaphore)(uintptr_t)1;VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};present.waitSemaphoreCount=1;present.pWaitSemaphores=&semaphore;
  Check(NativeVulkanGuides::Submit(d,VK_NULL_HANDLE,&present,VK_NULL_HANDLE,0,VK_NULL_HANDLE,0,false)==VK_SUCCESS&&d.pending&&waits==0&&present.waitSemaphoreCount==0,
   "accepted input queues without host wait and consumes present waits once");
  Check(!NativeVulkanGuides::Begin(d),"pending input forbids command reset");
  Check(NativeVulkanGuides::Submit(d,VK_NULL_HANDLE,nullptr)==VK_ERROR_DEVICE_LOST,"pending input forbids resubmission");
  Check(NativeVulkanGuides::CompleteSubmission(d)==VK_SUCCESS&&!d.pending&&waits==1,"explicit join proves command reuse");
  d.pending=true;d.WaitForFences=[](VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){return VK_TIMEOUT;};
  Check(NativeVulkanGuides::CompleteSubmission(d)==VK_TIMEOUT&&d.pending&&d.unsafe&&!NativeVulkanGuides::Begin(d),"uncertain deferred join retains pending owner");
  // Original producer recording is released (no remaining use entry), but
  // the private input submission can still be reading this depth allocation.
  d.depths.emplace_back();d.depths.back().consumed=true;
  NativeVulkanGuides::Collect(d);
  Check(d.depths.size()==1,"timeout cleanup retains depth referenced by private input");
  d.pending=false;NativeVulkanGuides::Collect(d);
  Check(d.depths.size()==1,"uncertain owner retains depth even without pending flag");
  d.unsafe=false;NativeVulkanGuides::Collect(d);
  Check(d.depths.empty(),"proven idle owner can collect released depth");
 }
 {
  NativeVulkanGuides::Device d;d.EndCommandBuffer=[](VkCommandBuffer){return VK_ERROR_OUT_OF_HOST_MEMORY;};
  Check(NativeVulkanGuides::Submit(d,VK_NULL_HANDLE,nullptr)==VK_ERROR_INITIALIZATION_FAILED&&d.unsafe,
    "failed command close quarantines recorded shared ownership instead of retrying an unsignalled timeline");
 }
 {
  NativeVulkanGuides::Device d;d.shared=std::make_unique<PreparedTransport::Session>();
  d.command=reinterpret_cast<VkCommandBuffer>(uintptr_t(1));d.fence=(VkFence)(uintptr_t)2;
  d.ResetCommandPool=[](VkDevice,VkCommandPool,VkCommandPoolResetFlags){return VK_ERROR_OUT_OF_HOST_MEMORY;};
  VkObservedPresent observed{};VkPresentInfoKHR present{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};NativeVulkanGuides::Depth depth;
  Check(NativeVulkanGuides::SharedFrame(d,observed,present,depth,1,false,true,{})==VK_SUCCESS&&
    d.sharedSerial==0&&d.sharedIdle&&!d.processorBlocked,"failed first shared Begin preserves initial ownership for retry");
 }
 SetEnvironmentVariableW(L"NEUROTIC_NATIVE_DEPTH_INVERTED",nullptr);
 NativeGuides::Configure(true);NativeVulkanGuides::SetCaptureActive(true);
 bool inverted=false;Check(NativeVulkanGuides::Orientation(VK_ATTACHMENT_LOAD_OP_CLEAR,0.f,inverted)&&inverted,"zero clear reverse heuristic");
 Check(NativeVulkanGuides::Orientation(VK_ATTACHMENT_LOAD_OP_CLEAR,1.f,inverted)&&!inverted,"one clear forward heuristic");
 Check(!NativeVulkanGuides::Orientation(VK_ATTACHMENT_LOAD_OP_CLEAR,.5f,inverted),"ambiguous depth convention refuses");
 Check(!NativeVulkanGuides::CombinedLayout(VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL),"separate stencil layout refuses combined barriers");
 {
  // Explicit driver failures test ownership decisions, not simulated pixels.
  NativeVulkanGuides::Device failure{};
  failure.EndCommandBuffer=[](VkCommandBuffer){return VK_SUCCESS;};
  failure.QueueSubmit=[](VkQueue,uint32_t,const VkSubmitInfo*,VkFence){return VK_ERROR_DEVICE_LOST;};
  failure.WaitForFences=[](VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){return VK_TIMEOUT;};
  VkSemaphore dummy=(VkSemaphore)(uintptr_t)1;VkPresentInfoKHR waits{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};waits.waitSemaphoreCount=1;waits.pWaitSemaphores=&dummy;
  Check(NativeVulkanGuides::Submit(failure,VK_NULL_HANDLE,&waits)==VK_ERROR_DEVICE_LOST&&failure.unsafe&&waits.waitSemaphoreCount==1,"failed submit quarantines and preserves uncertain waits");
  failure.unsafe=false;failure.QueueSubmit=[](VkQueue,uint32_t,const VkSubmitInfo*,VkFence){return VK_SUCCESS;};
  Check(NativeVulkanGuides::Submit(failure,VK_NULL_HANDLE,&waits)==VK_TIMEOUT&&failure.unsafe&&waits.waitSemaphoreCount==0,"accepted submission consumes waits; timeout quarantines");
 }
 {
  NativeVulkanGuides::Device d;d.memory.memoryTypeCount=2;
  d.memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  d.memory.memoryTypes[1].propertyFlags=d.memory.memoryTypes[0].propertyFlags|VK_MEMORY_PROPERTY_HOST_CACHED_BIT;
  d.CreateBuffer=[](VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer* out){*out=(VkBuffer)(uintptr_t)1;return VK_SUCCESS;};
  d.DestroyBuffer=[](VkDevice,VkBuffer,const VkAllocationCallbacks*){};
  d.GetBufferMemoryRequirements=[](VkDevice,VkBuffer,VkMemoryRequirements* out){out->size=4096;out->alignment=64;out->memoryTypeBits=3;};
  d.AllocateMemory=[](VkDevice,const VkMemoryAllocateInfo* info,const VkAllocationCallbacks*,VkDeviceMemory*){selectedMemoryType=info->memoryTypeIndex;return VK_ERROR_OUT_OF_DEVICE_MEMORY;};
  NativeVulkanGuides::Buffer b;Check(!NativeVulkanGuides::Allocate(d,4096,b)&&selectedMemoryType==1&&d.allocated==0,"readback prefers CPU cached memory without charging failed allocation");
  d.memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
  Check(!NativeVulkanGuides::Allocate(d,4096,b)&&selectedMemoryType==0,"readback coherent fallback never accepts unhandled noncoherent memory");
 }
 VkApplicationInfo app{VK_STRUCTURE_TYPE_APPLICATION_INFO};app.pApplicationName="Native guide acquisition fixture";app.apiVersion=VK_API_VERSION_1_1;
 const char* extensions[]={VK_KHR_SURFACE_EXTENSION_NAME,VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
 VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.pApplicationInfo=&app;ici.enabledExtensionCount=2;ici.ppEnabledExtensionNames=extensions;
 uint32_t layerCount=0;vkEnumerateInstanceLayerProperties(&layerCount,nullptr);std::vector<VkLayerProperties> layers(layerCount);vkEnumerateInstanceLayerProperties(&layerCount,layers.data());
 bool validationAvailable=false;for(const auto& layer:layers)if(std::strcmp(layer.layerName,"VK_LAYER_KHRONOS_validation")==0)validationAvailable=true;
 std::printf("VALIDATION available=%s enabled=false (fixture checks real bytes and completion; no validation-layer qualification)\n",validationAvailable?"true":"false");
 VkInstance instance{};auto created=vkCreateInstance(&ici,nullptr,&instance);
 if(created!=VK_SUCCESS){std::printf("UNAVAILABLE WSI instance: %d\n",created);return 77;}
 auto window=CreateWindowExW(0,L"STATIC",L"NeuRotic Vulkan fixture",WS_POPUP,0,0,64,64,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
 Check(window!=nullptr,"hidden fixture window");
 VkWin32SurfaceCreateInfoKHR sci{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};sci.hinstance=GetModuleHandleW(nullptr);sci.hwnd=window;
 VkSurfaceKHR surface{};VkCheck(vkCreateWin32SurfaceKHR(instance,&sci,nullptr,&surface),"surface");
 uint32_t count=0;VkCheck(vkEnumeratePhysicalDevices(instance,&count,nullptr),"physical count");std::vector<VkPhysicalDevice> physicals(count);
 VkCheck(vkEnumeratePhysicalDevices(instance,&count,physicals.data()),"physicals");VkPhysicalDevice physical{};uint32_t family=UINT32_MAX;
 for(auto p:physicals){uint32_t n=0;vkGetPhysicalDeviceQueueFamilyProperties(p,&n,nullptr);std::vector<VkQueueFamilyProperties> qs(n);vkGetPhysicalDeviceQueueFamilyProperties(p,&n,qs.data());
  for(uint32_t i=0;i<n;++i){VkBool32 supported=false;vkGetPhysicalDeviceSurfaceSupportKHR(p,i,surface,&supported);if(supported&&(qs[i].queueFlags&VK_QUEUE_GRAPHICS_BIT)){physical=p;family=i;break;}}
  if(physical)break;
 }
 if(!physical){std::puts("UNAVAILABLE graphics+WSI queue");return 77;}
 VkPhysicalDeviceProperties props{};vkGetPhysicalDeviceProperties(physical,&props);std::printf("DEVICE %s\n",props.deviceName);
 float priority=1;VkDeviceQueueCreateInfo qci{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};qci.queueFamilyIndex=family;qci.queueCount=1;qci.pQueuePriorities=&priority;
 const char* swapExtension=VK_KHR_SWAPCHAIN_EXTENSION_NAME;VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};dci.queueCreateInfoCount=1;dci.pQueueCreateInfos=&qci;dci.enabledExtensionCount=1;dci.ppEnabledExtensionNames=&swapExtension;
 VkDevice device{};VkCheck(vkCreateDevice(physical,&dci,nullptr,&device),"device");VkQueue queue{};vkGetDeviceQueue(device,family,0,&queue);
 RegisterVkNrCompletionDevice(device,{vkCreateFence,vkDestroyFence,vkGetFenceStatus,vkQueueSubmit,vkWaitForFences});RegisterVkNrCompletionQueue(device,queue,family);
 NativeVulkanGuides::DeviceCreated(device,physical,vkGetDeviceProcAddr,vkGetPhysicalDeviceProperties2);
 {
  auto& timed=*NativeVulkanGuides::devices.at(device);
  Check(NativeVulkanGuides::Commands(timed,family)&&NativeVulkanGuides::Begin(timed),"real profiling command owner");
  const bool supported=NativeVulkanGuides::BeginCaptureTiming(timed);
  if(supported)timed.CmdWriteTimestamp(timed.command,VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,timed.timestamps,1);
  VkCheck(NativeVulkanGuides::Submit(timed,queue,nullptr,VK_NULL_HANDLE,0,VK_NULL_HANDLE,0,false),"real deferred queue submission");
  Check(NativeVulkanGuides::CaptureGpuMs(timed)<0,"pending timestamp cannot be reported as completed");
  VkCheck(NativeVulkanGuides::CompleteSubmission(timed),"real deferred join");
  Check(supported?NativeVulkanGuides::CaptureGpuMs(timed)>=0:NativeVulkanGuides::CaptureGpuMs(timed)<0,"completed timestamp or explicit unsupported result");
 }
 {
  auto& native=*NativeVulkanGuides::devices.at(device);constexpr VkDeviceSize bytes=3840ull*2160*4;
  Check(!NativeVulkanGuides::EnsureColor(native,7680ull*4320*4)&&native.allocated==0,"unsupported color cannot poison later 4K reservation");
  Check(NativeVulkanGuides::EnsureColor(native,bytes),"actual 4K color reservation");
  std::vector<NativeVulkanGuides::Buffer> depthBuffers;
  for(unsigned i=0;i<4;++i){NativeVulkanGuides::Buffer b;if(NativeVulkanGuides::Allocate(native,bytes,b))depthBuffers.push_back(b);}
  Check(depthBuffers.size()>=2&&depthBuffers.size()<4&&native.color.capacity==bytes&&native.allocated<=NativeVulkanGuides::MaxBytes,"4K depth budget cannot consume reserved color");
  for(auto& b:depthBuffers)NativeVulkanGuides::Free(native,b);NativeVulkanGuides::Free(native,native.color);
  Check(native.allocated==0,"completed unused 4K allocation fixture releases all bytes");
 }
 actualCopyback=NativeVulkanGuides::devices.at(device)->CmdCopyBufferToImage;
 NativeVulkanGuides::devices.at(device)->CmdCopyBufferToImage=ObservedCopyback;
 GetVulkanPresentRegistry().DeviceCreated(device,physical,false,false,{family},false,false,false,false,VkNrCompletionDeviceGeneration(device));
 GetVulkanPresentRegistry().QueueObserved(device,queue,family,VK_QUEUE_GRAPHICS_BIT);
 if(selectedSource==0){
  Connections::SetNativeUsable(true);
  VkObservedPresent native{};native.device=device;native.queue=queue;
  VkPresentInfoKHR untouched{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};bool handled=true;
  VkCheck(NativeVulkanGuides::BeforePresent(native,untouched,{},&handled),"native-priority refusal");
  Check(!handled&&calls==0&&NativeVulkanGuides::Status().reason.find("game-supplied")!=std::string::npos,
   "Automatic native priority never enters capture or consumes Present");
  Connections::SetNativeUsable(false);
 }
 VkSurfaceCapabilitiesKHR caps{};VkCheck(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical,surface,&caps),"surface caps");
 VkSwapchainCreateInfoKHR swapci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};swapci.surface=surface;swapci.minImageCount=(std::max)(2u,caps.minImageCount);swapci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;swapci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
 swapci.imageExtent={64,64};swapci.imageArrayLayers=1;swapci.imageUsage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 swapci.preTransform=caps.currentTransform;swapci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;swapci.presentMode=VK_PRESENT_MODE_FIFO_KHR;swapci.clipped=true;
 VkSwapchainKHR swap{};VkCheck(vkCreateSwapchainKHR(device,&swapci,nullptr,&swap),"swapchain");
 VkCheck(vkGetSwapchainImagesKHR(device,swap,&count,nullptr),"swap images count");std::vector<VkImage> colors(count);VkCheck(vkGetSwapchainImagesKHR(device,swap,&count,colors.data()),"swap images");
 GetVulkanPresentRegistry().SwapchainCreated(device,swap,swapci,caps.supportedUsageFlags,true,colors,{family});
 std::vector<VkImageView> colorViews(count);for(uint32_t i=0;i<count;++i){VkImageViewCreateInfo vi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};vi.image=colors[i];vi.viewType=VK_IMAGE_VIEW_TYPE_2D;vi.format=swapci.imageFormat;vi.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};VkCheck(vkCreateImageView(device,&vi,nullptr,&colorViews[i]),"color view");NativeVulkanGuides::ViewCreated(device,vi,colorViews[i]);}
 VkImageCreateInfo imageci{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};imageci.imageType=VK_IMAGE_TYPE_2D;imageci.format=VK_FORMAT_D32_SFLOAT;imageci.extent={64,64,1};imageci.mipLevels=1;imageci.arrayLayers=1;imageci.samples=VK_SAMPLE_COUNT_1_BIT;imageci.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
 VkImage depth{};VkCheck(vkCreateImage(device,&imageci,nullptr,&depth),"depth");VulkanNrImageFacts().Created(device,imageci,depth,VK_SUCCESS);
 VkMemoryRequirements req{};vkGetImageMemoryRequirements(device,depth,&req);VkMemoryAllocateInfo mai{VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO};mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory memory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&memory),"depth memory");VkCheck(vkBindImageMemory(device,depth,memory,0),"depth bind");
 VkImageViewCreateInfo dvi{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};dvi.image=depth;dvi.viewType=VK_IMAGE_VIEW_TYPE_2D;dvi.format=imageci.format;dvi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,0,1,0,1};VkImageView depthView{};VkCheck(vkCreateImageView(device,&dvi,nullptr,&depthView),"depth view");NativeVulkanGuides::ViewCreated(device,dvi,depthView);
 VkAttachmentDescription attachments[2]{};attachments[0].format=swapci.imageFormat;attachments[1].format=imageci.format;
 for(auto& a:attachments){a.samples=VK_SAMPLE_COUNT_1_BIT;a.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;a.storeOp=VK_ATTACHMENT_STORE_OP_STORE;a.initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;}
 attachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
 VkAttachmentReference cr{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},dr{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};VkSubpassDescription sub{};sub.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;sub.colorAttachmentCount=1;sub.pColorAttachments=&cr;sub.pDepthStencilAttachment=&dr;
 VkRenderPassCreateInfo rpci{VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO};rpci.attachmentCount=2;rpci.pAttachments=attachments;rpci.subpassCount=1;rpci.pSubpasses=&sub;VkRenderPass pass{};VkCheck(vkCreateRenderPass(device,&rpci,nullptr,&pass),"render pass");NativeVulkanGuides::RenderPassCreated(device,rpci,pass);
 std::vector<VkFramebuffer> fbs(count);for(uint32_t i=0;i<count;++i){VkImageView v[]={colorViews[i],depthView};VkFramebufferCreateInfo fci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};fci.renderPass=pass;fci.attachmentCount=2;fci.pAttachments=v;fci.width=64;fci.height=64;fci.layers=1;VkCheck(vkCreateFramebuffer(device,&fci,nullptr,&fbs[i]),"framebuffer");NativeVulkanGuides::FramebufferCreated(device,fci,fbs[i]);}
 // Empty vertex shader is valid with rasterizer discard. The real render pass
 // clears its attachments and records a real draw; no fake draw callback alone.
 const uint32_t vs[]={0x07230203,0x00010000,0,5,0,0x00020011,1,0x0003000e,0,1,0x0005000f,0,3,0x6e69616d,0,0x00020013,1,0x00030021,2,1,0x00050036,1,3,0,2,0x000200f8,4,0x000100fd,0x00010038};
 VkShaderModuleCreateInfo smci{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};smci.codeSize=sizeof(vs);smci.pCode=vs;VkShaderModule shader{};VkCheck(vkCreateShaderModule(device,&smci,nullptr,&shader),"shader");
 VkPipelineLayoutCreateInfo plci{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};VkPipelineLayout layout{};VkCheck(vkCreatePipelineLayout(device,&plci,nullptr,&layout),"pipeline layout");
 VkPipelineShaderStageCreateInfo stage{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO};stage.stage=VK_SHADER_STAGE_VERTEX_BIT;stage.module=shader;stage.pName="main";
 VkPipelineVertexInputStateCreateInfo vi{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};VkPipelineInputAssemblyStateCreateInfo ia{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};ia.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
 VkPipelineRasterizationStateCreateInfo rs{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};rs.rasterizerDiscardEnable=true;rs.lineWidth=1;
 VkGraphicsPipelineCreateInfo pci{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};pci.stageCount=1;pci.pStages=&stage;pci.pVertexInputState=&vi;pci.pInputAssemblyState=&ia;pci.pRasterizationState=&rs;pci.layout=layout;pci.renderPass=pass;VkPipeline pipeline{};VkCheck(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&pipeline),"real draw pipeline");
 VkCommandPoolCreateInfo cpci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};cpci.queueFamilyIndex=family;cpci.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;VkCommandPool pool{};VkCheck(vkCreateCommandPool(device,&cpci,nullptr,&pool),"pool");VulkanNrRecordings().OnCreatePool(device,pool,cpci,VK_SUCCESS);
 VkCommandBufferAllocateInfo cai{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};cai.commandPool=pool;cai.commandBufferCount=1;VkCommandBuffer cb{};VkCheck(vkAllocateCommandBuffers(device,&cai,&cb),"command");VulkanNrRecordings().OnAllocateBuffers(device,cai,&cb,VK_SUCCESS);
 VkSemaphoreCreateInfo semci{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};VkSemaphore acquire{},rendered{};VkCheck(vkCreateSemaphore(device,&semci,nullptr,&acquire),"acquire semaphore");VkCheck(vkCreateSemaphore(device,&semci,nullptr,&rendered),"render semaphore");
 VkImageCreateInfo offci=imageci;offci.format=swapci.imageFormat;offci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
 VkImage offscreen{};VkCheck(vkCreateImage(device,&offci,nullptr,&offscreen),"offscreen color");VulkanNrImageFacts().Created(device,offci,offscreen,VK_SUCCESS);
 vkGetImageMemoryRequirements(device,offscreen,&req);mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory offMemory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&offMemory),"offscreen memory");VkCheck(vkBindImageMemory(device,offscreen,offMemory,0),"offscreen bind");
 VkImageViewCreateInfo offvi=dvi;offvi.image=offscreen;offvi.format=offci.format;offvi.subresourceRange.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;VkImageView offView{};VkCheck(vkCreateImageView(device,&offvi,nullptr,&offView),"offscreen view");NativeVulkanGuides::ViewCreated(device,offvi,offView);
 attachments[0].finalLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;VkRenderPass offPass{};VkCheck(vkCreateRenderPass(device,&rpci,nullptr,&offPass),"offscreen pass");NativeVulkanGuides::RenderPassCreated(device,rpci,offPass);
 VkImageView offViews[]={offView,depthView};VkFramebufferCreateInfo offFci{VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO};offFci.renderPass=offPass;offFci.attachmentCount=2;offFci.pAttachments=offViews;offFci.width=64;offFci.height=64;offFci.layers=1;VkFramebuffer offFb{};VkCheck(vkCreateFramebuffer(device,&offFci,nullptr,&offFb),"offscreen framebuffer");NativeVulkanGuides::FramebufferCreated(device,offFci,offFb);
 VkImage otherDepth{};VkCheck(vkCreateImage(device,&imageci,nullptr,&otherDepth),"second depth");VulkanNrImageFacts().Created(device,imageci,otherDepth,VK_SUCCESS);
 vkGetImageMemoryRequirements(device,otherDepth,&req);mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory otherMemory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&otherMemory),"second depth memory");VkCheck(vkBindImageMemory(device,otherDepth,otherMemory,0),"second depth bind");
 VkImageViewCreateInfo otherVi=dvi;otherVi.image=otherDepth;VkImageView otherView{};VkCheck(vkCreateImageView(device,&otherVi,nullptr,&otherView),"second depth view");NativeVulkanGuides::ViewCreated(device,otherVi,otherView);
 std::vector<VkFramebuffer> otherFbs(count);for(uint32_t i=0;i<count;++i){VkImageView alternateViews[]={colorViews[i],otherView};auto fci=offFci;fci.renderPass=pass;fci.pAttachments=alternateViews;VkCheck(vkCreateFramebuffer(device,&fci,nullptr,&otherFbs[i]),"second framebuffer");NativeVulkanGuides::FramebufferCreated(device,fci,otherFbs[i]);}
 VkImageView otherOffViews[]={offView,otherView};auto otherOffCi=offFci;otherOffCi.pAttachments=otherOffViews;
 VkFramebuffer otherOffFb{};VkCheck(vkCreateFramebuffer(device,&otherOffCi,nullptr,&otherOffFb),"second offscreen framebuffer");NativeVulkanGuides::FramebufferCreated(device,otherOffCi,otherOffFb);
 VkCommandBuffer finalCb{};VkCheck(vkAllocateCommandBuffers(device,&cai,&finalCb),"final color command");VulkanNrRecordings().OnAllocateBuffers(device,cai,&finalCb,VK_SUCCESS);
 auto depthSub=sub;depthSub.colorAttachmentCount=0;depthSub.pColorAttachments=nullptr;
 auto depthRp=rpci;depthRp.pSubpasses=&depthSub;VkRenderPass depthPass{};VkCheck(vkCreateRenderPass(device,&depthRp,nullptr,&depthPass),"depth only pass");NativeVulkanGuides::RenderPassCreated(device,depthRp,depthPass);
 auto depthFci=offFci;depthFci.renderPass=depthPass;VkFramebuffer depthFb{};VkCheck(vkCreateFramebuffer(device,&depthFci,nullptr,&depthFb),"depth only framebuffer");NativeVulkanGuides::FramebufferCreated(device,depthFci,depthFb);
 pci.renderPass=depthPass;VkPipeline depthPipeline{};VkCheck(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&depthPipeline),"depth only pipeline");
 VkImage extraColor{};VkCheck(vkCreateImage(device,&offci,nullptr,&extraColor),"extra deferred color");VulkanNrImageFacts().Created(device,offci,extraColor,VK_SUCCESS);
 vkGetImageMemoryRequirements(device,extraColor,&req);mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory extraMemory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&extraMemory),"extra color memory");VkCheck(vkBindImageMemory(device,extraColor,extraMemory,0),"extra color bind");
 auto extraVi=offvi;extraVi.image=extraColor;VkImageView extraView{};VkCheck(vkCreateImageView(device,&extraVi,nullptr,&extraView),"extra color view");NativeVulkanGuides::ViewCreated(device,extraVi,extraView);
 VkAttachmentDescription deferredAttachments[]={attachments[0],attachments[1],attachments[0]};VkAttachmentReference deferredColors[]={{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},{2,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}};
 auto deferredSub=sub;deferredSub.colorAttachmentCount=2;deferredSub.pColorAttachments=deferredColors;
 auto deferredRp=rpci;deferredRp.attachmentCount=3;deferredRp.pAttachments=deferredAttachments;deferredRp.pSubpasses=&deferredSub;
 VkRenderPass deferredPass{};VkCheck(vkCreateRenderPass(device,&deferredRp,nullptr,&deferredPass),"deferred MRT pass");NativeVulkanGuides::RenderPassCreated(device,deferredRp,deferredPass);
 VkImageView deferredViews[]={offView,depthView,extraView};auto deferredFci=offFci;deferredFci.renderPass=deferredPass;deferredFci.attachmentCount=3;deferredFci.pAttachments=deferredViews;
 VkFramebuffer deferredFb{};VkCheck(vkCreateFramebuffer(device,&deferredFci,nullptr,&deferredFb),"deferred framebuffer");NativeVulkanGuides::FramebufferCreated(device,deferredFci,deferredFb);
 std::vector<VkFramebuffer> deferredSwapFbs(count);for(uint32_t i=0;i<count;++i){VkImageView v[]={offView,depthView,colorViews[i]};auto fci=deferredFci;fci.pAttachments=v;VkCheck(vkCreateFramebuffer(device,&fci,nullptr,&deferredSwapFbs[i]),"MRT swapchain framebuffer");NativeVulkanGuides::FramebufferCreated(device,fci,deferredSwapFbs[i]);}
 pci.renderPass=deferredPass;VkPipeline deferredPipeline{};VkCheck(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&deferredPipeline),"deferred pipeline");
 // A framebuffer is reusable with a compatible render pass, even when the
 // creation handle differs. LOAD/STORE and final layouts belong to the active
 // pass. Give the creation pass a discarded depth store to catch using its data.
 auto compatibleAttachments=std::array<VkAttachmentDescription,2>{attachments[0],attachments[1]};
 compatibleAttachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 compatibleAttachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
 auto compatibleCi=rpci;compatibleCi.pAttachments=compatibleAttachments.data();
 VkRenderPass compatiblePass{};VkCheck(vkCreateRenderPass(device,&compatibleCi,nullptr,&compatiblePass),"compatible creation pass");NativeVulkanGuides::RenderPassCreated(device,compatibleCi,compatiblePass);
 std::vector<VkFramebuffer> compatibleFbs(count);for(uint32_t i=0;i<count;++i){VkImageView v[]={colorViews[i],depthView};auto fci=offFci;fci.renderPass=compatiblePass;fci.pAttachments=v;VkCheck(vkCreateFramebuffer(device,&fci,nullptr,&compatibleFbs[i]),"compatible framebuffer");NativeVulkanGuides::FramebufferCreated(device,fci,compatibleFbs[i]);}
 auto loadAttachments=std::array<VkAttachmentDescription,2>{attachments[0],attachments[1]};
 loadAttachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
 loadAttachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;loadAttachments[1].initialLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
 auto loadCi=rpci;loadCi.pAttachments=loadAttachments.data();VkRenderPass loadPass{};
 VkCheck(vkCreateRenderPass(device,&loadCi,nullptr,&loadPass),"retained depth pass");NativeVulkanGuides::RenderPassCreated(device,loadCi,loadPass);
 auto inlineAttachments=loadAttachments;inlineAttachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;inlineAttachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
 auto inlineCi=rpci;inlineCi.pAttachments=inlineAttachments.data();VkRenderPass inlinePass{};
 VkCheck(vkCreateRenderPass(device,&inlineCi,nullptr,&inlinePass),"inline depth clear pass");NativeVulkanGuides::RenderPassCreated(device,inlineCi,inlinePass);
 auto layeredCi=imageci;layeredCi.flags=VK_IMAGE_CREATE_MUTABLE_FORMAT_BIT|VK_IMAGE_CREATE_ALIAS_BIT;layeredCi.extent={128,128,1};layeredCi.mipLevels=2;layeredCi.arrayLayers=2;
 layeredCi.usage=VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
 layeredCi=NativeVulkanGuides::PrepareImage(device,layeredCi);Check(bool(layeredCi.usage&VK_IMAGE_USAGE_TRANSFER_SRC_BIT),"flagged layered depth receives supported transfer usage");
 VkImage layered{};VkCheck(vkCreateImage(device,&layeredCi,nullptr,&layered),"layered mip depth");VulkanNrImageFacts().Created(device,layeredCi,layered,VK_SUCCESS);
 vkGetImageMemoryRequirements(device,layered,&req);mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory layeredMemory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&layeredMemory),"layered memory");VkCheck(vkBindImageMemory(device,layered,layeredMemory,0),"layered bind");
 auto layeredVi=dvi;layeredVi.image=layered;layeredVi.viewType=VK_IMAGE_VIEW_TYPE_2D_ARRAY;layeredVi.subresourceRange={VK_IMAGE_ASPECT_DEPTH_BIT,1,1,1,1};
 VkImageView layeredView{};VkCheck(vkCreateImageView(device,&layeredVi,nullptr,&layeredView),"single-layer nonzero-mip view");NativeVulkanGuides::ViewCreated(device,layeredVi,layeredView);
 std::vector<VkFramebuffer> layeredFbs(count);for(uint32_t i=0;i<count;++i){VkImageView v[]={colorViews[i],layeredView};auto fci=offFci;fci.renderPass=pass;fci.pAttachments=v;VkCheck(vkCreateFramebuffer(device,&fci,nullptr,&layeredFbs[i]),"layered framebuffer");NativeVulkanGuides::FramebufferCreated(device,fci,layeredFbs[i]);}
 auto bigCi=imageci;bigCi.extent={128,128,1};VkImage bigDepth{};VkCheck(vkCreateImage(device,&bigCi,nullptr,&bigDepth),"mixed-size depth");VulkanNrImageFacts().Created(device,bigCi,bigDepth,VK_SUCCESS);
 vkGetImageMemoryRequirements(device,bigDepth,&req);mai.allocationSize=req.size;mai.memoryTypeIndex=Mem(physical,req.memoryTypeBits,0);VkDeviceMemory bigMemory{};VkCheck(vkAllocateMemory(device,&mai,nullptr,&bigMemory),"mixed depth memory");VkCheck(vkBindImageMemory(device,bigDepth,bigMemory,0),"mixed depth bind");
 auto bigVi=dvi;bigVi.image=bigDepth;VkImageView bigView{};VkCheck(vkCreateImageView(device,&bigVi,nullptr,&bigView),"mixed depth view");NativeVulkanGuides::ViewCreated(device,bigVi,bigView);
 VkAttachmentReference bigRef{0,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};auto bigSub=depthSub;bigSub.pDepthStencilAttachment=&bigRef;
 auto bigRp=rpci;bigRp.attachmentCount=1;bigRp.pAttachments=&attachments[1];bigRp.pSubpasses=&bigSub;VkRenderPass bigPass{};VkCheck(vkCreateRenderPass(device,&bigRp,nullptr,&bigPass),"mixed depth pass");NativeVulkanGuides::RenderPassCreated(device,bigRp,bigPass);
 auto bigFci=offFci;bigFci.renderPass=bigPass;bigFci.attachmentCount=1;bigFci.pAttachments=&bigView;bigFci.width=bigFci.height=128;VkFramebuffer bigFb{};VkCheck(vkCreateFramebuffer(device,&bigFci,nullptr,&bigFb),"mixed depth framebuffer");NativeVulkanGuides::FramebufferCreated(device,bigFci,bigFb);
 pci.renderPass=bigPass;VkPipeline bigPipeline{};VkCheck(vkCreateGraphicsPipelines(device,VK_NULL_HANDLE,1,&pci,nullptr,&bigPipeline),"mixed depth pipeline");
 // The new path must accept real completed offscreen depth without a color
 // lineage callback. It must still reject two equally plausible scene depths.
 for(unsigned frame=0;frame<30;++frame){
  const bool offscreenFrame=frame==2||frame==26||(frame>=9&&frame<17);
  forceUnsafe=frame==28;NativeVulkanGuides::SetCaptureActive(true);expectedReverse=frame==25||frame==4;NativeGuides::ConfigureDepthDirection(frame==4?1:(frame==8||frame==19||frame==24||frame==27)?0:-1);
  expectedDepth=frame==4?.5f:frame==25?0.f:1.f;
  const bool refused=(liveFallback&&frame==2)||frame==3||(frame>=5&&frame<=8)||frame==10||(frame>=14&&frame<18)||frame==19||frame==23||frame==26||frame==27||frame==29;
  uint32_t index=0;VkCheck(vkAcquireNextImageKHR(device,swap,UINT64_MAX,acquire,VK_NULL_HANDLE,&index),"acquire");
  // Delay only the observation for frame 17 to exercise recording with known
  // swapchain membership but absent acquire context. Vulkan itself is acquired.
  if(frame==17)GetVulkanPresentRegistry().PresentSucceeded(swap,index,queue);
  else GetVulkanPresentRegistry().ImageAcquired(device,swap,index,acquire);
  VkCheck(vkResetCommandBuffer(cb,0),"reset command");VulkanNrRecordings().OnReset(cb,VK_SUCCESS);
  VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};bi.flags=frame==7?0:VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;VkCheck(vkBeginCommandBuffer(cb,&bi),"begin command");VulkanNrRecordings().OnBegin(cb,bi.flags,VK_SUCCESS);
  VkClearValue clears[3]{};clears[0].color={{.25f,.5f,.75f,1.f}};clears[1].depthStencil={frame==3?.5f:1.f,0};clears[2]=clears[0];
  VkRenderPassBeginInfo begin{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};begin.renderPass=offscreenFrame?offPass:pass;begin.framebuffer=offscreenFrame?offFb:fbs[index];begin.renderArea.extent={64,64};begin.clearValueCount=2;begin.pClearValues=clears;
  if(frame==11){begin.renderPass=depthPass;begin.framebuffer=depthFb;}
  if(frame==9){begin.renderPass=bigPass;begin.framebuffer=bigFb;begin.renderArea.extent={128,128};begin.clearValueCount=1;begin.pClearValues=&clears[1];}
  if(frame==18)begin.framebuffer=compatibleFbs[index];
  if(frame==19)begin.renderPass=compatiblePass; // active pass discards; creation pass stores
  if(frame==21)begin.renderPass=inlinePass;
  if(frame>=22&&frame<26){begin.renderPass=loadPass;begin.framebuffer=layeredFbs[index];}
  if(frame==22||frame==25){
   auto& native=*NativeVulkanGuides::devices.at(device);NativeVulkanGuides::Move(native,cb,layered,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT,1,1);
   VkClearDepthStencilValue clear{frame==25?0.f:1.f,0};VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT,1,1,1,1};
   vkCmdClearDepthStencilImage(cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
   bool inverse=false;Check(NativeVulkanGuides::PriorDirection(cb,layeredView,inverse)&&inverse==expectedReverse,"separate clear exact subresource convention");
   auto missing=begin;missing.renderPass=VK_NULL_HANDLE;NativeVulkanGuides::BeginLegacy(cb,&missing);
   Check(!NativeVulkanGuides::PriorDirection(cb,layeredView,inverse),"unsupported scope invalidates old clear");
   NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
   NativeVulkanGuides::InvalidateDepthHistory(cb);
   Check(!NativeVulkanGuides::PriorDirection(cb,layeredView,inverse),"intervening untracked write invalidates old clear");
   NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
   VkImageMemoryBarrier discard{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};discard.image=layered;discard.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;
   NativeVulkanGuides::Barrier(cb,0,0,0,0,nullptr,0,nullptr,1,&discard);
   Check(!NativeVulkanGuides::PriorDirection(cb,layeredView,inverse),"undefined layout discards prior convention");
   NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,&clear,1,&range);
   NativeVulkanGuides::Move(native,cb,layered,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,VK_IMAGE_ASPECT_DEPTH_BIT,1,1);
  }
  if(frame==22){
   NativeVulkanGuides::InvalidateDepthHistory(cb);const auto copies=NativeVulkanGuides::Status().depthCopies;
   for(size_t attempt=0;attempt<NativeVulkanGuides::MaxCaptures;++attempt){
    vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&begin);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cb,3,1,0,0);if(frame!=27)NativeVulkanGuides::Draw(cb);
    vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
   }
   Check(NativeVulkanGuides::Status().depthCopies==copies,"unknown direction cannot consume capture slots before valid scene");
   VkClearDepthStencilValue clear{1.f,0};VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT,1,1,1,1};
   // Prior real depth is still 1 (fixture draw has depth testing disabled).
   NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,&clear,1,&range);
  }
  if(frame==12||frame==16){begin.renderPass=deferredPass;begin.framebuffer=frame==16?deferredSwapFbs[index]:deferredFb;begin.clearValueCount=3;}
  vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&begin);
  const auto copiesBefore=NativeVulkanGuides::Status().depthCopies;
  if(frame==21){VkClearAttachment clear{};clear.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;clear.clearValue.depthStencil={1.f,0};VkClearRect rect{{{0,0},{64,64}},0,1};vkCmdClearAttachments(cb,1,&clear,1,&rect);NativeVulkanGuides::ClearAttachments(cb,1,&clear,1,&rect);}
  vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,frame==9?bigPipeline:frame==11?depthPipeline:(frame==12||frame==16)?deferredPipeline:pipeline);vkCmdDraw(cb,3,1,0,0);if(frame!=27)NativeVulkanGuides::Draw(cb);
  if(frame==8){VkClearAttachment changed{};changed.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;changed.clearValue.depthStencil={0.f,0};VkClearRect rect{{{0,0},{64,64}},0,1};vkCmdClearAttachments(cb,1,&changed,1,&rect);NativeVulkanGuides::ClearAttachments(cb,1,&changed,1,&rect);}
  vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
  if(frame==11){
   auto auxiliary=begin;auxiliary.renderPass=bigPass;auxiliary.framebuffer=bigFb;auxiliary.renderArea.extent={128,128};auxiliary.clearValueCount=1;auxiliary.pClearValues=&clears[1];
   vkCmdBeginRenderPass(cb,&auxiliary,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&auxiliary);
   vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,bigPipeline);vkCmdDraw(cb,3,1,0,0);NativeVulkanGuides::Draw(cb);
   vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
   Check(NativeVulkanGuides::Status().depthCopies==copiesBefore+2,"matching and scaled candidates both recorded; selection must preserve matching tier");
  }
  if(frame==26){
   NativeVulkanGuides::SetCaptureActive(false);
   const auto pausedCopies=NativeVulkanGuides::Status().depthCopies;
   NativeVulkanGuides::BeginLegacy(cb,&begin);NativeVulkanGuides::Draw(cb);NativeVulkanGuides::End(cb);
   Check(NativeVulkanGuides::Status().depthCopies==pausedCopies,"NR disabled records no depth copies");
  }
  if(frame==0)Check(NativeVulkanGuides::devices.at(device)->color.capacity>=64*64*4,"reserve current color before depth candidates consume budget");
  if(frame==20){
   begin.renderPass=loadPass;vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&begin);
   vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cb,3,1,0,0);if(frame!=27)NativeVulkanGuides::Draw(cb);
   vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
   Check(NativeVulkanGuides::Status().depthCopies==copiesBefore+2,"LOAD pass retains depth and records its own copy");
  }
  if(frame==21)Check(NativeVulkanGuides::Status().depthCopies==copiesBefore+1,"in-pass full clear establishes direction after DONT_CARE begin");
  if(frame==22){
   // Metadata observed after the readback recording must not relabel its bytes.
   VkClearDepthStencilValue later{0.f,0};VkImageSubresourceRange range{VK_IMAGE_ASPECT_DEPTH_BIT,1,1,1,1};
   NativeVulkanGuides::ClearDepthImage(cb,layered,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL,&later,1,&range);
  }
  if(frame==7){
   // Observer-only missing-resource event after the real replayable-buffer
   // rejection: unrelated scopes must not erase that earlier diagnostic.
   auto missing=begin;missing.renderPass=VK_NULL_HANDLE;NativeVulkanGuides::BeginLegacy(cb,&missing);
   Check(NativeVulkanGuides::Status().reason.find("one-time primary")!=std::string::npos,"persistent rejection diagnostics survive unrelated scopes");
  }
  VkCommandBuffer colorCb=cb;
  if(frame==13){
   VkCheck(vkEndCommandBuffer(cb),"end scene command");VulkanNrRecordings().OnEnd(cb,VK_SUCCESS);
   VkCheck(vkBeginCommandBuffer(finalCb,&bi),"begin final color command");VulkanNrRecordings().OnBegin(finalCb,bi.flags,VK_SUCCESS);colorCb=finalCb;
  }
  if(offscreenFrame){
   auto& transport=*NativeVulkanGuides::devices.at(device);NativeVulkanGuides::Move(transport,colorCb,colors[index],VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
   VkImageCopy whole{};whole.srcSubresource=whole.dstSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};whole.extent={64,64,1};
   vkCmdCopyImage(colorCb,offscreen,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,colors[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&whole);VulkanNrRecordings().OnWork(colorCb);
   if(frame==2)NativeVulkanGuides::ColorCopy(cb,offscreen,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,colors[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&whole);
   NativeVulkanGuides::Move(transport,colorCb,colors[index],VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_ASPECT_COLOR_BIT);
  }
  if(frame==4||frame==5||frame==10){
   if(frame==4)clears[1].depthStencil.depth=.5f; // Unknown convention must not displace the clear-backed scene source.
   VkMemoryBarrier order{VK_STRUCTURE_TYPE_MEMORY_BARRIER};order.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;order.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
   vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&order,0,nullptr,0,nullptr);
   begin.renderPass=frame==10?offPass:pass;begin.framebuffer=frame==10?otherOffFb:otherFbs[index];vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&begin);
   vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cb,3,1,0,0);if(frame!=27)NativeVulkanGuides::Draw(cb);
   if(frame==8){VkClearAttachment changed{};changed.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;changed.clearValue.depthStencil={0.f,0};VkClearRect rect{{{0,0},{64,64}},0,1};vkCmdClearAttachments(cb,1,&changed,1,&rect);NativeVulkanGuides::ClearAttachments(cb,1,&changed,1,&rect);}
  vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
  }
  if(frame==4||frame==27){
   // A later version of the selected source must supersede its earlier copy.
   // Two unknown-only writes must refuse rather than publish the first version.
   clears[1].depthStencil.depth=.5f;
   begin.renderPass=pass;begin.framebuffer=frame==4?fbs[index]:otherFbs[index];
   for(unsigned repeat=0;repeat<(frame==27?3u:1u);++repeat){
    if(frame==27&&repeat==2)clears[1].depthStencil.depth=1.f; // A later-recorded clear cannot erase the suppressed-write tombstone.
    VkMemoryBarrier order{VK_STRUCTURE_TYPE_MEMORY_BARRIER};order.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;order.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(cb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&order,0,nullptr,0,nullptr);
    vkCmdBeginRenderPass(cb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(cb,true);NativeVulkanGuides::BeginLegacy(cb,&begin);
    vkCmdBindPipeline(cb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(cb,3,1,0,0);NativeVulkanGuides::Draw(cb);
    vkCmdEndRenderPass(cb);VulkanNrRecordings().OnRendering(cb,false);NativeVulkanGuides::End(cb);
   }
  }
  if(frame==8||frame==19){
   // A clear-backed source followed by a weaker version in another submitted
   // recording must refuse; source rank cannot make the old snapshot current.
   for(unsigned version=0;version<(frame==19?3u:2u);++version){
    if(version==1){
     VkCheck(vkEndCommandBuffer(cb),"end earlier source");VulkanNrRecordings().OnEnd(cb,VK_SUCCESS);
     VkCheck(vkBeginCommandBuffer(finalCb,&bi),"begin later source");VulkanNrRecordings().OnBegin(finalCb,bi.flags,VK_SUCCESS);colorCb=finalCb;
    }
    VkMemoryBarrier order{VK_STRUCTURE_TYPE_MEMORY_BARRIER};order.srcAccessMask=VK_ACCESS_MEMORY_WRITE_BIT;order.dstAccessMask=VK_ACCESS_MEMORY_WRITE_BIT|VK_ACCESS_MEMORY_READ_BIT;
    vkCmdPipelineBarrier(colorCb,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,1,&order,0,nullptr,0,nullptr);
    clears[1].depthStencil.depth=version?.5f:1.f;begin.renderPass=pass;begin.framebuffer=(frame==19&&version==1)?otherFbs[index]:fbs[index];
    vkCmdBeginRenderPass(colorCb,&begin,VK_SUBPASS_CONTENTS_INLINE);VulkanNrRecordings().OnRendering(colorCb,true);NativeVulkanGuides::BeginLegacy(colorCb,&begin);
    vkCmdBindPipeline(colorCb,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline);vkCmdDraw(colorCb,3,1,0,0);NativeVulkanGuides::Draw(colorCb);
    vkCmdEndRenderPass(colorCb);VulkanNrRecordings().OnRendering(colorCb,false);NativeVulkanGuides::End(colorCb);
   }
  }
  VkCheck(vkEndCommandBuffer(colorCb),"end command");VulkanNrRecordings().OnEnd(colorCb,VK_SUCCESS);
  VkCommandBuffer submittedCommands[]={cb,finalCb};const uint32_t submittedCount=(frame==8||frame==13||frame==19)?2u:1u;
  VkPipelineStageFlags waitStage=VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;VkSubmitInfo si{VK_STRUCTURE_TYPE_SUBMIT_INFO};si.waitSemaphoreCount=1;si.pWaitSemaphores=&acquire;si.pWaitDstStageMask=&waitStage;si.commandBufferCount=submittedCount;si.pCommandBuffers=submittedCommands;si.signalSemaphoreCount=1;si.pSignalSemaphores=&rendered;
  VkCheck(ObserveVkNrQueueSubmit(queue,{submittedCommands,submittedCount},[&]{return vkQueueSubmit(queue,1,&si,VK_NULL_HANDLE);}),"observed submit");
  // Frame 16 advances only the registry acquisition after recording. The
  // current Present context is valid but the MRT depth's stored binding is old.
  if(frame==16||frame==17)GetVulkanPresentRegistry().ImageAcquired(device,swap,index,acquire);
  VkObservedPresent observed{};observed.device=device;observed.physicalDevice=physical;observed.queue=queue;observed.queueFamily=family;observed.swapchain=swap;observed.image=colors[index];
  auto& r=observed.request;r.acquiredObserved=true;r.arrayLayers=1;r.fgStateKnown=true;r.format=swapci.imageFormat;r.colorSpace=swapci.imageColorSpace;r.extent={64,64};r.imageUsage=swapci.imageUsage;r.imageIndex=index;const auto actualContext=GetVulkanPresentRegistry().TaggedImageContext(colors[index],VK_FORMAT_UNDEFINED,{64,64});Check(bool(actualContext),"exact acquired image context");r.deviceGeneration=actualContext->deviceGeneration;r.swapchainGeneration=actualContext->swapchainGeneration;r.acquireGeneration=actualContext->acquireGeneration;
  if(frame==6||frame==15)++r.acquireGeneration; // both exact and estimated paths must refuse stale identity
  auto& c=observed.capabilities;c.queueObserved=true;c.presentQueueAccessQualified=true;c.queueFlags=VK_QUEUE_GRAPHICS_BIT;c.swapchainPNextKnown=true;
  VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.waitSemaphoreCount=1;pi.pWaitSemaphores=&rendered;pi.swapchainCount=1;pi.pSwapchains=&swap;pi.pImageIndices=&index;
  // An explicit registry-only second-window observation: never passed to Vulkan.
  auto secondSwap=(VkSwapchainKHR)(uintptr_t)0x9876;
  if(frame==14)GetVulkanPresentRegistry().SwapchainCreated(device,secondSwap,swapci,caps.supportedUsageFlags,true,{}, {family});
  // Explicit prior-queue metadata seam; fake queue is never submitted to Vulkan.
  expectQueueReset=frame==24;
  if(expectQueueReset){auto& native=*NativeVulkanGuides::devices.at(device);native.previousQueue=reinterpret_cast<VkQueue>(uintptr_t(0x9877));native.resetPending=false;}
  if(liveFallback&&(frame==0||frame==2||frame==3)){
   NativeGuides::ConfigureCpuFallback(frame!=2);
   Check(NativeGuides::SelectedCpuFallback()==(frame!=2),"live permission changes on an existing device and submitted frame");
  }
  bool handled=false;
  if(frame!=26)VkCheck(NativeVulkanGuides::BeforePresent(observed,pi,{},&handled),"actual capture and copyback");
  Connections::PolicyWire owner;
  Check(NeuRotic_QueryConnectionPolicyV1(&owner,sizeof(owner))!=0,"actual ownership telemetry");
  if(frame<28)Check(owner.claimedSource==0&&!owner.unsafe,"completed or refused frame releases connection owner");
  if(!cpuRefused&&frame!=26)Check(handled==!refused,"dispatch reports consumed Present work exactly once");
  if(frame>=28&&!cpuRefused)Check(owner.claimedSource==2&&owner.unsafe,"unsafe processor retains quarantined owner");
  if(selectedTransport==2)Check(sharedAttempts==0,"explicit CPU transport never acquires shared GPU resources");
  if(cpuRefused){
   Check(calls==0&&pi.waitSemaphoreCount==1,"GPU-only or disabled fallback preserves original frame without CPU processing");
   Check(NativeVulkanGuides::Status().reason.find("CPU fallback")!=std::string::npos,"transport refusal explains CPU fallback policy");
   VkCheck(vkQueuePresentKHR(queue,&pi),"transport-refused original Present");
   VkCheck(vkQueueWaitIdle(queue),"transport-refused Present idle");break;
  }
  Check(NativeVulkanGuides::Enabled(),"native observation restored after injection or refusal");
  if(frame==14)GetVulkanPresentRegistry().SwapchainDestroyed(secondSwap);
  Check(pi.waitSemaphoreCount==(refused?1u:0u),"owned completed submission consumes waits; refusal preserves waits");
  Check(NativeVulkanGuides::devices.at(device)->allocated<=NativeVulkanGuides::MaxBytes&&NativeVulkanGuides::devices.at(device)->depths.size()<=NativeVulkanGuides::MaxCaptures,"bounded owner memory and retained captures");
  auto status=NativeVulkanGuides::Status();std::printf("FRAME %u captured=%llu delivered=%llu reason=%s\n",frame,status.captured,status.delivered,status.reason.c_str());
  if(frame!=26){
   const auto effective=PreparedGuides::QueryStatusV2(GetTickCount64());
   Check(effective.available&&effective.fresh&&effective.status.sourceApi==0x20000,"Vulkan publishes into existing effective connection status");
   if(frame==0){Check(effective.status.modelPreparing,"first model frame reports preparation");preparingStatus=effective.status;}
   Check(bool(effective.status.outputValid)==(!refused&&frame!=0&&frame!=28),"effective output follows completed copyback, not a selected toggle");
   if(effective.status.guideReady)Check(effective.status.depthOrigin==PreparedGuides::Origin::Observed&&effective.status.motionOrigin==PreparedGuides::Origin::Derived,
    "captured depth and estimated motion keep their actual provenance");
   if(effective.status.outputValid){
    Check(effective.status.timingDomain==PreparedGuides::ClockDomain::CpuQpc&&
     effective.status.timingInterval==effective.status.capture,
     "processing duration is scoped to completed capture and explicitly uses the CPU clock");
    const auto signals=Connections::QueryCompletedInputSignals(GetTickCount64());
    Check(signals.usedFresh&&signals.used.consumed&&signals.used.color==2&&signals.used.depth==2&&signals.used.motion==2,
     "completed Vulkan capture publishes actual color, depth and derived-motion use");
    Check(signals.used.capture==effective.status.capture&&signals.used.generation==effective.status.generation,
     "signal completion belongs to the same capture and generation");
   }
   if(frame==0)Check(!Connections::QueryCompletedInputSignals(GetTickCount64()).usedFresh,
    "preparing is not completed signal consumption");
   if(frame==1){
    Check(!effective.status.workWidth&&!effective.status.workHeight,"capture raster is not invented model dimensions");
    NativeVulkanGuides::ModelDimensions(observed,48,32);
    Check(PreparedGuides::QueryStatusV2(GetTickCount64()).status.workWidth==48,"completed consumer reports actual model raster");
    auto stale=observed;++stale.request.acquireGeneration;
    NativeVulkanGuides::ModelDimensions(stale,80,80);
    Check(PreparedGuides::QueryStatusV2(GetTickCount64()).status.workWidth==48,"late model dimensions cannot overwrite another acquisition");
   }
  }
  if(frame==9)Check(status.estimatedAssociations==1&&status.reason.find("estimated offscreen")!=std::string::npos,"offscreen provenance remains estimated");
  if(frame==7)Check(status.reason.find("one-time primary")!=std::string::npos,"specific recording rejection survives Present");
  if(frame==19)Check(status.reason.find("ambiguous")!=std::string::npos,"suppressed later version invalidates older selected source");
  Check(calls==int(frame>=28?15:frame>=25?14:frame>=23?frame-11:frame>=20?frame-10:frame>=18?9:frame<3?frame+1:frame==3?3:frame>=14?8:frame>=11?frame-5:frame>=9?5:4)-(liveFallback&&frame>=2?1:0),"connected Process call / rejected depth does not process");
  if(refused){VkCheck(vkQueuePresentKHR(queue,&pi),"refused original Present");VkCheck(vkQueueWaitIdle(queue),"refused Present idle");PollVkNrCompletions();continue;}
  auto& nativeDevice=*NativeVulkanGuides::devices.at(device);
  Check(NativeVulkanGuides::Begin(nativeDevice),"verification begin");
  NativeVulkanGuides::Move(nativeDevice,nativeDevice.command,colors[index],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_ASPECT_COLOR_BIT);
  VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={64,64,1};vkCmdCopyImageToBuffer(nativeDevice.command,colors[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,nativeDevice.color.buffer,1,&copy);NativeVulkanGuides::ToHost(nativeDevice,nativeDevice.command,nativeDevice.color);
  NativeVulkanGuides::Move(nativeDevice,nativeDevice.command,colors[index],VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_ASPECT_COLOR_BIT);
  VkCheck(NativeVulkanGuides::Submit(nativeDevice,queue,nullptr),"verification read completion");const auto* actual=static_cast<const uint8_t*>(nativeDevice.color.mapped);
  Check((frame==0||frame==28)?actual[0]>=190&&actual[0]<=192:actual[0]==10&&actual[1]==20&&actual[2]==30,"preparing preserves original; delivered changes actual swapchain bytes");
  VkCheck(vkQueuePresentKHR(queue,&pi),"real Present");VkCheck(vkQueueWaitIdle(queue),"Present idle");PollVkNrCompletions();
 }
 NativeVulkanGuides::Unavailable("NR disabled after failure");
 Check(!PreparedGuides::QueryStatusV2(GetTickCount64()).status.outputValid&&!PreparedGuides::QueryStatusV2(GetTickCount64()).status.guideReady,"disable clears effective active and guide indicators immediately");
 if(!cpuRefused)Check(NativeVulkanGuides::Status().reason.find("fixture permanent core failure")!=std::string::npos,"processor reason survives NR disable and later capture refusals");
 // Diagnostic-only dynamic-rendering observation; no Vulkan command is issued.
 VkRenderingAttachmentInfo diagnosticDepth{VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO};diagnosticDepth.imageView=depthView;diagnosticDepth.imageLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;diagnosticDepth.loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;diagnosticDepth.storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
 VkRenderingInfo diagnosticScope{VK_STRUCTURE_TYPE_RENDERING_INFO};diagnosticScope.layerCount=1;diagnosticScope.pDepthAttachment=&diagnosticDepth;
 NativeVulkanGuides::BeginDynamic(cb,&diagnosticScope);
 Check(NativeVulkanGuides::Status().captureReason=="Dynamic depth store operation discards contents","dynamic discarded depth reports actual rejection");
 // Reuse the status actually produced by the first preparing frame, without
 // altering any GPU work. Failed Present/teardown must retire these live facts.
 if(!cpuRefused)NativeVulkanGuides::devices.at(device)->effective=preparingStatus;
 NativeVulkanGuides::AfterPresent(device,VK_ERROR_OUT_OF_DATE_KHR);Check(NativeVulkanGuides::devices.at(device)->resetPending,"failed physical Present resets future model history");
 auto failedStatus=PreparedGuides::QueryStatusV2(GetTickCount64());
 Check(!failedStatus.status.modelPreparing&&!failedStatus.status.guideReady&&!failedStatus.status.outputValid,"failed Present clears preparation as well as output facts");
 Check(std::string(failedStatus.status.reason).find("unsuccessful")!=std::string::npos,"effective readout explains failed Present instead of repeating preparation");
 if(!cpuRefused)NativeVulkanGuides::devices.at(device)->effective=preparingStatus;
 VkCheck(vkDeviceWaitIdle(device),"fixture teardown idle");vkDestroyCommandPool(device,pool,nullptr);VulkanNrRecordings().OnDestroyPool(device,pool);PollVkNrCompletions();NativeVulkanGuides::DeviceDestroyed(device);
 auto destroyedStatus=PreparedGuides::QueryStatusV2(GetTickCount64());
 Check(!destroyedStatus.status.modelPreparing&&!destroyedStatus.status.creationReady&&!destroyedStatus.status.guideReady&&!destroyedStatus.status.outputValid,"destroyed device cannot report active model preparation");
 for(auto f:fbs)vkDestroyFramebuffer(device,f,nullptr);vkDestroyPipeline(device,pipeline,nullptr);vkDestroyPipelineLayout(device,layout,nullptr);vkDestroyShaderModule(device,shader,nullptr);vkDestroyRenderPass(device,pass,nullptr);
 for(auto f:compatibleFbs)vkDestroyFramebuffer(device,f,nullptr);vkDestroyRenderPass(device,compatiblePass,nullptr);
 vkDestroyRenderPass(device,loadPass,nullptr);vkDestroyRenderPass(device,inlinePass,nullptr);
 for(auto f:layeredFbs)vkDestroyFramebuffer(device,f,nullptr);vkDestroyImageView(device,layeredView,nullptr);vkDestroyImage(device,layered,nullptr);vkFreeMemory(device,layeredMemory,nullptr);
 for(auto v:colorViews)vkDestroyImageView(device,v,nullptr);vkDestroyImageView(device,depthView,nullptr);vkDestroyImage(device,depth,nullptr);vkFreeMemory(device,memory,nullptr);vkDestroySemaphore(device,acquire,nullptr);vkDestroySemaphore(device,rendered,nullptr);vkDestroySwapchainKHR(device,swap,nullptr);
 for(auto f:otherFbs)vkDestroyFramebuffer(device,f,nullptr);vkDestroyImageView(device,otherView,nullptr);vkDestroyImage(device,otherDepth,nullptr);vkFreeMemory(device,otherMemory,nullptr);
 vkDestroyFramebuffer(device,depthFb,nullptr);vkDestroyRenderPass(device,depthPass,nullptr);vkDestroyPipeline(device,depthPipeline,nullptr);
 vkDestroyFramebuffer(device,bigFb,nullptr);vkDestroyRenderPass(device,bigPass,nullptr);vkDestroyPipeline(device,bigPipeline,nullptr);vkDestroyImageView(device,bigView,nullptr);vkDestroyImage(device,bigDepth,nullptr);vkFreeMemory(device,bigMemory,nullptr);
 vkDestroyFramebuffer(device,deferredFb,nullptr);vkDestroyRenderPass(device,deferredPass,nullptr);vkDestroyPipeline(device,deferredPipeline,nullptr);vkDestroyImageView(device,extraView,nullptr);vkDestroyImage(device,extraColor,nullptr);vkFreeMemory(device,extraMemory,nullptr);
 for(auto f:deferredSwapFbs)vkDestroyFramebuffer(device,f,nullptr);
 vkDestroyFramebuffer(device,otherOffFb,nullptr);vkDestroyFramebuffer(device,offFb,nullptr);vkDestroyRenderPass(device,offPass,nullptr);vkDestroyImageView(device,offView,nullptr);vkDestroyImage(device,offscreen,nullptr);vkFreeMemory(device,offMemory,nullptr);
 ObserveVkNrDeviceDestroy(device,[&]{vkDestroyDevice(device,nullptr);});vkDestroySurfaceKHR(instance,surface,nullptr);vkDestroyInstance(instance,nullptr);DestroyWindow(window);
 std::puts("PASS real Vulkan native depth/current-color/readback/preparation/completed copyback");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
