#include "pch.h"
#include "dlssnr/RenderingOutput.h"
#include "menu/menu_common.h"
#include "dlssnr/FinalFallbackControl.h"
#include "nr/semantic/character/CharacterCaptureVk.h"
#include "framegen/IFGFeature.h"
#if defined(NR_DIAG_VULKAN_NO_SUBMIT) && NR_DIAG_VULKAN_NO_SUBMIT
#if !defined(NR_DIAG_VULKAN_COMMAND_ONLY) || !NR_DIAG_VULKAN_COMMAND_ONLY
#error Submission isolation requires the command diagnostic bootstrap
#endif
#endif
#if defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY
#if !defined(NR_DIAG_VULKAN_NO_OBSERVERS) || !NR_DIAG_VULKAN_NO_OBSERVERS
#error Command isolation requires the no-observers diagnostic bootstrap
#endif
#endif
#if defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS
#if !defined(NR_DIAG_VULKAN_ONLY) || !NR_DIAG_VULKAN_ONLY || !defined(NR_DIAG_VULKAN_NO_LEGACY) || !NR_DIAG_VULKAN_NO_LEGACY || !defined(NR_DIAG_VULKAN_NO_AUGMENT) || !NR_DIAG_VULKAN_NO_AUGMENT
#error Vulkan observer isolation requires the no-augmentation diagnostic bootstrap
#endif
#endif
#include <nr/diagnostics/HostCost.h>
#include <dlssnr/DlssNrFeature_Vk.h>
#include <dlssnr/DlssNr.h>
#include <dlssnr/DlssNr_Present.h>
#include <dlssnr/PreparedGuideRoute.h>
#include <dlssnr/NativeVulkanGuides.h>
#include <dlssnr/NativeGuideRoute.h>

#include "Vulkan_Hooks.h"
#include "Streamline_Hooks.h"
#include "VulkanwDx12_Hooks.h"
#include <dlssnr/VulkanPresentGuidesVk.h>
#include <dlssnr/VulkanNrPresentAssociation.h>
#include <dlssnr/VulkanPresentConsumer.h>
#include <dlssnr/VulkanNrImageFacts.h>
#include <dlssnr/VulkanNrStreamline.h>
#include <dlssnr/VulkanNrFfxFg.h>
#include <dlssnr/VulkanNrCaptureVk.h>

#include <Util.h>
#include <Config.h>
#include <SysUtils.h>

#include <menu/menu_overlay_vk.h>
#include <proxies/KernelBase_Proxy.h>
#include <upscaler_time/UpscalerTime_Vk.h>

#include <misc/FrameLimit.h>
#include "Reflex_Hooks.h"

#include <spoofing/Vulkan_Spoofing.h>

#include <vulkan/vulkan.hpp>

#include <dlssnr/DlssNr_VkExtensions.h>
#include <dlssnr/VulkanPresentAdmission.h>
#include <dlssnr/VulkanPresentRegistry.h>
#include <dlssnr/VulkanPresentExecutor.h>
#include <dlssnr/VulkanPresentStatus.h>
#include <dlssnr/DlssNr_PresentInputPolicy.h>

#include <detours/detours.h>
#include <misc/IdentifyGpu.h>
#include <optional>
#include <chrono>
#include <dlssnr/VulkanNrRecording.h>
#include <dlssnr/VulkanNrFlightRecorder.h>
#include <intrin.h>
#include <dlssnr/VulkanNrCompletion.h>
#include <dlssnr/VulkanNrAcquireFence.h>
#include <dlssnr/VulkanNrDeviceSubmitHooks.h>
#include <dlssnr/VulkanWsiDispatch.h>
#include <dlssnr/NativeFgVulkan.h>
#include <dlssnr/NativeFgQueues.h>
#include <dlssnr/NativeFgMaintenance.h>
#include <dlssnr/NativeFgLayouts.h>

#include "Hook_Utils.h"

// for menu rendering
static VkDevice _device = VK_NULL_HANDLE;
static VkInstance _instance = VK_NULL_HANDLE;
static VkPhysicalDevice _PD = VK_NULL_HANDLE;
static HWND _hwnd = nullptr;
struct LoaderSurface { VkInstance instance=VK_NULL_HANDLE; HWND window=nullptr; };
static std::mutex loaderSurfaceMutex;
static std::unordered_map<uintptr_t,std::vector<LoaderSurface>> loaderSurfaces;

static std::mutex _vkPresentMutex;

PFN_vkCreateDevice o_vkCreateDevice = nullptr;
PFN_vkCreateInstance o_vkCreateInstance = nullptr;
PFN_vkCreateWin32SurfaceKHR o_vkCreateWin32SurfaceKHR = nullptr;
static PFN_vkDestroySurfaceKHR o_DestroySurfaceKHR = nullptr;
PFN_vkQueuePresentKHR o_QueuePresentKHR = nullptr;
PFN_vkCreateSwapchainKHR o_CreateSwapchainKHR = nullptr;
static PFN_vkAcquireNextImageKHR o_AcquireNextImageKHR = nullptr;
static PFN_vkAcquireNextImage2KHR o_AcquireNextImage2KHR = nullptr;
static PFN_vkGetInstanceProcAddr o_vkGetInstanceProcAddr = nullptr;
static PFN_vkDestroyInstance preparedDestroyInstance=nullptr;
static void VKAPI_CALL PreparedDestroyInstance(VkInstance instance,const VkAllocationCallbacks* allocator)
{
    DlssNr::GetVulkanPresentRegistry().InstanceDestroyed(instance);
    auto target=DlssNr::VkWsiTargetScope<PFN_vkDestroyInstance>::Get(preparedDestroyInstance);
    DlssNr::VkWsiTargetScope<PFN_vkDestroyInstance> clear(nullptr);
    if(target)target(instance,allocator);
}
static PFN_vkGetDeviceProcAddr o_vkGetDeviceProcAddr = nullptr;
static PFN_vkGetSwapchainImagesKHR o_GetSwapchainImagesKHR = nullptr;
static PFN_vkGetDeviceQueue o_GetDeviceQueue = nullptr;
static PFN_vkGetDeviceQueue2 o_GetDeviceQueue2 = nullptr;
static PFN_vkGetFenceStatus o_GetFenceStatus = nullptr;
static PFN_vkWaitForFences o_WaitForFences = nullptr;
static PFN_vkResetFences o_ResetFences = nullptr;
static PFN_vkDestroyFence o_DestroyFence = nullptr;

static void HookDeviceSubmissions(VkDevice device)
{
#if defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS
    return;
#else
    if(!o_vkGetDeviceProcAddr)return;
    for(const auto* name:{"vkQueueSubmit","vkQueueSubmit2","vkQueueSubmit2KHR"}){
        // Use the original resolver. The public resolver returns our wrapper,
        // which must never be installed as its own downstream target.
        const auto target=o_vkGetDeviceProcAddr(device,name);
        if(!target)continue;
        const auto exported=reinterpret_cast<PFN_vkVoidFunction>(KernelBaseProxy::GetProcAddress_()(vulkanModule,name));
        if(target==exported||Vulkan_wDx12::IsSubmitHookAddress(target)){
            LOG_INFO("Vulkan NR device submission hook: {} already covered by loader entry",name);continue;
        }
        int status=0;
        if(std::strcmp(name,"vkQueueSubmit")==0)
            status=static_cast<int>(DlssNr::VkNrDeviceSubmit::Install(reinterpret_cast<PFN_vkQueueSubmit>(target)));
        else status=static_cast<int>(DlssNr::VkNrDeviceSubmit2::Install(reinterpret_cast<PFN_vkQueueSubmit2>(target)));
        // Result order: installed, existing, unavailable, capacity, failure.
        if(status<=1)LOG_INFO("Vulkan NR device submission hook: {} {}",name,status==0?"installed":"shared target already covered");
        else LOG_WARN("Vulkan NR device submission hook: {} unavailable status={}",name,status);
    }
#endif
}

// Native loader trampolines retain the application's Vulkan dispatch. Proc-address
// aliases and the optional bridge share the same NR observation scope.
static PFN_vkCreateCommandPool nr_CreateCommandPool = nullptr;
static PFN_vkAllocateCommandBuffers nr_AllocateCommandBuffers = nullptr;
static PFN_vkBeginCommandBuffer nr_BeginCommandBuffer = nullptr;
static PFN_vkEndCommandBuffer nr_EndCommandBuffer = nullptr;
static PFN_vkResetCommandBuffer nr_ResetCommandBuffer = nullptr;
static PFN_vkResetCommandPool nr_ResetCommandPool = nullptr;
static PFN_vkFreeCommandBuffers nr_FreeCommandBuffers = nullptr;
static PFN_vkDestroyCommandPool nr_DestroyCommandPool = nullptr;
static PFN_vkCmdExecuteCommands nr_CmdExecuteCommands = nullptr;
static PFN_vkDestroyDevice nr_DestroyDevice = nullptr;
static PFN_vkQueueWaitIdle nr_QueueWaitIdle=nullptr;
static PFN_vkDeviceWaitIdle nr_DeviceWaitIdle=nullptr;
static PFN_vkDestroySwapchainKHR nr_DestroySwapchainKHR = nullptr;
static PFN_vkCreateImage nr_CreateImage = nullptr;
static PFN_vkCreateImageView nr_CreateImageView = nullptr;
static PFN_vkDestroyImage nr_DestroyImage = nullptr;
static PFN_vkDestroyImageView nr_DestroyImageView = nullptr;
static PFN_vkCreateRenderPass nr_CreateRenderPass=nullptr;
static PFN_vkCreateRenderPass2 nr_CreateRenderPass2=nullptr;
static PFN_vkCreateRenderPass2KHR nr_CreateRenderPass2KHR=nullptr;
static PFN_vkDestroyRenderPass nr_DestroyRenderPass=nullptr;
static PFN_vkCreateFramebuffer nr_CreateFramebuffer=nullptr;
static PFN_vkDestroyFramebuffer nr_DestroyFramebuffer=nullptr;
#if defined(NR_DIAG_VULKAN_NO_SUBMIT) && NR_DIAG_VULKAN_NO_SUBMIT
#define NR_RECORDING_FUNCTIONS(X) \
    X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(ResetCommandBuffer) X(ResetCommandPool) X(FreeCommandBuffers) X(DestroyCommandPool) X(CmdExecuteCommands) X(DestroyDevice) X(QueueWaitIdle) X(DeviceWaitIdle) \
    X(CreateImage) X(CreateImageView) X(DestroyImage) X(DestroyImageView) X(DestroySwapchainKHR) \
    X(CreateRenderPass) X(CreateRenderPass2) X(CreateRenderPass2KHR) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer)
#elif defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY
#define NR_RECORDING_FUNCTIONS(X) \
    X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(ResetCommandBuffer) X(ResetCommandPool) X(FreeCommandBuffers) X(DestroyCommandPool) X(CmdExecuteCommands) \
    X(DestroyDevice) X(DestroySwapchainKHR) X(QueueWaitIdle) X(DeviceWaitIdle)
#elif defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS
// Keep the device/swapchain destruction hooks paired with retained WSI registration.
#define NR_RECORDING_FUNCTIONS(X) X(DestroyDevice) X(DestroySwapchainKHR) X(QueueWaitIdle) X(DeviceWaitIdle)
#else
#define NR_RECORDING_FUNCTIONS(X) \
    X(CreateCommandPool) X(AllocateCommandBuffers) X(BeginCommandBuffer) X(EndCommandBuffer) \
    X(ResetCommandBuffer) X(ResetCommandPool) X(FreeCommandBuffers) X(DestroyCommandPool) X(CmdExecuteCommands) X(DestroyDevice) X(QueueWaitIdle) X(DeviceWaitIdle) \
    X(CreateImage) X(CreateImageView) X(DestroyImage) X(DestroyImageView) X(DestroySwapchainKHR) \
    X(CreateRenderPass) X(CreateRenderPass2) X(CreateRenderPass2KHR) X(DestroyRenderPass) X(CreateFramebuffer) X(DestroyFramebuffer)
#endif

static VkResult VKAPI_CALL nrQueueWaitIdle(VkQueue queue){
    if(!DlssNr::NativeFg::Internal()&&!DlssNr::NativeFg::DrainQueue(queue,false))return VK_ERROR_DEVICE_LOST;
    const auto result=DlssNr::NativeFg::QueueCall(nr_QueueWaitIdle,queue);
    if(result==VK_SUCCESS&&!DlssNr::NativeFg::Internal())DlssNr::NativeFg::DrainQueue(queue);
    return result; // WSI retirement is separate from queue idle; pending is not device loss.
}
static VkResult VKAPI_CALL nrDeviceWaitIdle(VkDevice device){
    if(!DlssNr::NativeFg::Internal()&&!DlssNr::NativeFg::DrainDevice(device,false))return VK_ERROR_DEVICE_LOST;
    const auto result=DlssNr::NativeFg::QueueCall(nr_DeviceWaitIdle,device);
    if(result==VK_SUCCESS&&!DlssNr::NativeFg::Internal())DlssNr::NativeFg::DrainDevice(device);
    return result;
}
static void VKAPI_CALL nrDestroySwapchainKHR(VkDevice device,VkSwapchainKHR swap,const VkAllocationCallbacks* allocator){
    // Removes CPU observation holds only. Private Present slots retain their separate WSI obligations.
    auto target=DlssNr::VkWsiTargetScope<PFN_vkDestroySwapchainKHR>::Get(nr_DestroySwapchainKHR);
    if(DlssNr::NativeFg::Internal()){target(device,swap,allocator);return;}
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Destroy,(uintptr_t)device,swap)){
        target(device,swap,allocator);return;
    }
    DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::Destroy,(uintptr_t)device,swap);
    DlssNr::VkWsiTargetScope<PFN_vkDestroySwapchainKHR> clear(nullptr);
    DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,7,
        DlssNr::VulkanLoaderWsiDevices().Device(device).generation,(uintptr_t)swap,(uintptr_t)device,0,0,0,true);
    const auto fgDestroyed=DlssNr::NativeFg::Destroy(device,swap);
    if(fgDestroyed==false){LOG_WARN("Standalone FG swapchain retained during destruction");return;}
    MenuOverlayVk::ShutdownSwapchain(device,swap);
    DlssNr::VulkanLoaderWsiDevices().SwapchainDestroyed(device,swap);
    DlssNr::GetVulkanPresentRegistry().SwapchainDestroyed(swap);
    if(!fgDestroyed.has_value())target(device,swap,allocator);
    DlssNr::GetVulkanLoaderPresentRegistry().SwapchainDestroyed(swap);
}
static void VKAPI_CALL nrDestroyDevice(VkDevice device, const VkAllocationCallbacks* allocator)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkDestroyDevice>::Get(nr_DestroyDevice);
    if(DlssNr::NativeFg::Internal()){target(device,allocator);return;}
    if(!DlssNr::NativeFg::DestroyDevice(device)){LOG_WARN("Standalone FG device retained during destruction");return;}
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::DeviceDestroy,(uintptr_t)device,nullptr)){
        target(device,allocator);return;
    }
    DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,8,
        DlssNr::VulkanLoaderWsiDevices().Device(device).generation,(uintptr_t)device,0,0,0,0,true);
    MenuOverlayVk::ShutdownDevice(device);
    DlssNr::RetryShutdownVk(device);
    if (!DlssNr::ObserveVkNrDeviceDestroy(device,[&]{
        DlssNr::NativeVulkanGuides::DeviceDestroyed(device);
        DlssNr::VulkanGuidesDeviceDestroyed(device);DlssNr::VulkanNrImageFacts().DeviceDestroyed(device);
        DlssNr::GetVulkanPresentRegistry().DeviceDestroyed(device);
        DlssNr::GetVulkanLoaderPresentRegistry().DeviceDestroyed(device);
        DlssNr::GetVulkanApplicationPresentRegistry().DeviceDestroyed(device);
        DlssNr::VulkanLoaderWsiDevices().Destroyed(device);
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::DeviceDestroy,(uintptr_t)device,nullptr);
        DlssNr::VkWsiTargetScope<PFN_vkDestroyDevice> clear(nullptr);
        target(device,allocator);
        MenuOverlayVk::NotifyDeviceDestroyed(device);
    }))
        LOG_WARN("Vulkan NR: owned completion drain was unproved at device destruction");
    // Completion/recording owners have processed the actual destruction return.
    DlssNr::DeviceDestroyedVk(device);
}
static VkResult VKAPI_CALL nrCreateImage(VkDevice device,const VkImageCreateInfo* info,const VkAllocationCallbacks* allocator,VkImage* image)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::CreateImage,reinterpret_cast<uintptr_t>(info), device);
    auto prepared=info?DlssNr::NativeVulkanGuides::PrepareImage(device,*info):VkImageCreateInfo{};
    auto result=nr_CreateImage(device,info?&prepared:nullptr,allocator,image);
    if(result!=VK_SUCCESS&&info&&prepared.usage!=info->usage){prepared=*info;result=nr_CreateImage(device,info,allocator,image);}
    if(scope.Observe()&&info&&image&&result==VK_SUCCESS)DlssNr::VulkanNrImageFacts().Created(device,prepared,*image,result);
    return result;
}
static VkResult VKAPI_CALL nrCreateImageView(VkDevice device,const VkImageViewCreateInfo* info,const VkAllocationCallbacks* allocator,VkImageView* view)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::CreateView,reinterpret_cast<uintptr_t>(info), device);
    const auto result=nr_CreateImageView(device,info,allocator,view);
    if(result==VK_SUCCESS&&info&&view&&DlssNr::NativeFg::Selected())DlssNr::NativeFg::Layouts::View(device,*view,info->image);
    if(scope.Observe()&&info&&view&&result==VK_SUCCESS){DlssNr::VulkanNrImageFacts().ViewCreated(device,*info,*view,result);DlssNr::NativeVulkanGuides::ViewCreated(device,*info,*view);}
    return result;
}
static void VKAPI_CALL nrDestroyImage(VkDevice device,VkImage image,const VkAllocationCallbacks* allocator)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::DestroyImage,reinterpret_cast<uintptr_t>(image), device);
    nr_DestroyImage(device,image,allocator);if(scope.Observe())DlssNr::VulkanNrImageFacts().Destroyed(device,image);
}
static void VKAPI_CALL nrDestroyImageView(VkDevice device,VkImageView view,const VkAllocationCallbacks* allocator)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::DestroyView,reinterpret_cast<uintptr_t>(view), device);
    if(scope.Observe())DlssNr::NativeVulkanGuides::ViewDestroyed(device,view);
    DlssNr::NativeFg::Layouts::ForgetView(device,view);nr_DestroyImageView(device,view,allocator);if(scope.Observe())DlssNr::VulkanNrImageFacts().ViewDestroyed(device,view);
}

static VkResult VKAPI_CALL nrCreateRenderPass(VkDevice d,const VkRenderPassCreateInfo* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    auto r=DlssNr::NativeFg::Layouts::CreatePass(nr_CreateRenderPass,nr_DestroyRenderPass,d,ci,a,out,DlssNr::NativeFg::Selected()&&!DlssNr::NativeFg::Internal());if(r==VK_SUCCESS&&ci&&out)DlssNr::NativeVulkanGuides::RenderPassCreated(d,*ci,*out);return r;
}
static VkResult VKAPI_CALL nrCreateRenderPass2(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    auto r=DlssNr::NativeFg::Layouts::CreatePass(nr_CreateRenderPass2,nr_DestroyRenderPass,d,ci,a,out,DlssNr::NativeFg::Selected()&&!DlssNr::NativeFg::Internal());if(r==VK_SUCCESS&&ci&&out)DlssNr::NativeVulkanGuides::RenderPassCreated2(d,*ci,*out);return r;
}
static VkResult VKAPI_CALL nrCreateRenderPass2KHR(VkDevice d,const VkRenderPassCreateInfo2* ci,const VkAllocationCallbacks* a,VkRenderPass* out){
    auto r=DlssNr::NativeFg::Layouts::CreatePass(nr_CreateRenderPass2KHR,nr_DestroyRenderPass,d,ci,a,out,DlssNr::NativeFg::Selected()&&!DlssNr::NativeFg::Internal());if(r==VK_SUCCESS&&ci&&out)DlssNr::NativeVulkanGuides::RenderPassCreated2(d,*ci,*out);return r;
}
static void VKAPI_CALL nrDestroyRenderPass(VkDevice d,VkRenderPass p,const VkAllocationCallbacks* a){
    DlssNr::NativeVulkanGuides::RenderPassDestroyed(d,p);DlssNr::NativeFg::Layouts::DestroyPass(nr_DestroyRenderPass,d,p,a);
}
static VkResult VKAPI_CALL nrCreateFramebuffer(VkDevice d,const VkFramebufferCreateInfo* ci,const VkAllocationCallbacks* a,VkFramebuffer* out){
    auto r=nr_CreateFramebuffer(d,ci,a,out);if(r==VK_SUCCESS&&ci&&out&&DlssNr::NativeFg::Selected())DlssNr::NativeFg::Layouts::Framebuffer(d,*out,*ci);if(r==VK_SUCCESS&&ci&&out)DlssNr::NativeVulkanGuides::FramebufferCreated(d,*ci,*out);return r;
}
static void VKAPI_CALL nrDestroyFramebuffer(VkDevice d,VkFramebuffer p,const VkAllocationCallbacks* a){
    DlssNr::NativeFg::Layouts::ForgetFrame(d,p);DlssNr::NativeVulkanGuides::FramebufferDestroyed(d,p);nr_DestroyFramebuffer(d,p,a);
}
static VkResult VKAPI_CALL nrCreateCommandPool(VkDevice device, const VkCommandPoolCreateInfo* info,
                                               const VkAllocationCallbacks* allocator, VkCommandPool* pool)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::CreatePool, reinterpret_cast<uintptr_t>(info), device);
    const auto result = nr_CreateCommandPool(device, info, allocator, pool);
    if (scope.Observe() && info && pool && result == VK_SUCCESS)
        DlssNr::VulkanNrRecordings().OnCreatePool(device, *pool, *info, result);
    return result;
}
static VkResult VKAPI_CALL nrAllocateCommandBuffers(VkDevice device, const VkCommandBufferAllocateInfo* info,
                                                    VkCommandBuffer* buffers)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Allocate, reinterpret_cast<uintptr_t>(info), device);
    const auto result = nr_AllocateCommandBuffers(device, info, buffers);
    if (scope.Observe() && info) DlssNr::VulkanNrRecordings().OnAllocateBuffers(device, *info, buffers, result);
    return result;
}
static VkResult VKAPI_CALL nrBeginCommandBuffer(VkCommandBuffer buffer, const VkCommandBufferBeginInfo* info)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Begin, reinterpret_cast<uintptr_t>(buffer));
    const auto result = nr_BeginCommandBuffer(buffer, info);
    if (scope.Observe() && info) DlssNr::VulkanNrRecordings().OnBegin(buffer, info->flags, result);
    return result;
}
static VkResult VKAPI_CALL nrEndCommandBuffer(VkCommandBuffer buffer)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::End, reinterpret_cast<uintptr_t>(buffer));
    const auto result = nr_EndCommandBuffer(buffer);
    if (scope.Observe()) DlssNr::VulkanNrRecordings().OnEnd(buffer, result);
    return result;
}
static VkResult VKAPI_CALL nrResetCommandBuffer(VkCommandBuffer buffer, VkCommandBufferResetFlags flags)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Reset, reinterpret_cast<uintptr_t>(buffer));
    const auto result = nr_ResetCommandBuffer(buffer, flags);
    if (scope.Observe()) {
        DlssNr::VulkanNrRecordings().OnReset(buffer,result);
        const auto device=DlssNr::VulkanNrRecordings().CommandDevice(buffer);
        if(result==VK_SUCCESS&&device)DlssNr::RetryShutdownVk(device);
    }
    return result;
}
static VkResult VKAPI_CALL nrResetCommandPool(VkDevice device, VkCommandPool pool, VkCommandPoolResetFlags flags)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::ResetPool, reinterpret_cast<uintptr_t>(pool), device);
    const auto result = nr_ResetCommandPool(device, pool, flags);
    if (scope.Observe()) {DlssNr::VulkanNrRecordings().OnResetPool(device, pool, result);
        if(result==VK_SUCCESS)DlssNr::RetryShutdownVk(device);}
    return result;
}
static void VKAPI_CALL nrFreeCommandBuffers(VkDevice device, VkCommandPool pool, uint32_t count, const VkCommandBuffer* buffers)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Free, reinterpret_cast<uintptr_t>(pool), device);
    nr_FreeCommandBuffers(device, pool, count, buffers);
    if (scope.Observe() && buffers) for (uint32_t i = 0; i < count; ++i) DlssNr::VulkanNrRecordings().OnFree(buffers[i]);
    if(scope.Observe())DlssNr::RetryShutdownVk(device);
}
static void VKAPI_CALL nrDestroyCommandPool(VkDevice device, VkCommandPool pool, const VkAllocationCallbacks* allocator)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::DestroyPool, reinterpret_cast<uintptr_t>(pool), device);
    nr_DestroyCommandPool(device, pool, allocator);
    if (scope.Observe()) {DlssNr::VulkanNrRecordings().OnDestroyPool(device, pool);DlssNr::RetryShutdownVk(device);}
}
static void VKAPI_CALL nrCmdExecuteCommands(VkCommandBuffer buffer, uint32_t count, const VkCommandBuffer* children)
{
    DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Execute, reinterpret_cast<uintptr_t>(buffer));
    nr_CmdExecuteCommands(buffer, count, children);
    if (scope.Observe() && children){DlssNr::NativeVulkanGuides::InvalidateDepthHistory(buffer);DlssNr::VulkanNrRecordings().OnExecute(buffer, {children, count});DlssNr::VulkanNrRecordings().OnWork(buffer);}
}

static PFN_vkVoidFunction NrRecordingAddress(const char* name)
{
#define NR_RECORDING_ADDRESS(Name) if (nr_##Name && std::strcmp(name, "vk" #Name) == 0) return reinterpret_cast<PFN_vkVoidFunction>(nr##Name)
#if defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS && !(defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY)
    NR_RECORDING_ADDRESS(QueueWaitIdle);NR_RECORDING_ADDRESS(DeviceWaitIdle);
    NR_RECORDING_ADDRESS(DestroyDevice); NR_RECORDING_ADDRESS(DestroySwapchainKHR);
#else
    NR_RECORDING_ADDRESS(CreateCommandPool); NR_RECORDING_ADDRESS(AllocateCommandBuffers);
    NR_RECORDING_ADDRESS(BeginCommandBuffer); NR_RECORDING_ADDRESS(EndCommandBuffer);
    NR_RECORDING_ADDRESS(ResetCommandBuffer); NR_RECORDING_ADDRESS(ResetCommandPool);
    NR_RECORDING_ADDRESS(FreeCommandBuffers); NR_RECORDING_ADDRESS(DestroyCommandPool); NR_RECORDING_ADDRESS(CmdExecuteCommands);
    NR_RECORDING_ADDRESS(QueueWaitIdle);NR_RECORDING_ADDRESS(DeviceWaitIdle);
    NR_RECORDING_ADDRESS(DestroyDevice);NR_RECORDING_ADDRESS(DestroySwapchainKHR);
#if !defined(NR_DIAG_VULKAN_NO_OBSERVERS) || !NR_DIAG_VULKAN_NO_OBSERVERS || (defined(NR_DIAG_VULKAN_NO_SUBMIT) && NR_DIAG_VULKAN_NO_SUBMIT)
    NR_RECORDING_ADDRESS(CreateImage);NR_RECORDING_ADDRESS(CreateImageView);NR_RECORDING_ADDRESS(DestroyImage);NR_RECORDING_ADDRESS(DestroyImageView);
    NR_RECORDING_ADDRESS(CreateRenderPass);NR_RECORDING_ADDRESS(CreateRenderPass2);NR_RECORDING_ADDRESS(CreateRenderPass2KHR);
    NR_RECORDING_ADDRESS(DestroyRenderPass);NR_RECORDING_ADDRESS(CreateFramebuffer);NR_RECORDING_ADDRESS(DestroyFramebuffer);
#endif
#endif
#undef NR_RECORDING_ADDRESS
    return nullptr;
}

template<class PFN,int Index> struct NrWorkHook;
template<int Index,class... Args> struct NrWorkHook<void(VKAPI_PTR*)(VkCommandBuffer,Args...),Index>{
    using Function=void(VKAPI_PTR*)(VkCommandBuffer,Args...);static inline Function original=nullptr;
    static void VKAPI_CALL Invoke(VkCommandBuffer cb,Args... args){
        DlssNr::VkNrObservationScope scope(DlssNr::VkNrObservation::Work,reinterpret_cast<uintptr_t>(cb));
        if constexpr(Index>=39&&Index<=41){if(DlssNr::NativeFg::Layouts::active.load(std::memory_order_relaxed))DlssNr::NativeFg::Layouts::Barrier(original,DlssNr::VulkanNrRecordings().CommandDevice(cb),cb,args...);else original(cb,args...);}
        else if constexpr(Index>=42&&Index<=44){if(DlssNr::NativeFg::Layouts::active.load(std::memory_order_relaxed))DlssNr::NativeFg::Layouts::Wait(original,DlssNr::VulkanNrRecordings().CommandDevice(cb),cb,args...);else original(cb,args...);}
        else original(cb,args...);
        if(scope.Observe()){
            if constexpr(Index<39)DlssNr::VulkanNrRecordings().OnWork(cb);
            if constexpr((Index<39||Index>=42)&&Index!=11&&Index!=12&&!((Index>=2&&Index<=5)||(Index>=24&&Index<=28)||(Index>=31&&Index<=34)||(Index>=36&&Index<=38)))
                DlssNr::NativeVulkanGuides::InvalidateDepthHistory(cb);
            if constexpr(Index>=39&&Index<=41)DlssNr::NativeVulkanGuides::Barrier(cb,args...);
            if constexpr(Index==11)DlssNr::NativeVulkanGuides::ClearDepthImage(cb,args...);
            if constexpr(Index==12)DlssNr::NativeVulkanGuides::ClearAttachments(cb,args...);
            if constexpr(Index==6||Index==13||Index==14)DlssNr::NativeVulkanGuides::ColorCopy(cb,args...);
            if constexpr(Index==8||Index==17||Index==18)DlssNr::NativeVulkanGuides::ColorBlit(cb,args...);
            if constexpr((Index>=2&&Index<=5)||(Index>=24&&Index<=28)||(Index>=31&&Index<=34)||(Index>=36&&Index<=38))
                DlssNr::NativeVulkanGuides::Draw(cb);
        }
    }
};
using NrWorkDispatch=NrWorkHook<PFN_vkCmdDispatch,0>;
using NrWorkDispatchIndirect=NrWorkHook<PFN_vkCmdDispatchIndirect,1>;
using NrWorkDraw=NrWorkHook<PFN_vkCmdDraw,2>;
using NrWorkDrawIndexed=NrWorkHook<PFN_vkCmdDrawIndexed,3>;
using NrWorkDrawIndirect=NrWorkHook<PFN_vkCmdDrawIndirect,4>;
using NrWorkDrawIndexedIndirect=NrWorkHook<PFN_vkCmdDrawIndexedIndirect,5>;
using NrWorkCopyImage=NrWorkHook<PFN_vkCmdCopyImage,6>;
using NrWorkCopyBufferToImage=NrWorkHook<PFN_vkCmdCopyBufferToImage,7>;
using NrWorkBlitImage=NrWorkHook<PFN_vkCmdBlitImage,8>;
using NrWorkResolveImage=NrWorkHook<PFN_vkCmdResolveImage,9>;
using NrWorkClearColorImage=NrWorkHook<PFN_vkCmdClearColorImage,10>;
using NrWorkClearDepthStencilImage=NrWorkHook<PFN_vkCmdClearDepthStencilImage,11>;
using NrWorkClearAttachments=NrWorkHook<PFN_vkCmdClearAttachments,12>;
using NrWorkCopyImage2=NrWorkHook<PFN_vkCmdCopyImage2,13>;
using NrWorkCopyImage2KHR=NrWorkHook<PFN_vkCmdCopyImage2KHR,14>;
using NrWorkCopyBufferToImage2=NrWorkHook<PFN_vkCmdCopyBufferToImage2,15>;
using NrWorkCopyBufferToImage2KHR=NrWorkHook<PFN_vkCmdCopyBufferToImage2KHR,16>;
using NrWorkBlitImage2=NrWorkHook<PFN_vkCmdBlitImage2,17>;
using NrWorkBlitImage2KHR=NrWorkHook<PFN_vkCmdBlitImage2KHR,18>;
using NrWorkResolveImage2=NrWorkHook<PFN_vkCmdResolveImage2,19>;
using NrWorkResolveImage2KHR=NrWorkHook<PFN_vkCmdResolveImage2KHR,20>;
using NrWorkTraceRaysKHR=NrWorkHook<PFN_vkCmdTraceRaysKHR,21>;
using NrWorkTraceRaysIndirectKHR=NrWorkHook<PFN_vkCmdTraceRaysIndirectKHR,22>;
using NrWorkTraceRaysIndirect2KHR=NrWorkHook<PFN_vkCmdTraceRaysIndirect2KHR,23>;
using NrWorkDrawMeshTasksEXT=NrWorkHook<PFN_vkCmdDrawMeshTasksEXT,24>;
using NrWorkDrawMeshTasksIndirectEXT=NrWorkHook<PFN_vkCmdDrawMeshTasksIndirectEXT,25>;
using NrWorkDrawMeshTasksIndirectCountEXT=NrWorkHook<PFN_vkCmdDrawMeshTasksIndirectCountEXT,26>;
using NrWorkDrawIndirectCount=NrWorkHook<PFN_vkCmdDrawIndirectCount,27>;
using NrWorkDrawIndexedIndirectCount=NrWorkHook<PFN_vkCmdDrawIndexedIndirectCount,28>;
using NrWorkDispatchBase=NrWorkHook<PFN_vkCmdDispatchBase,29>;
using NrWorkDispatchBaseKHR=NrWorkHook<PFN_vkCmdDispatchBaseKHR,30>;
using NrWorkDrawIndirectCountKHR=NrWorkHook<PFN_vkCmdDrawIndirectCountKHR,31>;
using NrWorkDrawIndexedIndirectCountKHR=NrWorkHook<PFN_vkCmdDrawIndexedIndirectCountKHR,32>;
using NrWorkDrawIndirectCountAMD=NrWorkHook<PFN_vkCmdDrawIndirectCountAMD,33>;
using NrWorkDrawIndexedIndirectCountAMD=NrWorkHook<PFN_vkCmdDrawIndexedIndirectCountAMD,34>;
using NrWorkTraceRaysNV=NrWorkHook<PFN_vkCmdTraceRaysNV,35>;
using NrWorkDrawMeshTasksNV=NrWorkHook<PFN_vkCmdDrawMeshTasksNV,36>;
using NrWorkDrawMeshTasksIndirectNV=NrWorkHook<PFN_vkCmdDrawMeshTasksIndirectNV,37>;
using NrWorkDrawMeshTasksIndirectCountNV=NrWorkHook<PFN_vkCmdDrawMeshTasksIndirectCountNV,38>;
using NrWorkPipelineBarrier=NrWorkHook<PFN_vkCmdPipelineBarrier,39>;
using NrWorkPipelineBarrier2=NrWorkHook<PFN_vkCmdPipelineBarrier2,40>;
using NrWorkPipelineBarrier2KHR=NrWorkHook<PFN_vkCmdPipelineBarrier2KHR,41>;
using NrWorkWaitEvents=NrWorkHook<PFN_vkCmdWaitEvents,42>;
using NrWorkWaitEvents2=NrWorkHook<PFN_vkCmdWaitEvents2,43>;
using NrWorkWaitEvents2KHR=NrWorkHook<PFN_vkCmdWaitEvents2KHR,44>;
static PFN_vkVoidFunction NrWorkAddress(const char* name,PFN_vkVoidFunction original){
#if defined(NR_DIAG_VULKAN_NO_OBSERVERS) && NR_DIAG_VULKAN_NO_OBSERVERS && !(defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY)
    return original;
#else
    if(std::strcmp(name,"vkCmdDispatchBase")==0){if(!NrWorkDispatchBase::original)NrWorkDispatchBase::original=reinterpret_cast<PFN_vkCmdDispatchBase>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDispatchBase::Invoke);}
    if(std::strcmp(name,"vkCmdDispatchBaseKHR")==0){if(!NrWorkDispatchBaseKHR::original)NrWorkDispatchBaseKHR::original=reinterpret_cast<PFN_vkCmdDispatchBaseKHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDispatchBaseKHR::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndirectCountKHR")==0){if(!NrWorkDrawIndirectCountKHR::original)NrWorkDrawIndirectCountKHR::original=reinterpret_cast<PFN_vkCmdDrawIndirectCountKHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndirectCountKHR::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndexedIndirectCountKHR")==0){if(!NrWorkDrawIndexedIndirectCountKHR::original)NrWorkDrawIndexedIndirectCountKHR::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCountKHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndexedIndirectCountKHR::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndirectCountAMD")==0){if(!NrWorkDrawIndirectCountAMD::original)NrWorkDrawIndirectCountAMD::original=reinterpret_cast<PFN_vkCmdDrawIndirectCountAMD>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndirectCountAMD::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndexedIndirectCountAMD")==0){if(!NrWorkDrawIndexedIndirectCountAMD::original)NrWorkDrawIndexedIndirectCountAMD::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCountAMD>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndexedIndirectCountAMD::Invoke);}
    if(std::strcmp(name,"vkCmdTraceRaysNV")==0){if(!NrWorkTraceRaysNV::original)NrWorkTraceRaysNV::original=reinterpret_cast<PFN_vkCmdTraceRaysNV>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkTraceRaysNV::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksNV")==0){if(!NrWorkDrawMeshTasksNV::original)NrWorkDrawMeshTasksNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksNV>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksNV::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksIndirectNV")==0){if(!NrWorkDrawMeshTasksIndirectNV::original)NrWorkDrawMeshTasksIndirectNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectNV>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksIndirectNV::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksIndirectCountNV")==0){if(!NrWorkDrawMeshTasksIndirectCountNV::original)NrWorkDrawMeshTasksIndirectCountNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountNV>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksIndirectCountNV::Invoke);}
    if(std::strcmp(name,"vkCmdPipelineBarrier")==0){if(!NrWorkPipelineBarrier::original)NrWorkPipelineBarrier::original=reinterpret_cast<PFN_vkCmdPipelineBarrier>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkPipelineBarrier::Invoke);}
    if(std::strcmp(name,"vkCmdPipelineBarrier2")==0){if(!NrWorkPipelineBarrier2::original)NrWorkPipelineBarrier2::original=reinterpret_cast<PFN_vkCmdPipelineBarrier2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkPipelineBarrier2::Invoke);}
    if(std::strcmp(name,"vkCmdPipelineBarrier2KHR")==0){if(!NrWorkPipelineBarrier2KHR::original)NrWorkPipelineBarrier2KHR::original=reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkPipelineBarrier2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdWaitEvents")==0){if(!NrWorkWaitEvents::original)NrWorkWaitEvents::original=reinterpret_cast<PFN_vkCmdWaitEvents>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkWaitEvents::Invoke);}
    if(std::strcmp(name,"vkCmdWaitEvents2")==0){if(!NrWorkWaitEvents2::original)NrWorkWaitEvents2::original=reinterpret_cast<PFN_vkCmdWaitEvents2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkWaitEvents2::Invoke);}
    if(std::strcmp(name,"vkCmdWaitEvents2KHR")==0){if(!NrWorkWaitEvents2KHR::original)NrWorkWaitEvents2KHR::original=reinterpret_cast<PFN_vkCmdWaitEvents2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkWaitEvents2KHR::Invoke);}

    if(std::strcmp(name,"vkCmdDispatch")==0){if(!NrWorkDispatch::original)NrWorkDispatch::original=reinterpret_cast<PFN_vkCmdDispatch>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDispatch::Invoke);}
    if(std::strcmp(name,"vkCmdDispatchIndirect")==0){if(!NrWorkDispatchIndirect::original)NrWorkDispatchIndirect::original=reinterpret_cast<PFN_vkCmdDispatchIndirect>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDispatchIndirect::Invoke);}
    if(std::strcmp(name,"vkCmdDraw")==0){if(!NrWorkDraw::original)NrWorkDraw::original=reinterpret_cast<PFN_vkCmdDraw>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDraw::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndexed")==0){if(!NrWorkDrawIndexed::original)NrWorkDrawIndexed::original=reinterpret_cast<PFN_vkCmdDrawIndexed>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndexed::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndirect")==0){if(!NrWorkDrawIndirect::original)NrWorkDrawIndirect::original=reinterpret_cast<PFN_vkCmdDrawIndirect>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndirect::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndexedIndirect")==0){if(!NrWorkDrawIndexedIndirect::original)NrWorkDrawIndexedIndirect::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndexedIndirect::Invoke);}
    if(std::strcmp(name,"vkCmdCopyImage")==0){if(!NrWorkCopyImage::original)NrWorkCopyImage::original=reinterpret_cast<PFN_vkCmdCopyImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyImage::Invoke);}
    if(std::strcmp(name,"vkCmdCopyBufferToImage")==0){if(!NrWorkCopyBufferToImage::original)NrWorkCopyBufferToImage::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyBufferToImage::Invoke);}
    if(std::strcmp(name,"vkCmdBlitImage")==0){if(!NrWorkBlitImage::original)NrWorkBlitImage::original=reinterpret_cast<PFN_vkCmdBlitImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkBlitImage::Invoke);}
    if(std::strcmp(name,"vkCmdResolveImage")==0){if(!NrWorkResolveImage::original)NrWorkResolveImage::original=reinterpret_cast<PFN_vkCmdResolveImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkResolveImage::Invoke);}
    if(std::strcmp(name,"vkCmdClearColorImage")==0){if(!NrWorkClearColorImage::original)NrWorkClearColorImage::original=reinterpret_cast<PFN_vkCmdClearColorImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkClearColorImage::Invoke);}
    if(std::strcmp(name,"vkCmdClearDepthStencilImage")==0){if(!NrWorkClearDepthStencilImage::original)NrWorkClearDepthStencilImage::original=reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkClearDepthStencilImage::Invoke);}
    if(std::strcmp(name,"vkCmdClearAttachments")==0){if(!NrWorkClearAttachments::original)NrWorkClearAttachments::original=reinterpret_cast<PFN_vkCmdClearAttachments>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkClearAttachments::Invoke);}
    if(std::strcmp(name,"vkCmdCopyImage2")==0){if(!NrWorkCopyImage2::original)NrWorkCopyImage2::original=reinterpret_cast<PFN_vkCmdCopyImage2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyImage2::Invoke);}
    if(std::strcmp(name,"vkCmdCopyImage2KHR")==0){if(!NrWorkCopyImage2KHR::original)NrWorkCopyImage2KHR::original=reinterpret_cast<PFN_vkCmdCopyImage2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyImage2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdCopyBufferToImage2")==0){if(!NrWorkCopyBufferToImage2::original)NrWorkCopyBufferToImage2::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyBufferToImage2::Invoke);}
    if(std::strcmp(name,"vkCmdCopyBufferToImage2KHR")==0){if(!NrWorkCopyBufferToImage2KHR::original)NrWorkCopyBufferToImage2KHR::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkCopyBufferToImage2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdBlitImage2")==0){if(!NrWorkBlitImage2::original)NrWorkBlitImage2::original=reinterpret_cast<PFN_vkCmdBlitImage2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkBlitImage2::Invoke);}
    if(std::strcmp(name,"vkCmdBlitImage2KHR")==0){if(!NrWorkBlitImage2KHR::original)NrWorkBlitImage2KHR::original=reinterpret_cast<PFN_vkCmdBlitImage2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkBlitImage2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdResolveImage2")==0){if(!NrWorkResolveImage2::original)NrWorkResolveImage2::original=reinterpret_cast<PFN_vkCmdResolveImage2>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkResolveImage2::Invoke);}
    if(std::strcmp(name,"vkCmdResolveImage2KHR")==0){if(!NrWorkResolveImage2KHR::original)NrWorkResolveImage2KHR::original=reinterpret_cast<PFN_vkCmdResolveImage2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkResolveImage2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdTraceRaysKHR")==0){if(!NrWorkTraceRaysKHR::original)NrWorkTraceRaysKHR::original=reinterpret_cast<PFN_vkCmdTraceRaysKHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkTraceRaysKHR::Invoke);}
    if(std::strcmp(name,"vkCmdTraceRaysIndirectKHR")==0){if(!NrWorkTraceRaysIndirectKHR::original)NrWorkTraceRaysIndirectKHR::original=reinterpret_cast<PFN_vkCmdTraceRaysIndirectKHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkTraceRaysIndirectKHR::Invoke);}
    if(std::strcmp(name,"vkCmdTraceRaysIndirect2KHR")==0){if(!NrWorkTraceRaysIndirect2KHR::original)NrWorkTraceRaysIndirect2KHR::original=reinterpret_cast<PFN_vkCmdTraceRaysIndirect2KHR>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkTraceRaysIndirect2KHR::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksEXT")==0){if(!NrWorkDrawMeshTasksEXT::original)NrWorkDrawMeshTasksEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksEXT::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksIndirectEXT")==0){if(!NrWorkDrawMeshTasksIndirectEXT::original)NrWorkDrawMeshTasksIndirectEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksIndirectEXT::Invoke);}
    if(std::strcmp(name,"vkCmdDrawMeshTasksIndirectCountEXT")==0){if(!NrWorkDrawMeshTasksIndirectCountEXT::original)NrWorkDrawMeshTasksIndirectCountEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawMeshTasksIndirectCountEXT::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndirectCount")==0){if(!NrWorkDrawIndirectCount::original)NrWorkDrawIndirectCount::original=reinterpret_cast<PFN_vkCmdDrawIndirectCount>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndirectCount::Invoke);}
    if(std::strcmp(name,"vkCmdDrawIndexedIndirectCount")==0){if(!NrWorkDrawIndexedIndirectCount::original)NrWorkDrawIndexedIndirectCount::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(original);return reinterpret_cast<PFN_vkVoidFunction>(NrWorkDrawIndexedIndirectCount::Invoke);}
    return original;
#endif
}
// Those aren't hooked, just grabbed for use
static PFN_vkGetPhysicalDeviceFeatures2 o_vkGetPhysicalDeviceFeatures2 = nullptr;
PFN_vkCreateSemaphore VulkanHooks::o_vkCreateSemaphore = nullptr;
PFN_vkSignalSemaphore VulkanHooks::o_vkSignalSemaphore = nullptr;
PFN_vkAntiLagUpdateAMD VulkanHooks::o_vkAntiLagUpdateAMD = nullptr;

namespace
{
std::mutex g_swapchainExtentMutex;
std::unordered_map<uintptr_t, VulkanPresentedExtent> g_swapchainExtents;
std::atomic<uint64_t> g_presentedExtent {};
}

VulkanPresentedExtent GetVulkanPresentedExtent()
{
    const uint64_t packed = g_presentedExtent.load(std::memory_order_acquire);
    return { static_cast<uint32_t>(packed >> 32), static_cast<uint32_t>(packed) };
}

// Forward declaration
static VkResult VKAPI_CALL hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo);
static VkResult VKAPI_CALL hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain);
static VkResult VKAPI_CALL hkvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                        VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex);
static VkResult VKAPI_CALL hkvkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                         uint32_t* pImageIndex);
PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device,const char* name);

// WSI interception is installed on loader exports and resolver returns. Never
// detour the first device's resolver address into a process-wide singleton.
static void HookDevice(VkDevice) {}

static void ObserveLoaderQueue(VkDevice device,VkQueue queue,uint32_t family,VkDeviceQueueCreateFlags flags)
{
    if(DlssNr::NativeFg::Internal())return;
    auto& devices=DlssNr::VulkanLoaderWsiDevices();devices.Queue(device,queue,family,flags);
    const auto d=devices.Device(device);
    if(d.generation&&family<d.families.size()&&!flags){
        DlssNr::GetVulkanLoaderPresentRegistry().QueueObserved(device,queue,family,d.families[family]);
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,2,d.generation,
            (uintptr_t)device,(uintptr_t)queue,family,d.families[family],0,true);
    }
}
static void VKAPI_CALL hkvkGetDeviceQueue(VkDevice device,uint32_t family,uint32_t index,VkQueue* queue)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkGetDeviceQueue>::Get(o_GetDeviceQueue);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Queue,(uintptr_t)device,queue)){target(device,family,index,queue);return;}
    {DlssNr::VkWsiForwardScope scope(DlssNr::VkWsiOperation::Queue,(uintptr_t)device,queue);DlssNr::VkWsiTargetScope<PFN_vkGetDeviceQueue> clear(nullptr);target(device,family,index,queue);}
    if(queue)ObserveLoaderQueue(device,*queue,family,0);
}
static void VKAPI_CALL hkvkGetDeviceQueue2(VkDevice device,const VkDeviceQueueInfo2* info,VkQueue* queue)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkGetDeviceQueue2>::Get(o_GetDeviceQueue2);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Queue2,(uintptr_t)device,info)){target(device,info,queue);return;}
    {DlssNr::VkWsiForwardScope scope(DlssNr::VkWsiOperation::Queue2,(uintptr_t)device,info);DlssNr::VkWsiTargetScope<PFN_vkGetDeviceQueue2> clear(nullptr);target(device,info,queue);}
    if(queue&&info)ObserveLoaderQueue(device,*queue,info->queueFamilyIndex,info->flags);
}
static VkResult VKAPI_CALL hkvkGetSwapchainImagesKHR(VkDevice device,VkSwapchainKHR swap,uint32_t* count,VkImage* images)
{
    // Owned standalone FG returns the SDK virtual image array; physical calls bypass it.
    auto target=DlssNr::VkWsiTargetScope<PFN_vkGetSwapchainImagesKHR>::Get(o_GetSwapchainImagesKHR);
    if(!DlssNr::NativeFg::Internal())if(const auto result=DlssNr::NativeFg::Images(device,swap,count,images))return *result;
    DlssNr::VkWsiTargetScope<PFN_vkGetSwapchainImagesKHR> clear(nullptr);
    return target(device,swap,count,images);
}

VALIDATE_HOOK(hkvkCreateWin32SurfaceKHR, PFN_vkCreateWin32SurfaceKHR)
static VkResult hkvkCreateWin32SurfaceKHR(VkInstance instance, const VkWin32SurfaceCreateInfoKHR* pCreateInfo,
                                          const VkAllocationCallbacks* pAllocator, VkSurfaceKHR* pSurface)
{
    LOG_FUNC();

    auto target=DlssNr::VkWsiTargetScope<PFN_vkCreateWin32SurfaceKHR>::Get(o_vkCreateWin32SurfaceKHR);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::SurfaceCreate,(uintptr_t)instance,pCreateInfo))
        return target(instance,pCreateInfo,pAllocator,pSurface);
    DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::SurfaceCreate,(uintptr_t)instance,pCreateInfo);
    DlssNr::VkWsiTargetScope<PFN_vkCreateWin32SurfaceKHR> clear(nullptr);
    auto result = target(instance, pCreateInfo, pAllocator, pSurface);
    if(result==VK_SUCCESS&&pSurface&&pCreateInfo){
        std::lock_guard lock(loaderSurfaceMutex);
        auto& candidates=loaderSurfaces[(uintptr_t)*pSurface];
        std::erase_if(candidates,[&](const auto& candidate){return candidate.instance==instance;});
        candidates.push_back({instance,pCreateInfo->hwnd});
    }

    auto procHwnd = Util::GetProcessWindow();
    LOG_DEBUG("procHwnd: {0:X}, swapchain hwnd: {1:X}", (UINT64) procHwnd, (UINT64) pCreateInfo->hwnd);

    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        MenuOverlayVk::DestroyVulkanObjects(false);

        _instance = instance;
        State::Instance().VulkanInstance = instance;
        LOG_DEBUG("_instance captured: {0:X}", (UINT64) _instance);
        _hwnd = pCreateInfo->hwnd;
        LOG_DEBUG("_hwnd captured: {0:X}", (UINT64) _hwnd);
    }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkCreateInstance, PFN_vkCreateInstance)
static VkResult hkvkCreateInstance(const VkInstanceCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator,
                                   VkInstance* pInstance)
{
    LOG_FUNC();

    VkInstanceCreateInfo localCreateInfo {};
    memcpy(&localCreateInfo, pCreateInfo, sizeof(VkInstanceCreateInfo));

    VulkanSpoofing::hkvkCreateInstance(&localCreateInfo, pAllocator, pInstance);

    std::vector<std::string> fgInstanceExtensions;
    if(DlssNr::NativeFg::Selected()&&o_vkGetInstanceProcAddr){
        const auto enumerate=reinterpret_cast<PFN_vkEnumerateInstanceExtensionProperties>(o_vkGetInstanceProcAddr(VK_NULL_HANDLE,"vkEnumerateInstanceExtensionProperties"));
        uint32_t count=0;
        if(enumerate&&enumerate(nullptr,&count,nullptr)==VK_SUCCESS&&count<=1024){
            std::vector<VkExtensionProperties> values(count);
            if(enumerate(nullptr,&count,values.data())==VK_SUCCESS)for(uint32_t i=0;i<count;++i)fgInstanceExtensions.emplace_back(values[i].extensionName);
        }
    }
    const DlssNr::NativeFg::MaintenanceInstancePreparation fgInstance(localCreateInfo,fgInstanceExtensions,DlssNr::NativeFg::Selected());
    localCreateInfo=fgInstance.Apply(localCreateInfo);

    VkResult result;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        result = o_vkCreateInstance(&localCreateInfo, pAllocator, pInstance);
    }

    if (result == VK_SUCCESS)
    {
        const auto enumerate=o_vkGetInstanceProcAddr?reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(
            o_vkGetInstanceProcAddr(*pInstance,"vkEnumeratePhysicalDevices")):nullptr;
        std::uint32_t count=0;
        if(enumerate && enumerate(*pInstance,&count,nullptr)==VK_SUCCESS && count && count<=64){
            std::vector<VkPhysicalDevice> physical(count);
            if(enumerate(*pInstance,&count,physical.data())==VK_SUCCESS){physical.resize(count);
                const auto api=localCreateInfo.pApplicationInfo && localCreateInfo.pApplicationInfo->apiVersion?
                    localCreateInfo.pApplicationInfo->apiVersion:VK_API_VERSION_1_0;
                DlssNr::GetVulkanPresentRegistry().InstanceObserved(*pInstance,api,std::move(physical));}}
        if(enumerate&&DlssNr::NativeFg::Selected()){
            uint32_t n=0;if(enumerate(*pInstance,&n,nullptr)==VK_SUCCESS&&n<=64){std::vector<VkPhysicalDevice> values(n);
                if(enumerate(*pInstance,&n,values.data())==VK_SUCCESS)for(uint32_t i=0;i<n;++i)
                    DlssNr::NativeFg::InstanceMaintenance(values[i],DlssNr::NativeFg::HasInstanceExtension(localCreateInfo,DlssNr::NativeFg::SurfaceMaintenanceExtension)&&
                        DlssNr::NativeFg::HasInstanceExtension(localCreateInfo,"VK_KHR_get_surface_capabilities2"));}
        }
        State::Instance().VulkanInstance = *pInstance;
        LOG_DEBUG("State::Instance().VulkanInstance captured: {0:X}", (UINT64) State::Instance().VulkanInstance);

#ifdef VULKAN_DEBUG_LAYER
        auto address = vkGetInstanceProcAddr(State::Instance().VulkanInstance, "vkCreateDebugUtilsMessengerEXT");
        auto vkCreateDebugUtilsMessengerEXT = (PFN_vkCreateDebugUtilsMessengerEXT) address;
        VkDebugUtilsMessengerEXT debugMessenger;
        vkCreateDebugUtilsMessengerEXT(State::Instance().VulkanInstance, &VulkanSpoofing::debugCreateInfo, nullptr,
                                       &debugMessenger);
#endif
    }

    // Disabled to prevent unnecessary object release
    // if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    //{
    //     MenuOverlayVk::DestroyVulkanObjects(false);
    // }

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkCreateDevice, PFN_vkCreateDevice)
extern "C" __declspec(dllexport) unsigned __cdecl NeuRotic_QueryPreparedVulkanV1(DlssNr::PreparedVulkan::DeviceInfo* info)
{
    if(!info || info->size!=sizeof(*info) || info->version!=1 || info->reserved || !info->device || !info->queue)return 0;
    auto out=*info;
    if(!DlssNr::GetVulkanPresentRegistry().PreparedDevice(out) &&
       !DlssNr::GetVulkanLoaderPresentRegistry().PreparedDevice(out))return 0;
    if(DlssNr::GetVulkanPresentRegistry().PhysicalInstanceApi(out.physical)<VK_API_VERSION_1_1)return 0;
    const auto loader=GetModuleHandleW(L"vulkan-1.dll");if(!loader)return 0;
    out.getDeviceProcAddr=reinterpret_cast<PFN_vkGetDeviceProcAddr>(GetProcAddress(loader,"vkGetDeviceProcAddr"));
    out.getProperties2=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(GetProcAddress(loader,"vkGetPhysicalDeviceProperties2"));
    out.getImageFormatProperties2=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties2>(GetProcAddress(loader,"vkGetPhysicalDeviceImageFormatProperties2"));
    out.getExternalSemaphoreProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceExternalSemaphoreProperties>(GetProcAddress(loader,"vkGetPhysicalDeviceExternalSemaphoreProperties"));
    out.getMemoryProperties=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(GetProcAddress(loader,"vkGetPhysicalDeviceMemoryProperties"));
    if(!out.getDeviceProcAddr || !out.getProperties2 || !out.getImageFormatProperties2 ||
       !out.getExternalSemaphoreProperties || !out.getMemoryProperties)return 0;
    *info=out;return 1;
}
static VkResult hkvkCreateDevice(VkPhysicalDevice physicalDevice, const VkDeviceCreateInfo* pCreateInfo,
                                 const VkAllocationCallbacks* pAllocator, VkDevice* pDevice)
{
    LOG_FUNC();

    VkDeviceCreateInfo localCreteInfo {};
    memcpy(&localCreteInfo, pCreateInfo, sizeof(VkDeviceCreateInfo));

    // Check support for AntiLag before spoof
    VkPhysicalDeviceFeatures2 features2 = {};
    features2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2;

    VkPhysicalDeviceAntiLagFeaturesAMD antiLagFeatures = {};
    antiLagFeatures.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ANTI_LAG_FEATURES_AMD;

    features2.pNext = &antiLagFeatures;

    if (o_vkGetPhysicalDeviceFeatures2)
    {
        o_vkGetPhysicalDeviceFeatures2(physicalDevice, &features2);
        State::Instance().vkAntiLagSupported = antiLagFeatures.antiLag != 0;
    }

    VulkanSpoofing::hkvkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);

    // Prepare immutable capabilities before the user chooses a route or enables NR.
    // No model creation, evaluation, working images, or caller-memory mutations occur here.
    const auto nrSettings = TryNrConfigSnapshot(*Config::Instance());
    const auto nrRuntime = nrSettings ? nrSettings->GetDlssNrRuntimeSnapshot() : NrConfigState::RuntimeSnapshot{};
    const uint32_t nrRoute = nrSettings ? nrSettings->DlssNrRoute.value_or_default() : 0;
    // A failed snapshot must not override an explicit opt-out.
    const bool preparationAllowed = nrSettings && nrSettings->DlssNrVulkanPrepare.value_or_default();
    std::vector<std::string> supportedNrExtensions;
    bool nrExtensionQuerySucceeded = false;
    VkPhysicalDeviceFeatures nrPhysicalFeatures {};
    bool nrFeatureQuerySucceeded = false;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        supportedNrExtensions = DlssNr::VkExt::SupportedDeviceExtensions(
            o_vkGetInstanceProcAddr, State::Instance().VulkanInstance, physicalDevice, &nrExtensionQuerySucceeded);
        const auto getFeatures = o_vkGetInstanceProcAddr != nullptr
            ? reinterpret_cast<PFN_vkGetPhysicalDeviceFeatures>(o_vkGetInstanceProcAddr(
                State::Instance().VulkanInstance, "vkGetPhysicalDeviceFeatures")) : nullptr;
        if (getFeatures != nullptr)
        {
            getFeatures(physicalDevice, &nrPhysicalFeatures);
            nrFeatureQuerySucceeded = true;
        }
    }
    const auto nrStorageWrite = DlssNr::PrepareVkNrStorageWrite(localCreteInfo,
        nrPhysicalFeatures.shaderStorageImageWriteWithoutFormat == VK_TRUE, preparationAllowed);
    localCreteInfo = nrStorageWrite.Apply(localCreteInfo);
    const auto nrPreparation = DlssNr::PrepareVkNrDevice(localCreteInfo, supportedNrExtensions,
        DlssNr::ShouldPrepareVulkanModelExtensions(nrRuntime.enabled, nrRoute, preparationAllowed), pCreateInfo);
    localCreteInfo = nrPreparation.Apply(localCreteInfo);
    // Automatic and Built-in discovery prepare only advertised sharing support.
    // Successful observation below, not environment intent, enables transport.
    wchar_t preparedOptIn[8]{};
    const bool prepareExternal=DlssNr::NativeGuides::ObserveBuiltIn()||
        (GetEnvironmentVariableW(L"NEUROTIC_PREPARED_VULKAN",preparedOptIn,8)==1 && preparedOptIn[0]==L'1');
    VkPhysicalDeviceTimelineSemaphoreFeatures preparedTimeline{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_TIMELINE_SEMAPHORE_FEATURES};
    VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT fgMaintenanceSupport{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};
    const bool fgMaintenanceRequested=DlssNr::NativeFg::Selected()&&DlssNr::NativeFg::SurfaceMaintenance(physicalDevice)&&
        std::find(supportedNrExtensions.begin(),supportedNrExtensions.end(),DlssNr::NativeFg::MaintenanceExtension)!=supportedNrExtensions.end();
    if(fgMaintenanceRequested)preparedTimeline.pNext=&fgMaintenanceSupport;
    VkPhysicalDeviceFeatures2 preparedFeatures{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
    preparedFeatures.pNext=&preparedTimeline;
    const auto preparedInstanceApi=DlssNr::GetVulkanPresentRegistry().PhysicalInstanceApi(physicalDevice);
    VkPhysicalDeviceProperties preparedProperties{};
    if(prepareExternal)vkGetPhysicalDeviceProperties(physicalDevice,&preparedProperties);
    const bool preparedCore11=preparedInstanceApi>=VK_API_VERSION_1_1 && preparedProperties.apiVersion>=VK_API_VERSION_1_1;
    if(prepareExternal && preparedCore11 && o_vkGetPhysicalDeviceFeatures2)o_vkGetPhysicalDeviceFeatures2(physicalDevice,&preparedFeatures);
    const DlssNr::PreparedVulkan::Preparation preparedExternal(localCreteInfo,supportedNrExtensions,
        preparedTimeline.timelineSemaphore==VK_TRUE,prepareExternal,
        preparedCore11);
    localCreteInfo=preparedExternal.Apply(localCreteInfo);
    const DlssNr::NativeFg::MaintenancePreparation fgMaintenance(localCreteInfo,supportedNrExtensions,
        fgMaintenanceSupport.swapchainMaintenance1==VK_TRUE,fgMaintenanceRequested&&preparedCore11);
    localCreteInfo=fgMaintenance.Apply(localCreteInfo);
    LOG_INFO("DLSS-NR Vulkan preparation: policy={} enabledIntent={} route={} extensionQuerySucceeded={} "
             "source=hook-create-input", preparationAllowed, nrRuntime.enabled, nrRoute, nrExtensionQuerySucceeded);
    for (uint32_t i = 0; i < std::size(DlssNr::VkExt::kDevice); ++i)
        LOG_INFO("DLSS-NR Vulkan prerequisite: {} advertised={} callerEnabled={} added={} unavailable={}",
            DlssNr::VkExt::kDevice[i], (nrPreparation.advertised & (1u << i)) != 0,
            (nrPreparation.callerEnabled & (1u << i)) != 0, (nrPreparation.added & (1u << i)) != 0,
            (nrPreparation.unavailable & (1u << i)) != 0);

    uint32_t fgFamilyCount=0;vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&fgFamilyCount,nullptr);
    std::vector<VkQueueFamilyProperties> fgFamilies(fgFamilyCount);
    if(fgFamilyCount)vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice,&fgFamilyCount,fgFamilies.data());
    const auto appQueueCreateInfo=localCreteInfo;
    const DlssNr::NativeFg::QueuePreparation fgQueues(localCreteInfo,fgFamilies,DlssNr::NativeFg::Selected()&&DlssNr::NativeGuides::Selected()&&
        !DlssNr::NativeFg::ObserveMaintenance(localCreteInfo,VK_SUCCESS));
    localCreteInfo=fgQueues.Apply(localCreteInfo);
    auto result = o_vkCreateDevice(physicalDevice, &localCreteInfo, pAllocator, pDevice);
    bool fgQueuesCreated=fgQueues.family!=UINT32_MAX&&result==VK_SUCCESS;
    LOG_INFO("Standalone FG startup: selected={} nativeGuides={} privateFamily={} firstPrivateQueue={} createResult={}",
        DlssNr::NativeFg::Selected(),DlssNr::NativeGuides::Selected(),fgQueues.family,fgQueues.first,(int)result);
    if(DlssNr::NativeFg::Selected()&&appQueueCreateInfo.pQueueCreateInfos)
        for(uint32_t i=0;i<appQueueCreateInfo.queueCreateInfoCount;++i){
            const auto& q=appQueueCreateInfo.pQueueCreateInfos[i];
            LOG_INFO("Standalone FG queue request: family={} appQueues={} available={} flags={} extensionChain={}",
                q.queueFamilyIndex,q.queueCount,q.queueFamilyIndex<fgFamilies.size()?fgFamilies[q.queueFamilyIndex].queueCount:0,
                q.flags,q.pNext!=nullptr);
        }
    if(result!=VK_SUCCESS&&fgQueues.family!=UINT32_MAX){localCreteInfo=appQueueCreateInfo;result=o_vkCreateDevice(physicalDevice,&localCreteInfo,pAllocator,pDevice);}
    if(result==VK_SUCCESS)DlssNr::NativeFg::DeviceMaintenance(*pDevice,DlssNr::NativeFg::ObserveMaintenance(localCreteInfo,result));
    if(DlssNr::NativeFg::Selected())LOG_INFO("Standalone FG presentation maintenance: requested={} advertisedFeature={} enabled={}",
        fgMaintenanceRequested,fgMaintenanceSupport.swapchainMaintenance1==VK_TRUE,DlssNr::NativeFg::ObserveMaintenance(localCreteInfo,result));
    const auto nrCapabilities = DlssNr::ObserveVkNrDeviceCreation(localCreteInfo, result);
    // This certificate covers inputs at this observed boundary after a successful
    // create. It makes no claim about unobserved downstream feature/queue additions.
    const bool modelExtensionsEnabled = nrCapabilities.ModelExtensionsEnabled();
    const bool presentPrepared = modelExtensionsEnabled && nrCapabilities.CompositionEnabled();
    LOG_INFO("DLSS-NR Vulkan prerequisite: shaderStorageImageWriteWithoutFormat queried={} supported={} "
             "added={} enabled={} unsafeFeatureChain={}", nrFeatureQuerySucceeded,
             nrPhysicalFeatures.shaderStorageImageWriteWithoutFormat == VK_TRUE, nrStorageWrite.amended,
             nrCapabilities.CompositionEnabled(), nrStorageWrite.unsafeChain);
    LOG_INFO("DLSS-NR Vulkan creation: result={} modelExtensionMask={} prepared={} "
             "bufferDeviceAddressObserved={} bufferDeviceAddressEnabled={} featureChainComplete={} "
             "source=successful-hook-create-input downstreamAugmentation=unobserved",
             (int) result, nrCapabilities.enabledModelExtensions, presentPrepared,
             nrCapabilities.bufferDeviceAddressObserved, nrCapabilities.bufferDeviceAddressEnabled,
             nrCapabilities.featureChainComplete);
    if (result == VK_SUCCESS && !State::Instance().vulkanSkipHooks)
    {
        if (o_vkGetDeviceProcAddr) {
            DlssNr::VkNrCompletionDispatch dispatch;
            dispatch.createFence = reinterpret_cast<PFN_vkCreateFence>(o_vkGetDeviceProcAddr(*pDevice,"vkCreateFence"));
            dispatch.destroyFence = reinterpret_cast<PFN_vkDestroyFence>(o_vkGetDeviceProcAddr(*pDevice,"vkDestroyFence"));
            dispatch.getFenceStatus = reinterpret_cast<PFN_vkGetFenceStatus>(o_vkGetDeviceProcAddr(*pDevice,"vkGetFenceStatus"));
            dispatch.queueSubmit = reinterpret_cast<PFN_vkQueueSubmit>(o_vkGetDeviceProcAddr(*pDevice,"vkQueueSubmit"));
            dispatch.waitForFences = reinterpret_cast<PFN_vkWaitForFences>(o_vkGetDeviceProcAddr(*pDevice,"vkWaitForFences"));
            DlssNr::RegisterVkNrCompletionDevice(*pDevice,dispatch);
            HookDeviceSubmissions(*pDevice);
        }

        auto& registry = DlssNr::GetVulkanPresentRegistry();
        std::vector<uint32_t> createdFamilies;
        if (localCreteInfo.pQueueCreateInfos != nullptr)
            for (uint32_t i = 0; i < localCreteInfo.queueCreateInfoCount; ++i)
                createdFamilies.push_back(localCreteInfo.pQueueCreateInfos[i].queueFamilyIndex);
        Vulkan_wDx12::cmdBufferStateTracker.EnableNativeObservations();
        registry.DeviceCreated(*pDevice, physicalDevice, modelExtensionsEnabled, presentPrepared,
                               createdFamilies, nrExtensionQuerySucceeded && nrPreparation.unavailable != 0,
                               nrCapabilities.CompositionEnabled(), nrFeatureQuerySucceeded &&
                               nrPhysicalFeatures.shaderStorageImageWriteWithoutFormat != VK_TRUE,
                               nrStorageWrite.unsafeChain,DlssNr::VkNrCompletionDeviceGeneration(*pDevice));
        const auto preparedEnabled=DlssNr::PreparedVulkan::Observe(localCreteInfo,result);
        registry.PreparedTransportObserved(*pDevice,preparedEnabled);
        DlssNr::NativeVulkanGuides::DeviceCreated(*pDevice,physicalDevice,o_vkGetDeviceProcAddr,
            registry.PhysicalInstanceApi(physicalDevice)>=VK_API_VERSION_1_1&&o_vkGetInstanceProcAddr?
            reinterpret_cast<PFN_vkGetPhysicalDeviceProperties2>(o_vkGetInstanceProcAddr(
                _instance!=VK_NULL_HANDLE?_instance:State::Instance().VulkanInstance,"vkGetPhysicalDeviceProperties2")):nullptr);

        uint32_t familyCount = 0;
        vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, nullptr);
        std::vector<VkQueueFamilyProperties> familyProperties(familyCount);
        if (familyCount != 0)
            vkGetPhysicalDeviceQueueFamilyProperties(physicalDevice, &familyCount, familyProperties.data());
        std::vector<VkQueueFlags> loaderFamilies;
        for(const auto& family:familyProperties)loaderFamilies.push_back(family.queueFlags);
        auto loaderImages=o_vkGetDeviceProcAddr?reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
            o_vkGetDeviceProcAddr(*pDevice,"vkGetSwapchainImagesKHR")):nullptr;
        DlssNr::VulkanLoaderWsiDevices().Created(*pDevice,physicalDevice,VK_NULL_HANDLE,
            std::move(loaderFamilies),loaderImages);
        DlssNr::GetVulkanLoaderPresentRegistry().DeviceCreated(*pDevice,physicalDevice,false,false,createdFamilies,
            false,false,false,false,DlssNr::VkNrCompletionDeviceGeneration(*pDevice));
        DlssNr::GetVulkanLoaderPresentRegistry().PreparedTransportObserved(*pDevice,preparedEnabled);
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,1,
            DlssNr::VulkanLoaderWsiDevices().Device(*pDevice).generation,(uintptr_t)*pDevice,
            (uintptr_t)physicalDevice,0,familyCount,result,true);
        auto getQueue = o_vkGetDeviceProcAddr != nullptr
            ? reinterpret_cast<PFN_vkGetDeviceQueue>(o_vkGetDeviceProcAddr(*pDevice, "vkGetDeviceQueue"))
            : nullptr;
        if (getQueue != nullptr && localCreteInfo.pQueueCreateInfos != nullptr)
        {
            for (uint32_t i = 0; i < localCreteInfo.queueCreateInfoCount; ++i)
            {
                const auto& create = appQueueCreateInfo.pQueueCreateInfos[i];
                if (create.queueFamilyIndex >= familyCount ||
                    (create.flags & VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT) != 0) continue;
                for (uint32_t index = 0; index < create.queueCount; ++index)
                {
                    VkQueue queue = VK_NULL_HANDLE;
                    getQueue(*pDevice, create.queueFamilyIndex, index, &queue);
                    ObserveLoaderQueue(*pDevice,queue,create.queueFamilyIndex,create.flags);
                    DlssNr::RegisterVkNrCompletionQueue(*pDevice,queue,create.queueFamilyIndex);
                    registry.QueueObserved(*pDevice, queue, create.queueFamilyIndex,
                                           familyProperties[create.queueFamilyIndex].queueFlags);
                }
            }
        }

        if(fgQueuesCreated)DlssNr::NativeFg::DeviceCreated(*pDevice,fgQueues.family,fgQueues.first,getQueue);
        if (DlssNr::ShouldHookVulkanPresent(Config::Instance()->OverlayMenu.value_or_default(),
                                              nrRuntime.enabled, nrRoute))
        {
            _PD = physicalDevice;
            _device = *pDevice;
            HookDevice(_device);
        }

        if (Config::Instance()->OverlayMenu.value_or_default())
        {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};

        VkPhysicalDeviceIDProperties idProps {};
        idProps.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ID_PROPERTIES;

        VkPhysicalDeviceProperties2 props2 {};
        props2.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2;
        props2.pNext = &idProps;

        vkGetPhysicalDeviceProperties2(physicalDevice, &props2);

        if (idProps.deviceLUIDValid == VK_TRUE)
        {
            auto primaryGpu = IdentifyGpu::getPrimaryGpu();
            auto luid = (PLUID) idProps.deviceLUID;
            if (!IsEqualLUID(*luid, primaryGpu.luid))
                LOG_WARN("VkDevice created with non-primary GPU");
        }
        }
    }

    if (State::Instance().vkAntiLagSupported)
    {
        if (result == VK_SUCCESS && o_vkGetDeviceProcAddr)
        {
            VulkanHooks::o_vkAntiLagUpdateAMD =
                (PFN_vkAntiLagUpdateAMD) o_vkGetDeviceProcAddr(*pDevice, "vkAntiLagUpdateAMD");
        }
        else
        {
            State::Instance().vkAntiLagSupported = false;
            LOG_WARN("Vulkan AntiLag can't be enabled");
        }
    }

#ifdef USE_QUEUE_SUBMIT_2_KHR
    if (result == VK_SUCCESS)
        hkvkGetDeviceProcAddr(*pDevice, "vkQueueSubmit2KHR");
#endif

    LOG_FUNC_RESULT(result);

    return result;
}

VALIDATE_HOOK(hkvkAcquireNextImageKHR, PFN_vkAcquireNextImageKHR)
static VkResult VKAPI_CALL hkvkAcquireNextImageKHR(VkDevice device, VkSwapchainKHR swapchain, uint64_t timeout,
                                        VkSemaphore semaphore, VkFence fence, uint32_t* pImageIndex)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkAcquireNextImageKHR>::Get(o_AcquireNextImageKHR);
    if(DlssNr::NativeFg::Internal())return target(device,swapchain,timeout,semaphore,fence,pImageIndex);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Acquire,(uintptr_t)device,pImageIndex))
        return target(device,swapchain,timeout,semaphore,fence,pImageIndex);
    const auto result=DlssNr::VkFlight::Call(DlssNr::VkFlight::CallSite::Acquire,(uintptr_t)swapchain,[&]{
        DlssNr::VkWsiForwardScope scope(DlssNr::VkWsiOperation::Acquire,(uintptr_t)device,pImageIndex);
        DlssNr::VkWsiTargetScope<PFN_vkAcquireNextImageKHR> clear(nullptr);
        if(const auto result=DlssNr::NativeFg::Acquire(device,swapchain,timeout,semaphore,fence,pImageIndex))return *result;
        return target(device,swapchain,timeout,semaphore,fence,pImageIndex);});
    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && pImageIndex != nullptr)
    {
        DlssNr::GetVulkanLoaderPresentRegistry().ImageAcquired(device,swapchain,*pImageIndex,semaphore,fence);
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,4,
            DlssNr::VulkanLoaderWsiDevices().Device(device).generation,(uintptr_t)swapchain,
            (uintptr_t)semaphore,(uintptr_t)device,*pImageIndex,result);
    }
    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && pImageIndex != nullptr)
        DlssNr::GetVulkanPresentRegistry().ImageAcquired(device, swapchain, *pImageIndex,semaphore,fence);
    return result;
}

VALIDATE_HOOK(hkvkAcquireNextImage2KHR, PFN_vkAcquireNextImage2KHR)
static VkResult VKAPI_CALL hkvkAcquireNextImage2KHR(VkDevice device, const VkAcquireNextImageInfoKHR* pAcquireInfo,
                                         uint32_t* pImageIndex)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkAcquireNextImage2KHR>::Get(o_AcquireNextImage2KHR);
    if(DlssNr::NativeFg::Internal())return target(device,pAcquireInfo,pImageIndex);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Acquire2,(uintptr_t)device,pAcquireInfo))
        return target(device,pAcquireInfo,pImageIndex);
    const auto result=DlssNr::VkFlight::Call(DlssNr::VkFlight::CallSite::Acquire,(uintptr_t)device,[&]{
        DlssNr::VkWsiForwardScope scope(DlssNr::VkWsiOperation::Acquire2,(uintptr_t)device,pAcquireInfo);
        DlssNr::VkWsiTargetScope<PFN_vkAcquireNextImage2KHR> clear(nullptr);
        if(pAcquireInfo&&DlssNr::NativeFg::Owns(pAcquireInfo->swapchain)){
            if(pAcquireInfo->pNext||pAcquireInfo->deviceMask!=1)return VK_ERROR_FEATURE_NOT_PRESENT;
            return *DlssNr::NativeFg::Acquire(device,pAcquireInfo->swapchain,pAcquireInfo->timeout,pAcquireInfo->semaphore,pAcquireInfo->fence,pImageIndex);}
        return target(device,pAcquireInfo,pImageIndex);});
    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) && pAcquireInfo && pImageIndex)
    {
        DlssNr::GetVulkanLoaderPresentRegistry().ImageAcquired(device,pAcquireInfo->swapchain,*pImageIndex,pAcquireInfo->semaphore,pAcquireInfo->fence);
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,4,
            DlssNr::VulkanLoaderWsiDevices().Device(device).generation,(uintptr_t)pAcquireInfo->swapchain,
            (uintptr_t)pAcquireInfo->semaphore,(uintptr_t)device,*pImageIndex,result);
    }
    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) &&
        pAcquireInfo != nullptr && pImageIndex != nullptr)
        DlssNr::GetVulkanPresentRegistry().ImageAcquired(device, pAcquireInfo->swapchain, *pImageIndex,pAcquireInfo->semaphore,pAcquireInfo->fence);
    return result;
}

static void ReportVkPresentResult(VkQueue queue, const VkPresentInfoKHR& info,
                                  const DlssNr::VkObservedPresent* observed,
                                  const DlssNr::VkPresentExecution* execution,
                                  bool originalCalled, VkResult result, const char* reason)
{
    auto& status = DlssNr::GetVulkanPresentStatus();
    status.Observe(observed, execution, originalCalled, result, reason);
    const uint64_t generation = observed ? observed->request.swapchainGeneration : 0;
    const uint32_t decision = execution ? static_cast<uint32_t>(execution->admission.reason) |
        (static_cast<uint32_t>(execution->record.status) << 8) |
        (static_cast<uint32_t>(execution->handoff.state) << 16) : UINT32_MAX;
    static thread_local DlssNr::VkPresentLogGate logGate;
    const char* message = reason != nullptr ? reason : "";
    if (!logGate.ShouldLog(generation, reinterpret_cast<uintptr_t>(queue), decision,
                           originalCalled, result, message)) return;

    const auto totals = status.Snapshot();
    LOG_INFO("DLSS-NR Vulkan Present: runtime decision generation={} queue={} admission={} "
             "allowed={} record={} handoff={} originalCalled={} originalResult={} reason=[{}] "
             "totals attempts={} recorded={} submitted={} completed={} trackedModelPrivateBytes={}",
             generation, reinterpret_cast<uintptr_t>(queue),
             execution ? static_cast<uint32_t>(execution->admission.reason) : UINT32_MAX,
             execution && execution->admission.allowed,
             execution ? static_cast<uint32_t>(execution->record.status) : UINT32_MAX,
             execution ? static_cast<uint32_t>(execution->handoff.state) : UINT32_MAX,
             originalCalled, static_cast<int32_t>(result), message,
             totals.attempts, totals.recorded, totals.submitted, totals.completed,
             DlssNr::PresentGenerationPrivateBytesVk());
    if (observed)
    {
        const auto& r = observed->request;
        const auto& c = observed->capabilities;
        LOG_INFO("DLSS-NR Vulkan Present: runtime facts route={} policy={} fg={} format={} "
                 "extent={}x{} usage=0x{:X} surfaceUsage=0x{:X} knownChain={} acquired={} "
                 "imageIndex={} imageCount={} queueObserved={} queueFamily={} queueFlags=0x{:X} "
                 "presentFamilyMatches={} presentQueueAccessQualified={} prepared={}",
                 r.route, static_cast<uint32_t>(r.policy), r.fgKnownActive,
                 static_cast<uint32_t>(r.format), r.extent.width, r.extent.height,
                 r.imageUsage, c.surfaceUsage, c.swapchainPNextKnown, r.acquiredObserved,
                 r.imageIndex, c.imageCount, c.queueObserved, observed->queueFamily, c.queueFlags,
                 c.queueFamilyMatches, c.presentQueueAccessQualified, c.routePreparedAtDeviceCreation);
    }
    const auto chain = DlssNr::DescribeVkSwapchainChain(info.pNext);
    std::string types;
    for (size_t i = 0; i < chain.count; ++i)
        types += (i == 0 ? "" : ",") + std::to_string(static_cast<uint32_t>(chain.types[i]));
    LOG_INFO("DLSS-NR Vulkan Present: present call swapchains={} waits={} pNextTypes=[{}] "
             "chainCycle={} chainTruncated={}", info.swapchainCount, info.waitSemaphoreCount,
             types.empty() ? "none" : types, chain.cycle, chain.truncated);
}

// Raw loader boundary, below public provider wrappers. Lower Vulkan layers are
// still unknown; this establishes loader-visible WSI, never absolute terminality.
// Report outside input/tracker/overlay locks. Producers only update fixed atomics.
// Rows are cumulative sampled elapsed times; overlapping categories are not additive.
static void ReportVulkanHostCosts()
{
    using namespace Neurotic::HostCost;
    static ReportGate gate;
    static std::atomic_flag reporting = ATOMIC_FLAG_INIT;
    if (reporting.test_and_set(std::memory_order_acquire)) return;
    struct Release { std::atomic_flag& flag; ~Release(){flag.clear(std::memory_order_release);} } release{reporting};
    if (gate.Exhausted()) { Stop(); return; }
    const auto now = Now();
    if (!gate.Take(now)) return;
    Scope reportingCost(Kind::DiagnosticReport);
    static std::array<uint64_t, static_cast<size_t>(Kind::Count)> previous {};
    try {
        const auto settings = TryNrConfigSnapshot(*Config::Instance());
        const auto runtime = settings ? settings->GetDlssNrRuntimeSnapshot() : NrConfigState::RuntimeSnapshot{};
        spdlog::info("NR_HOST_COST snapshotNs={} enabled={} route={} cumulative=true sampled=true "
            "overlappingCategories=true liveFields=best-effort maxReports=600 "
            "sampleStride=64:tracker-binding,tracker-barrier,tracker-other,recording-work,observation-scope;1:others "
            "trackerBeforeLockIncludesLookup=true",
            now, runtime.enabled, settings ? settings->DlssNrRoute.value_or_default() : 0);
        for (size_t i=0; i<previous.size(); ++i) {
            const auto v=Read(static_cast<Kind>(i));
            if(v.samples==previous[i]) continue;
            previous[i]=v.samples;
            spdlog::info("NR_HOST_COST category={} samples={} sampledTotalMs={:.3f} beforeLockMs={:.3f} "
                "maxMs={:.3f} maxBeforeLockMs={:.3f}", Names[i], v.samples, v.totalNs/1e6,
                v.beforeLockNs/1e6, v.maxNs/1e6, v.maxBeforeLockNs/1e6);
        }
    } catch (...) { /* Diagnostic logging must not change Present behavior. */ }
}

static VkResult LoaderQueuePresent(VkQueue queue,const VkPresentInfoKHR* info,PFN_vkQueuePresentKHR target,
                                   bool* originalCalled=nullptr)
{
    target=DlssNr::NativeFg::ResolvePresent(info,target);
    if(!info)return DlssNr::NativeFg::CallPresent(target,queue,info);
    auto local=*info;auto& registry=DlssNr::GetVulkanLoaderPresentRegistry();
    const auto device=DlssNr::VulkanLoaderWsiDevices().ForQueue(queue);
    DlssNr::VulkanPresentConsumers().RefreshPhysical(device.device);
    const auto& inspectorHost=State::Instance();
    const auto inspectorManagedFg=inspectorHost.currentFG;
    const bool inspectorFg=inspectorHost.dlssgLastSetMode.load()!=sl::DLSSGMode::eOff ||
        (inspectorManagedFg&&inspectorManagedFg->IsActive()&&!inspectorManagedFg->IsPaused()) ||
        (DlssNr::VulkanNrStreamlineAdapter().Generation()!=0 &&
         DlssNr::VulkanNrStreamlineAdapter().Activity()==DlssNr::VkNrFgActivity::On);
    auto observed=registry.SnapshotForPresent(queue,*info,false,0,DlssNr::PresentInput::Policy::ImageOnly,inspectorFg);
    VulkanPresentedExtent presented {};
    bool extentKnown = device.generation && local.swapchainCount == 1 && local.pSwapchains != nullptr && observed &&
        observed->capabilities.queueObserved && observed->capabilities.imagesObserved && observed->image;
    {
        std::lock_guard<std::mutex> lock(g_swapchainExtentMutex);
        for (uint32_t i = 0; extentKnown && i < local.swapchainCount; ++i)
        {
            const auto it = g_swapchainExtents.find(reinterpret_cast<uintptr_t>(local.pSwapchains[i]));
            if (it == g_swapchainExtents.end())
            {
                extentKnown = false;
                break;
            }
            if (i == 0)
                presented = it->second;
            else if (presented.width != it->second.width || presented.height != it->second.height)
                extentKnown = false;
        }
    }
    const uint64_t packedExtent = extentKnown ? (static_cast<uint64_t>(presented.width) << 32) | presented.height : 0;
    g_presentedExtent.store(packedExtent, std::memory_order_release);

    if(!IdentifyGpu::getPrimaryGpu().usesDxvk)State::Instance().swapchainApi=Vulkan;
    if(auto currentFeature=State::Instance().currentFeature)currentFeature->TickFrozenCheck();
    MenuOverlayVk::PresentLock lock;
    auto overlay=MenuOverlayVk::QueuePresentStatus::Bypassed;
    if(device.generation&&observed&&Config::Instance()->OverlayMenu.value_or_default()){
        Neurotic::HostCost::Scope nrOverlayCost(Neurotic::HostCost::Kind::PresentOverlay);
        lock=MenuOverlayVk::LockPresent();
        nrOverlayCost.Acquired();
        overlay=MenuOverlayVk::QueuePresent(queue,&local,lock,&*observed,
            {device.generation,&registry,device.getImages});
        if(overlay==MenuOverlayVk::QueuePresentStatus::SubmitUncertain)return VK_ERROR_DEVICE_LOST;
    } else {
        if(Config::Instance()->OverlayMenu.value_or_default()) {
            static std::atomic<uint64_t> bypasses{0};const auto n=++bypasses;
            if(n<=4||n%600==0)LOG_INFO("Vulkan loader overlay unavailable: deviceObserved={} snapshot={} queue={} swapchains={} count={}",
                device.generation!=0,observed.has_value(),reinterpret_cast<uintptr_t>(queue),local.swapchainCount,n);
        }
        MenuOverlayVk::Unavailable();
    }
    struct OverlayComposition { const MenuOverlayVk::PresentLock* lock; VkSwapchainKHR swap; } composition {
        &lock, local.swapchainCount == 1 && local.pSwapchains ? local.pSwapchains[0] : VK_NULL_HANDLE};
    const bool composedOverlay = overlay == MenuOverlayVk::QueuePresentStatus::Accepted && composition.swap &&
        DlssNr::NativeFg::Composition(composition.swap,
            [](void* context, VkCommandBuffer command, VkImage image, unsigned width, unsigned height, uint64_t) -> VkResult {
                const auto& pinned = *static_cast<OverlayComposition*>(context);
                return MenuOverlayVk::CompositeGenerated(*pinned.lock, pinned.swap, command, image, width, height);
            }, &composition);
    auto result=DlssNr::VkFlight::Call(DlssNr::VkFlight::CallSite::Present,(uintptr_t)queue,[&]{
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::Present,(uintptr_t)queue,&local);
        DlssNr::VkWsiTargetScope<PFN_vkQueuePresentKHR> clear(nullptr);
        if(originalCalled)*originalCalled=true;
        Neurotic::HostCost::Scope nrDriverCost(Neurotic::HostCost::Kind::PresentOriginal);
        return DlssNr::NativeFg::CallPresent(target,queue,&local);});
    if (composedOverlay) MenuOverlayVk::GeneratedPresentationFinished(lock, result);
    if(observed){
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,5,device.generation,
            (uintptr_t)observed->swapchain,(uintptr_t)observed->image,(uintptr_t)queue,observed->request.imageIndex,result);
        DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,6,observed->request.acquireGeneration,
            (uintptr_t)observed->swapchain,observed->request.swapchainGeneration,(uintptr_t)queue,1,result);
    }
    if((result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)&&local.pSwapchains&&local.pImageIndices)
        for(uint32_t i=0;i<local.swapchainCount;++i){
            const auto item=local.pResults?local.pResults[i]:result;
            if(item==VK_SUCCESS||item==VK_SUBOPTIMAL_KHR)registry.PresentSucceeded(local.pSwapchains[i],local.pImageIndices[i],queue);
        }
    if(DlssNr::VulkanPresentConsumers().Interested()&&observed&&local.swapchainCount==1&&local.pWaitSemaphores&&local.waitSemaphoreCount){
        const auto selected=DlssNr::VkWsiSelectedPresentResult(true,result,local);
        const auto serial=registry.LastPresentSerial(observed->swapchain,observed->request.imageIndex);
        DlssNr::VulkanPresentConsumers().PhysicalPresent(observed->device,
            DlssNr::VkNrCompletionDeviceGeneration(observed->device),{local.pWaitSemaphores,local.waitSemaphoreCount},selected,
            DlssNr::MakeVkPhysicalPresentWait(*observed,serial,registry));
    }
    if(overlay==MenuOverlayVk::QueuePresentStatus::Accepted){
        const auto selected=DlssNr::VkWsiSelectedPresentResult(true,result,local);
        MenuOverlayVk::Presented(lock,selected==VK_SUCCESS||selected==VK_SUBOPTIMAL_KHR,
            {device.generation,&registry,device.getImages});
    }
    if (composition.swap && DlssNr::NativeFg::Owns(composition.swap)) {
        static std::atomic<int64_t> lastReport {0};
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch()).count();
        auto previous = lastReport.load();
        if ((now - previous >= 2000 || (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR)) &&
            lastReport.compare_exchange_strong(previous, now))
            LOG_INFO("Standalone FG progress: present={} {}", static_cast<int>(result), DlssNr::NativeFg::Status());
    }
    return result;
}

VALIDATE_HOOK(hkvkQueuePresentKHR, PFN_vkQueuePresentKHR)
static VkResult VKAPI_CALL hkvkQueuePresentKHR(VkQueue queue, const VkPresentInfoKHR* pPresentInfo)
{
    using NrClock=std::chrono::steady_clock;
    const auto nrHookStart=NrClock::now();
    auto target=DlssNr::VkWsiTargetScope<PFN_vkQueuePresentKHR>::Get(o_QueuePresentKHR);
    if(DlssNr::NativeFg::Internal())return DlssNr::NativeFg::QueueCall(target,queue,pPresentInfo);
    DlssNr::FinalFallback::PresentScope nrOutputScope;
    DlssNr::RenderingOutput::Signal(reinterpret_cast<uintptr_t>(MenuCommon::Handle()),MenuCommon::IsVisible(),true);
    const bool standalone=pPresentInfo&&pPresentInfo->swapchainCount==1&&pPresentInfo->pSwapchains&&DlssNr::NativeFg::Owns(pPresentInfo->pSwapchains[0]);
    struct StandaloneFrame{VkSwapchainKHR swap{};bool finished=false;
        void Finish(){if(swap&&!finished){DlssNr::NativeFg::Finish(swap);finished=true;}}
        ~StandaloneFrame(){Finish();}} standaloneFrame;
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Present,(uintptr_t)queue,pPresentInfo))
        return target(queue,pPresentInfo);
    if(standalone){
        const auto owner=DlssNr::VulkanLoaderWsiDevices().ForQueue(queue);
        DlssNr::PreparedVulkan::DeviceInfo observed;observed.device=owner.device;observed.queue=queue;
        if(!owner.generation||!DlssNr::GetVulkanPresentRegistry().PreparedDevice(observed)||
           (observed.queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))!=(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT)||
           !DlssNr::NativeFg::BindPresentQueue(owner.device,pPresentInfo->pSwapchains[0],queue,observed.family))return VK_ERROR_DEVICE_LOST;
        if(!DlssNr::NativeFg::Begin(pPresentInfo->pSwapchains[0]))return VK_ERROR_DEVICE_LOST;
        standaloneFrame.swap=pPresentInfo->pSwapchains[0];
    }
    Neurotic::HostCost::Activate();
    Neurotic::HostCost::Scope nrHostCost(Neurotic::HostCost::Kind::PresentHook);
    // Same-thread, same-queue entry spacing includes game work between Presents.
    // It is neither GPU time nor proof of a displayed frame.
    thread_local VkQueue nrPreviousQueue=VK_NULL_HANDLE;
    thread_local uint64_t nrPreviousPresentNs=0;
    const auto nrPresentNs=Neurotic::HostCost::Now();
    if(Neurotic::HostCost::Enabled() && nrPreviousQueue==queue && nrPreviousPresentNs)
        Neurotic::HostCost::Record(Neurotic::HostCost::Kind::PresentInterval,nrPresentNs-nrPreviousPresentNs);
    nrPreviousQueue=queue; nrPreviousPresentNs=nrPresentNs;
    ReportVulkanHostCosts();
    if(!DlssNr::VulkanLoaderWsiDevices().ForQueue(queue).generation){
        standaloneFrame.Finish();
        bool originalCalled=false;const auto result=LoaderQueuePresent(queue,pPresentInfo,target,&originalCalled);
        if(pPresentInfo)ReportVkPresentResult(queue,*pPresentInfo,nullptr,nullptr,originalCalled,result,
            "Loader WSI device/queue identity unavailable: Present NR and overlay bypassed");
        return result;
    }
    const auto& fgHost=State::Instance();
    const auto& fgAdapter=DlssNr::VulkanNrStreamlineAdapter();
    if(DlssNr::VkWsiNeedsOverlayOnly(fgAdapter.Generation()!=0,fgAdapter.Activity()==DlssNr::VkNrFgActivity::Off,
        fgHost.dlssgLastSetMode.load()!=sl::DLSSGMode::eOff,
        fgHost.activeFgInput!=FGInput::NoFG||fgHost.activeFgOutput!=FGOutput::NoFG||
        DlssNr::VkNrNgxFgFeatureCount()||DlssNr::VulkanNrFfxStatus().providerId)){
        if(const auto settings=TryNrConfigSnapshot(*Config::Instance());
           settings&&!settings->GetDlssNrRuntimeSnapshot().enabled&&!DlssNr::WantsVulkanNrCapture()){
            // Provider-owned Present has no semantic acquisition snapshot here.
            // Release completed private work using only its real device/queue;
            // unresolved WSI waits remain owned until an exact proof is available.
            const auto device=DlssNr::VulkanLoaderWsiDevices().ForQueue(queue);
            DlssNr::RetireInactiveVulkanPresentGuides(device.device);
            DlssNr::VkObservedPresent maintenance{};maintenance.device=device.device;maintenance.queue=queue;
            DlssNr::PollVkNrCompletions();
            DlssNr::GetVulkanPresentExecutor().Maintain(maintenance,true);
            DlssNr::RetireInactiveVk(device.device,false,settings->DlssNrRoute.value_or_default());
        }
        DlssNr::NativeVulkanGuides::SetCaptureActive(false);
        if(DlssNr::NativeVulkanGuides::Enabled())DlssNr::NativeVulkanGuides::Unavailable("Native capture bypassed: frame-generation provider active or state unknown");
        standaloneFrame.Finish();
        bool originalCalled=false;
        const auto result=LoaderQueuePresent(queue,pPresentInfo,target,&originalCalled);
        if(pPresentInfo&&((result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR)||
           DlssNr::GetVulkanPresentStatus().PhysicalReports(fgAdapter.Generation())))
            ReportVkPresentResult(queue,*pPresentInfo,nullptr,nullptr,originalCalled,result,
            "FG active or provider state unknown: physical Present NR bypassed; loader overlay only");
        return result;
    }

    LOG_FUNC();
    if (pPresentInfo == nullptr)
        return VK_ERROR_INITIALIZATION_FAILED;
    auto nrSettings = TryNrConfigSnapshot(*Config::Instance());
    if(nrSettings&&!nrSettings->GetDlssNrRuntimeSnapshot().enabled&&DlssNr::WantsVulkanNrCapture())*nrSettings=nrSettings->ForPrivateDiagnostic();
    const auto nrRuntime = nrSettings ? nrSettings->GetDlssNrRuntimeSnapshot() : NrConfigState::RuntimeSnapshot{};
    const bool capturedVulkanRenderer=DlssNr::NativeGuides::ObserveBuiltIn()&&
        (DlssNr::NativeGuides::SelectedSource()==0||DlssNr::NativeGuides::SelectedSource()==2)&&
        DlssNr::NativeGuides::VulkanRendererSelected()&&!DlssNr::NativeFg::Selected();
    const bool nrPresentEnabled = nrRuntime.enabled && (!DlssNr::PreparedGuides::OwnsPresentOutput()||capturedVulkanRenderer);
    const uint32_t nrRoute = nrSettings ? nrSettings->DlssNrRoute.value_or_default() : 0;
    if(nrSettings&&!nrRuntime.enabled)
        DlssNr::RetireInactiveVulkanPresentGuides(DlssNr::VulkanLoaderWsiDevices().ForQueue(queue).device);
    const auto& host = State::Instance();
    const bool fgActive = host.dlssgLastSetMode.load() != sl::DLSSGMode::eOff ||
        host.activeFgInput != FGInput::NoFG || host.activeFgOutput != FGOutput::NoFG;
    // Consume scalar observations at entry, even while NR is disabled. A new SR
    // observation arriving during a slow Present belongs to the next interval.
    auto observed = DlssNr::GetVulkanPresentRegistry().SnapshotForPresent(queue, *pPresentInfo,
        nrPresentEnabled, nrRoute, nrSettings?DlssNr::PresentInput::Selected(*nrSettings):DlssNr::PresentInput::Policy::ImageOnly, fgActive);
    // A successful public marker supplies the frame; this exact call supplies
    // the acquired final image. Optional Backbuffer tags are not required.
    const auto publicFrame=DlssNr::VulkanPublicPresentFrames().Take(
        DlssNr::VulkanNrStreamlineAdapter().Generation(),GetCurrentThreadId());
    const auto association=publicFrame&&observed?DlssNr::MakeVkApplicationPresentTag(*publicFrame,*observed):std::nullopt;
    // Freeze selected producer/lifetime before timing or model work can overlap
    // the application's next recording on another thread.
    const auto guideSelection=DlssNr::BeginVulkanGuidePresent(queue,association?&*association:nullptr);
    // Native priority means usable private guides for this exact acquisition,
    // rather than DLSS module presence, metadata or last frame's readiness.
    const auto nativeGuides=observed?DlssNr::SelectVulkanPresentGuides(
        DlssNr::MakeVulkanGuidePresentContract(*observed,guideSelection.get()),guideSelection.get(),observed->image):std::nullopt;
    DlssNr::Connections::SetNativeUsable(nativeGuides.has_value());
    // get upscaler time
    UpscalerTimeVk::ReadUpscalingTime(_device);

    VkPresentInfoKHR localPresentInfo {};
    memcpy(&localPresentInfo, pPresentInfo, sizeof(VkPresentInfoKHR));

    if(observed) {
        auto& nativeFg=DlssNr::VulkanNrStreamlineAdapter();
        if(nativeFg.Generation()) {
            const auto activity=nativeFg.Activity();
            observed->request.fgStateKnown=activity!=DlssNr::VkNrFgActivity::Unknown;
            observed->request.fgKnownActive=activity==DlssNr::VkNrFgActivity::On||
                host.activeFgInput!=FGInput::NoFG||host.activeFgOutput!=FGOutput::NoFG;
        }
        if(DlssNr::VkNrNgxFgFeatureCount()||DlssNr::VulkanNrFfxStatus().providerId)
            observed->request.fgStateKnown=false;
    }
    DlssNr::NativeVulkanGuides::SetCaptureActive(observed&&nrRuntime.enabled&&(nrRoute==1||nrRoute==2)&&!DlssNr::PreparedGuides::ExternalRoute());
    if(DlssNr::NativeVulkanGuides::Enabled()) {
        if(!nrRuntime.enabled)DlssNr::NativeVulkanGuides::Unavailable("Native capture selected; enable NeuRotic NR");
        else if(nrRoute!=1&&nrRoute!=2)DlssNr::NativeVulkanGuides::Unavailable("Native capture requires the Present route");
        else if(!observed)DlssNr::NativeVulkanGuides::Unavailable("Native capture waiting for observed Vulkan swapchain and acquired image");
    }
    if(DlssNr::NativeVulkanGuides::Enabled()&&DlssNr::PreparedGuides::ExternalRoute())
        DlssNr::NativeVulkanGuides::Unavailable("Restart using normal game launch; external prepared route selected");
    DlssNr::VkPresentExecution execution {};
    bool captureHandled=false;
    DlssNr::NativeVulkanGuides::CapturedConsumer capturedConsumer;
    if(capturedVulkanRenderer&&observed&&nrSettings&&!standalone){
        capturedConsumer=[&](std::shared_ptr<const DlssNr::VkCapturedGuideInput> guides,VkPresentInfoKHR& present,std::string& reason){
            const auto instance=_instance!=VK_NULL_HANDLE?_instance:State::Instance().VulkanInstance;
            execution=DlssNr::GetVulkanPresentExecutor().BeforePresent(*observed,instance,present,
                std::make_shared<NrConfigSnapshot<Config>>(*nrSettings),{},{},{},std::move(guides));
            reason=execution.reason;
            if(execution.handoff.state==DlssNr::VkPresentHandoffState::Uncertain)return DlssNr::NativeGuides::Outcome::Unsafe;
            if(execution.handoff.state!=DlssNr::VkPresentHandoffState::Accepted)return DlssNr::NativeGuides::Outcome::Unavailable;
            DlssNr::ApplyVkPresentHandoff(present,execution.handoff);
            if(!DlssNr::GetVulkanPresentExecutor().CompleteCapturedGuides(execution.ticket)){
                execution.handoff.state=DlssNr::VkPresentHandoffState::Uncertain;
                execution.handoff.result=VK_ERROR_INITIALIZATION_FAILED;
                reason="Vulkan NR guide consumer completion uncertain";return DlssNr::NativeGuides::Outcome::Unsafe;
            }
            execution.capturedCompletionObserved=true;
            return execution.record.preparationOnly?DlssNr::NativeGuides::Outcome::Preparing:DlssNr::NativeGuides::Outcome::Delivered;
        };
    }
    if(observed&&DlssNr::NativeVulkanGuides::Enabled()&&nrRuntime.enabled&&(nrRoute==1||nrRoute==2)&&!DlssNr::PreparedGuides::ExternalRoute()){
        const auto nativeResult=DlssNr::NativeVulkanGuides::BeforePresent(*observed,localPresentInfo,capturedConsumer,&captureHandled);
        if(nativeResult!=VK_SUCCESS)return nativeResult;
        if(execution.handoff.state==DlssNr::VkPresentHandoffState::Uncertain)return execution.handoff.result;
        if(captureHandled){
            if(capturedVulkanRenderer&&execution.capturedCompletionObserved&&!execution.record.preparationOnly){
                const auto work=DlssNr::GetVulkanPresentStatus().Snapshot().workload.appliedWork;
                DlssNr::NativeVulkanGuides::ModelDimensions(*observed,work.width,work.height);
            }else if(!capturedVulkanRenderer){
                const auto model=DlssNr::Telemetry();
                if(model.running&&model.frameWidth==observed->request.extent.width&&model.frameHeight==observed->request.extent.height)
                    DlssNr::NativeVulkanGuides::ModelDimensions(*observed,model.workWidth,model.workHeight);
            }
        }
    }

    const auto nrNowMs=[] {return std::chrono::duration<double,std::milli>(NrClock::now().time_since_epoch()).count();};
    auto nrCall=DlssNr::GetVulkanPresentStatus().BeginCall(nrRoute,std::chrono::duration<double,std::milli>(nrHookStart.time_since_epoch()).count());
    struct NrPacingEnd {
        DlssNr::PresentPacing::CallToken token;NrClock::time_point start;double adapter=0,original=0;bool failed=true;
        ~NrPacingEnd(){DlssNr::GetVulkanPresentStatus().FinishCall(token,adapter,std::chrono::duration<double,std::milli>(NrClock::now()-start).count(),original,failed);}
    } nrPacing{nrCall,nrHookStart};
    if(observed&&nrSettings){
        DlssNr::PollVkNrCompletions();
        DlssNr::GetVulkanPresentExecutor().Maintain(*observed,!nrPresentEnabled||(nrRoute!=1&&nrRoute!=2));
        DlssNr::RetireInactiveVk(observed->device,nrPresentEnabled,nrRoute);
    }
    if (!standalone && !captureHandled && !DlssNr::Connections::PreparedOwnerClaimed() &&
        (!capturedVulkanRenderer||DlssNr::NativeGuides::SelectedSource()==0) && nrPresentEnabled && (nrRoute == 1 || nrRoute == 2))
    {
        if (observed)
        {
            auto& streamline=DlssNr::VulkanNrStreamlineAdapter();
            if(streamline.Generation()) {
                const auto activity=streamline.Activity();
                observed->request.fgStateKnown=activity!=DlssNr::VkNrFgActivity::Unknown;
                // A successful public unload supersedes a stale last SetOptions
                // mode. Independently selected FG adapters keep their own gate.
                observed->request.fgKnownActive=activity==DlssNr::VkNrFgActivity::On ||
                    host.activeFgInput!=FGInput::NoFG || host.activeFgOutput!=FGOutput::NoFG;
            }
            // Successfully created native/FFX providers do not prove an Off state.
            if(DlssNr::VkNrNgxFgFeatureCount()||DlssNr::VulkanNrFfxStatus().providerId)
                observed->request.fgStateKnown=false;
            const auto instance = _instance != VK_NULL_HANDLE ? _instance : State::Instance().VulkanInstance;
            execution = DlssNr::GetVulkanPresentExecutor().BeforePresent(*observed, instance, *pPresentInfo,
                std::make_shared<NrConfigSnapshot<Config>>(*nrSettings),guideSelection);
            if (execution.handoff.state == DlssNr::VkPresentHandoffState::Accepted)
                DlssNr::ApplyVkPresentHandoff(localPresentInfo, execution.handoff);
            else if (execution.handoff.state == DlssNr::VkPresentHandoffState::Uncertain)
            {
                ReportVkPresentResult(queue, *pPresentInfo, &*observed, &execution, false,
                    execution.handoff.result, execution.reason.c_str());
                return execution.handoff.result;
            }
        }
    }

    nrPacing.adapter=std::chrono::duration<double,std::milli>(NrClock::now()-nrHookStart).count();
    if(execution.handoff.state==DlssNr::VkPresentHandoffState::Accepted&&!execution.record.preparationOnly)
        DlssNr::GetVulkanPresentStatus().ExpectGpu(nrCall,execution.record.use,execution.record.requestedPasses,nrNowMs());
    // LoaderQueuePresent owns the only overlay attempt and its wait array lifetime.
    ReflexHooks::update(false, true);

    // original call
    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    const auto nrOriginalStart=NrClock::now();
    bool originalCalled=false;
    standaloneFrame.Finish();
    auto result = LoaderQueuePresent(queue, &localPresentInfo, target, &originalCalled);
    if(!originalCalled){
        if(observed)DlssNr::NativeVulkanGuides::AfterPresent(observed->device,result);
        if(execution.ticket)DlssNr::GetVulkanPresentExecutor().MarkUncertain(execution.ticket);
        ReportVkPresentResult(queue,*pPresentInfo,observed?&*observed:nullptr,
            observed?&execution:nullptr,false,result,"Vulkan loader overlay submission uncertain; original Present not called");
        return result;
    }
    nrPacing.original=std::chrono::duration<double,std::milli>(NrClock::now()-nrOriginalStart).count();
    nrPacing.failed=result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR;
    const VkResult selectedImageResult = DlssNr::VkWsiSelectedPresentResult(originalCalled,result,localPresentInfo);
    if(observed)DlssNr::NativeVulkanGuides::AfterPresent(observed->device,selectedImageResult);
    if (nrPresentEnabled && (nrRoute == 1 || nrRoute == 2))
        ReportVkPresentResult(queue, *pPresentInfo, observed ? &*observed : nullptr,
            observed ? &execution : nullptr, true, selectedImageResult,
            !observed ? "Vulkan swapchain or queue observation unavailable" :
            selectedImageResult != VK_SUCCESS && selectedImageResult != VK_SUBOPTIMAL_KHR ? "Original Vulkan Present failed" :
            execution.reason.c_str());
    if ((result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR) &&
        localPresentInfo.pSwapchains != nullptr && localPresentInfo.pImageIndices != nullptr)
        for (uint32_t i = 0; i < localPresentInfo.swapchainCount; ++i)
        {
            const VkResult imageResult = localPresentInfo.pResults != nullptr ? localPresentInfo.pResults[i] : result;
            if (imageResult == VK_SUCCESS || imageResult == VK_SUBOPTIMAL_KHR)
                DlssNr::GetVulkanPresentRegistry().PresentSucceeded(localPresentInfo.pSwapchains[i],
                                                                      localPresentInfo.pImageIndices[i],queue);
        }

    if(execution.ticket!=0)
        DlssNr::GetVulkanPresentExecutor().OriginalPresentReturned(execution.ticket,selectedImageResult,
            observed?DlssNr::GetVulkanPresentRegistry().LastPresentSerial(observed->swapchain,observed->request.imageIndex):0);

    // Unsure about Vulkan Reflex fps limit and if that could be causing an issue here
    if (!State::Instance().reflexLimitsFps)
        FrameLimit::sleep(false);

    LOG_FUNC_RESULT(result);
    return result;
}

#include "Vulkan_ApplicationPresent.inl"

VALIDATE_HOOK(hkvkCreateSwapchainKHR, PFN_vkCreateSwapchainKHR)
static VkResult VKAPI_CALL hkvkCreateSwapchainKHR(VkDevice device, const VkSwapchainCreateInfoKHR* pCreateInfo,
                                       const VkAllocationCallbacks* pAllocator, VkSwapchainKHR* pSwapchain)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkCreateSwapchainKHR>::Get(o_CreateSwapchainKHR);
    if(DlssNr::NativeFg::Internal())return target(device,pCreateInfo,pAllocator,pSwapchain);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::Create,(uintptr_t)device,pCreateInfo))
        return target(device,pCreateInfo,pAllocator,pSwapchain);
    LOG_FUNC();

    VkSwapchainCreateInfoKHR localCreateInfo {};
    const VkSwapchainCreateInfoKHR* submittedInfo = pCreateInfo;
    VkImageUsageFlags surfaceUsage = 0;
    const bool knownUsageChain = pCreateInfo != nullptr &&
                                DlssNr::IsVkSwapchainUsageChainKnown(pCreateInfo->pNext);
    auto& nrRegistry = DlssNr::GetVulkanPresentRegistry();
    const auto physical = nrRegistry.PhysicalDevice(device);
    if (pCreateInfo != nullptr)
    {
        const auto chain = DlssNr::DescribeVkSwapchainChain(pCreateInfo->pNext);
        std::string types;
        for (size_t i = 0; i < chain.count; ++i)
            types += (i == 0 ? "" : ",") + std::to_string(static_cast<uint32_t>(chain.types[i]));
        LOG_INFO("DLSS-NR Vulkan Present: swapchain request extent={}x{} format={} colorSpace={} "
                 "usage=0x{:X} flags=0x{:X} presentMode={} sharingMode={} queueFamilyCount={} "
                 "prepared={} pNextTypes=[{}] chainCycle={} chainTruncated={}",
                 pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height,
                 static_cast<uint32_t>(pCreateInfo->imageFormat),
                 static_cast<uint32_t>(pCreateInfo->imageColorSpace), pCreateInfo->imageUsage,
                 pCreateInfo->flags, static_cast<uint32_t>(pCreateInfo->presentMode),
                 static_cast<uint32_t>(pCreateInfo->imageSharingMode), pCreateInfo->queueFamilyIndexCount,
                 nrRegistry.PresentPrepared(device), types.empty() ? "none" : types,
                 chain.cycle, chain.truncated);
    }
    if (pCreateInfo != nullptr && physical != VK_NULL_HANDLE && o_vkGetInstanceProcAddr != nullptr)
    {
        const auto instance = State::Instance().VulkanInstance;
        auto getCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(
            o_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        auto getCapabilities2 = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(
            o_vkGetInstanceProcAddr(instance, "vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
        surfaceUsage = DlssNr::QueryVkSwapchainSurfaceUsage(physical, *pCreateInfo,
                                                          getCapabilities, getCapabilities2);

        const auto preparationSettings = TryNrConfigSnapshot(*Config::Instance());
        const bool requested = DlssNr::NativeVulkanGuides::Enabled() || (preparationSettings &&
                               preparationSettings->DlssNrVulkanPrepare.value_or_default() &&
                               nrRegistry.PresentPrepared(device));
        const auto usage = DlssNr::PrepareVkSwapchainUsage(pCreateInfo->imageUsage, surfaceUsage,
                                                           knownUsageChain, requested);
        if (requested)
            LOG_INFO("DLSS-NR Vulkan Present: swapchain usage qualification knownChain={} "
                     "surfaceUsage=0x{:X} effectiveUsage=0x{:X} amended={} reason={}",
                     knownUsageChain, surfaceUsage, usage.usage, usage.amended,
                     DlssNr::VkPresentRefusalText(usage.reason));
        if (usage.amended)
        {
            localCreateInfo = *pCreateInfo;
            localCreateInfo.imageUsage = usage.usage;
            submittedInfo = &localCreateInfo;
        }
        else if (requested && usage.reason != DlssNr::VkPresentRefusal::None)
            LOG_WARN("DLSS-NR Vulkan Present: swapchain transfer usage unchanged: {}",
                     DlssNr::VkPresentRefusalText(usage.reason));

        // Inspector samples only the physical output. Prepare its read-only
        // usage before actual creation even when NR is disabled or unprepared.
        // Unknown extension chains and unsupported surfaces keep caller usage.
        const auto inspectorUsage=Neurotic::Semantic::Character::CharacterVkSwapchainUsage(
            submittedInfo->imageUsage,surfaceUsage,knownUsageChain,pCreateInfo->flags);
        if (inspectorUsage!=submittedInfo->imageUsage)
        {
            localCreateInfo = *submittedInfo;
            localCreateInfo.imageUsage = inspectorUsage;
            submittedInfo = &localCreateInfo;
        }
    }

    ScopedVulkanCreatingSC scopedVulkanCreatingSC {};
    VkResult result = VK_SUCCESS;
    {
        ScopedSkipSpoofingGlobal skipSpoofingGlobal {};
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::Create,(uintptr_t)device,submittedInfo);
        DlssNr::VkWsiTargetScope<PFN_vkCreateSwapchainKHR> clear(nullptr);
        VkQueue fgQueue{};uint32_t fgFamily=UINT32_MAX;
        DlssNr::PreparedVulkan::DeviceInfo fgDevice;fgDevice.device=device;
        VkBool32 fgPresent=VK_FALSE;
        auto support=o_vkGetInstanceProcAddr?reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(o_vkGetInstanceProcAddr(State::Instance().VulkanInstance,"vkGetPhysicalDeviceSurfaceSupportKHR")):nullptr;
        const auto& fgHost=State::Instance();
        const auto qualificationRefusal=[&]()->const char*{
            if(!DlssNr::NativeFg::Selected())return "FG was off at startup; enable it and restart the game";
            if(!DlssNr::NativeVulkanGuides::Enabled())return "built-in Vulkan capture is not enabled at startup";
            if(!submittedInfo)return "swapchain creation information is missing";
            if(!nrRegistry.FramegenQueue(device,fgQueue,fgFamily))return "a single game graphics/compute queue family could not be selected";
            fgDevice.queue=fgQueue;
            if(!nrRegistry.PreparedDevice(fgDevice))return "game queue/device registration is incomplete";
            if(!fgDevice.timeline)return "timeline semaphores were not enabled on the game device";
            if(!fgDevice.externalMemoryWin32)return "Win32 shared GPU memory was not enabled on the game device";
            if(!fgDevice.externalSemaphoreWin32)return "Win32 shared GPU semaphores were not enabled on the game device";
            if(DlssNr::VulkanLoaderWsiDevices().ForQueue(fgQueue).device!=device)return "presentation queue/device registration does not match";
            if(!support)return "surface presentation support query is unavailable";
            if(support(physical,fgFamily,submittedInfo->surface,&fgPresent)!=VK_SUCCESS)return "surface presentation support query failed";
            if(!fgPresent)return "selected graphics queue cannot present to this surface";
            if(fgHost.activeFgInput!=FGInput::NoFG)return "another OptiScaler FG input is active";
            if(fgHost.activeFgOutput!=FGOutput::NoFG)return "another OptiScaler FG output is active";
            if(fgHost.dlssgLastSetMode.load()!=sl::DLSSGMode::eOff)return "Streamline DLSS frame generation is active";
            if(DlssNr::VkNrNgxFgFeatureCount())return "an NGX frame-generation feature is active";
            if(DlssNr::VulkanNrFfxStatus().providerId)return "an FFX frame-generation provider is active";
            return nullptr;
        }();
        const auto owned=DlssNr::NativeFg::Create(device,physical,fgQueue,fgFamily,submittedInfo,pAllocator,pSwapchain,
            qualificationRefusal==nullptr,qualificationRefusal);
        if(DlssNr::NativeFg::Selected())LOG_INFO("Standalone FG admission: family={} result={} status={}",
            fgFamily,owned?(int)*owned:(int)VK_ERROR_FEATURE_NOT_PRESENT,DlssNr::NativeFg::Status());
        if(owned)result=*owned;
        else if(submittedInfo&&DlssNr::NativeFg::Owns(submittedInfo->oldSwapchain))result=VK_ERROR_FEATURE_NOT_PRESENT;
        else result = target(device, submittedInfo, pAllocator, pSwapchain);
    }

    if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && *pSwapchain != VK_NULL_HANDLE &&
        !State::Instance().vulkanSkipHooks)
    {
        std::vector<VkImage> images;
        auto getImages = o_vkGetDeviceProcAddr != nullptr
            ? reinterpret_cast<PFN_vkGetSwapchainImagesKHR>(
                  o_vkGetDeviceProcAddr(device, "vkGetSwapchainImagesKHR"))
            : nullptr;
        if(DlssNr::NativeFg::Owns(*pSwapchain)){getImages=hkvkGetSwapchainImagesKHR;DlssNr::VulkanLoaderWsiDevices().ImagesDispatch(device,getImages);}
        if (getImages != nullptr)
            for(unsigned attempt=0;attempt<4;++attempt){
                uint32_t count=0;
                if(getImages(device,*pSwapchain,&count,nullptr)!=VK_SUCCESS||!count||count>256)break;
                images.resize(count);const auto capacity=count;
                const auto imageResult=getImages(device,*pSwapchain,&count,images.data());
                if(imageResult==VK_SUCCESS&&count&&count<=capacity){images.resize(count);break;}
                images.clear();if(imageResult!=VK_INCOMPLETE)break;
            }
        std::vector<uint32_t> presentFamilies;
        if (physical != VK_NULL_HANDLE && o_vkGetInstanceProcAddr != nullptr)
        {
            auto surfaceSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(
                o_vkGetInstanceProcAddr(State::Instance().VulkanInstance,
                                        "vkGetPhysicalDeviceSurfaceSupportKHR"));
            if (surfaceSupport != nullptr)
                for (const uint32_t family : nrRegistry.QueueFamilies(device))
                {
                    VkBool32 supported = VK_FALSE;
                    if (surfaceSupport(physical, family, pCreateInfo->surface, &supported) == VK_SUCCESS &&
                        supported == VK_TRUE)
                        presentFamilies.push_back(family);
                }
        }
        DlssNr::VulkanLoaderWsiDevices().Swapchain(device,*pSwapchain);
        const auto loaderGeneration=DlssNr::VulkanLoaderWsiDevices().Device(device).generation;
        for(size_t imageIndex=0;imageIndex<images.size();++imageIndex)
            DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Wsi,3,loaderGeneration,
                (uintptr_t)*pSwapchain,(uintptr_t)images[imageIndex],(uintptr_t)device,imageIndex,result,true);
        nrRegistry.SwapchainCreated(device, *pSwapchain, *submittedInfo, surfaceUsage, knownUsageChain,
                                    images, presentFamilies);
        DlssNr::GetVulkanLoaderPresentRegistry().SwapchainCreated(device,*pSwapchain,*submittedInfo,
            surfaceUsage,knownUsageChain,std::move(images),std::move(presentFamilies));

        State::Instance().screenWidth = static_cast<float>(pCreateInfo->imageExtent.width);
        State::Instance().screenHeight = static_cast<float>(pCreateInfo->imageExtent.height);
        {
            std::lock_guard<std::mutex> lock(g_swapchainExtentMutex);
            g_swapchainExtents[reinterpret_cast<uintptr_t>(*pSwapchain)] =
                { pCreateInfo->imageExtent.width, pCreateInfo->imageExtent.height };
        }

        // The same question the DXGI side asks: what does one unit of this buffer mean?
        //
        // EXTENDED_SRGB_LINEAR is scRGB, 1.0 = 80 nits. HDR10_ST2084 is PQ, 1.0 = 10000 nits. Both
        // are absolute, so in either the white point is arithmetic rather than a reading -- which
        // matters most for the games that supply no exposure texture, since nothing else answers for
        // them. Logged, not yet used.
        {
            static VkColorSpaceKHR lastSpace = (VkColorSpaceKHR) -1;

            if (pCreateInfo->imageColorSpace != lastSpace)
            {
                lastSpace = pCreateInfo->imageColorSpace;

                const char* name = "other";
                const char* meaning = "relative -- no scale to be had";

                switch (pCreateInfo->imageColorSpace)
                {
                case VK_COLOR_SPACE_EXTENDED_SRGB_LINEAR_EXT:
                    name = "scRGB (extended sRGB, linear)";
                    meaning = "absolute: 1.0 = 80 nits, so 203-nit paper white = 2.5375";
                    break;
                case VK_COLOR_SPACE_HDR10_ST2084_EXT:
                    name = "PQ / ST.2084 (HDR10)";
                    meaning = "absolute: 1.0 = 10000 nits, so 203-nit paper white = 0.0203";
                    break;
                case VK_COLOR_SPACE_SRGB_NONLINEAR_KHR:
                    name = "sRGB (SDR)";
                    break;
                case VK_COLOR_SPACE_HDR10_HLG_EXT:
                    name = "HLG";
                    break;
                default:
                    break;
                }

                LOG_INFO("DLSS-NR: swapchain colour space {} -- {} ({}), format {}",
                         (int) pCreateInfo->imageColorSpace, name, meaning, (int) pCreateInfo->imageFormat);
            }
        }

        LOG_DEBUG("if (result == VK_SUCCESS && device != VK_NULL_HANDLE && pCreateInfo != nullptr && pSwapchain != "
                  "VK_NULL_HANDLE)");

        _device = device;
        LOG_DEBUG("_device captured: {0:X}", (UINT64) _device);

        const auto loaderDevice=DlssNr::VulkanLoaderWsiDevices().Device(device);
        LoaderSurface surface;
        {std::lock_guard lock(loaderSurfaceMutex);if(const auto it=loaderSurfaces.find((uintptr_t)submittedInfo->surface);
             it!=loaderSurfaces.end()&&it->second.size()==1)surface=it->second.front();}
        if (Config::Instance()->OverlayMenu.value_or_default() && loaderDevice.generation && surface.instance)
            MenuOverlayVk::CreateSwapchain(device,loaderDevice.physical,surface.instance,surface.window,
                submittedInfo,pAllocator,pSwapchain,{loaderDevice.generation,
                    &DlssNr::GetVulkanLoaderPresentRegistry(),loaderDevice.getImages});
    }

    LOG_FUNC_RESULT(result);
    return result;
}

static void VKAPI_CALL hkvkDestroySurfaceKHR(VkInstance instance,VkSurfaceKHR surface,const VkAllocationCallbacks* allocator)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkDestroySurfaceKHR>::Get(o_DestroySurfaceKHR);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::SurfaceDestroy,(uintptr_t)instance,surface)){
        target(instance,surface,allocator);return;
    }
    DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::SurfaceDestroy,(uintptr_t)instance,surface);
    DlssNr::VkWsiTargetScope<PFN_vkDestroySurfaceKHR> clear(nullptr);
    {std::lock_guard lock(loaderSurfaceMutex);const auto it=loaderSurfaces.find((uintptr_t)surface);
     if(it!=loaderSurfaces.end()){
         std::erase_if(it->second,[&](const auto& candidate){return candidate.instance==instance;});
         if(it->second.empty())loaderSurfaces.erase(it);
     }}
    target(instance,surface,allocator);
}
static VkResult VKAPI_CALL hkvkGetFenceStatus(VkDevice device,VkFence fence)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkGetFenceStatus>::Get(o_GetFenceStatus);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::FenceStatus,(uintptr_t)device,fence))return target(device,fence);
    return DlssNr::ObserveVkAcquireFenceStatus(device,fence,[&]{
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::FenceStatus,(uintptr_t)device,fence);
        DlssNr::VkWsiTargetScope<PFN_vkGetFenceStatus> clear(nullptr);return target(device,fence);});
}
static VkResult VKAPI_CALL hkvkWaitForFences(VkDevice device,uint32_t count,const VkFence* fences,VkBool32 waitAll,uint64_t timeout)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkWaitForFences>::Get(o_WaitForFences);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::FenceWait,(uintptr_t)device,fences))return target(device,count,fences,waitAll,timeout);
    return DlssNr::ObserveVkAcquireFenceWait(device,count,fences,waitAll,[&]{
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::FenceWait,(uintptr_t)device,fences);
        DlssNr::VkWsiTargetScope<PFN_vkWaitForFences> clear(nullptr);return target(device,count,fences,waitAll,timeout);
    },[&](VkFence fence){return o_GetFenceStatus?o_GetFenceStatus(device,fence):VK_NOT_READY;});
}
static VkResult VKAPI_CALL hkvkResetFences(VkDevice device,uint32_t count,const VkFence* fences)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkResetFences>::Get(o_ResetFences);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::FenceReset,(uintptr_t)device,fences))return target(device,count,fences);
    return DlssNr::ObserveVkAcquireFenceReset(device,count,fences,[&]{
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::FenceReset,(uintptr_t)device,fences);
        DlssNr::VkWsiTargetScope<PFN_vkResetFences> clear(nullptr);return target(device,count,fences);
    },[&](VkFence fence){return o_GetFenceStatus?o_GetFenceStatus(device,fence):VK_NOT_READY;});
}
static void VKAPI_CALL hkvkDestroyFence(VkDevice device,VkFence fence,const VkAllocationCallbacks* allocator)
{
    auto target=DlssNr::VkWsiTargetScope<PFN_vkDestroyFence>::Get(o_DestroyFence);
    if(DlssNr::VkWsiForwardScope::Matches(DlssNr::VkWsiOperation::FenceDestroy,(uintptr_t)device,fence)){target(device,fence,allocator);return;}
    DlssNr::ObserveVkAcquireFenceDestroy(device,fence,[&]{
        DlssNr::VkWsiForwardScope forward(DlssNr::VkWsiOperation::FenceDestroy,(uintptr_t)device,fence);
        DlssNr::VkWsiTargetScope<PFN_vkDestroyFence> clear(nullptr);target(device,fence,allocator);
    },[&](VkFence observed){return o_GetFenceStatus?o_GetFenceStatus(device,observed):VK_NOT_READY;});
}
static PFN_vkVoidFunction LoaderWsiAddress(const char* name,PFN_vkVoidFunction original)
{
    if(!name||!original)return original;
#define WSI_ADDRESS(Name) if(std::strcmp(name,"vk" #Name)==0) { \
    using Slots=DlssNr::VkWsiFunctionSlots<DlssNr::VkWsiHookTag<hkvk##Name>,PFN_vk##Name>; \
    const auto wrapped=Slots::Wrap(reinterpret_cast<PFN_vk##Name>(original)); \
    if(!Slots::IsEntry(wrapped)){DlssNr::VulkanLoaderWsiDevices().Disable(); \
        LOG_WARN("Vulkan loader WSI dispatch capacity exhausted; overlay rights disabled");} \
    return reinterpret_cast<PFN_vkVoidFunction>(wrapped); }
    WSI_ADDRESS(CreateWin32SurfaceKHR) WSI_ADDRESS(DestroySurfaceKHR)
    WSI_ADDRESS(CreateSwapchainKHR) WSI_ADDRESS(GetSwapchainImagesKHR)
    WSI_ADDRESS(AcquireNextImageKHR) WSI_ADDRESS(AcquireNextImage2KHR)
    WSI_ADDRESS(QueuePresentKHR) WSI_ADDRESS(GetDeviceQueue) WSI_ADDRESS(GetDeviceQueue2)
    WSI_ADDRESS(GetFenceStatus) WSI_ADDRESS(WaitForFences) WSI_ADDRESS(ResetFences) WSI_ADDRESS(DestroyFence)
#undef WSI_ADDRESS
    if(std::strcmp(name,"vkDestroySwapchainKHR")==0){
        using Slots=DlssNr::VkWsiFunctionSlots<DlssNr::VkWsiHookTag<nrDestroySwapchainKHR>,PFN_vkDestroySwapchainKHR>;
        const auto wrapped=Slots::Wrap(reinterpret_cast<PFN_vkDestroySwapchainKHR>(original));
        if(!Slots::IsEntry(wrapped))DlssNr::VulkanLoaderWsiDevices().Disable();
        return reinterpret_cast<PFN_vkVoidFunction>(wrapped);
    }
    if(std::strcmp(name,"vkDestroyDevice")==0){
        using Slots=DlssNr::VkWsiFunctionSlots<DlssNr::VkWsiHookTag<nrDestroyDevice>,PFN_vkDestroyDevice>;
        const auto wrapped=Slots::Wrap(reinterpret_cast<PFN_vkDestroyDevice>(original));
        if(!Slots::IsEntry(wrapped))DlssNr::VulkanLoaderWsiDevices().Disable();
        return reinterpret_cast<PFN_vkVoidFunction>(wrapped);
    }
    return original;
}

VALIDATE_HOOK(hkvkGetInstanceProcAddr, PFN_vkGetInstanceProcAddr)
PFN_vkVoidFunction hkvkGetInstanceProcAddr(VkInstance instance, const char* pName)
{
    auto orgFunc = o_vkGetInstanceProcAddr(instance, pName);

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if(std::strcmp(pName,"vkGetDeviceProcAddr")==0)return reinterpret_cast<PFN_vkVoidFunction>(hkvkGetDeviceProcAddr);
    if(std::strcmp(pName,"vkGetInstanceProcAddr")==0)return reinterpret_cast<PFN_vkVoidFunction>(hkvkGetInstanceProcAddr);
    if(std::strcmp(pName,"vkDestroyInstance")==0)return reinterpret_cast<PFN_vkVoidFunction>(
        DlssNr::VkWsiFunctionSlots<DlssNr::VkWsiHookTag<&PreparedDestroyInstance>,PFN_vkDestroyInstance>::Wrap(
            reinterpret_cast<PFN_vkDestroyInstance>(orgFunc)));
    if(auto wsi=LoaderWsiAddress(pName,orgFunc);wsi!=orgFunc)return wsi;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetInstanceProcAddr(orgFunc, pName);
    if (result == orgFunc) if (auto nr = NrRecordingAddress(pName)) return nr;
    if (result != VK_NULL_HANDLE)
        return result==orgFunc?NrWorkAddress(pName,result):result;

    return NrWorkAddress(pName,orgFunc);
}

// Resolver observations are provenance, never terminal-WSI or resource rights.
// Only changed WSI address tuples are logged; no hot-path disk trace per frame.
static void TraceVkWsiResolver(VkDevice device,const char* name,PFN_vkVoidFunction address,void* caller)
{
    if(!name)return;
    const char* names[]={"vkCreateSwapchainKHR","vkGetSwapchainImagesKHR","vkAcquireNextImageKHR","vkAcquireNextImage2KHR","vkQueuePresentKHR"};
    uint32_t kind=0;for(;kind<std::size(names);++kind)if(std::strcmp(name,names[kind])==0)break;
    if(kind==std::size(names))return;
    struct Seen {VkDevice device;PFN_vkVoidFunction address;void* caller;uint32_t kind;};
    static std::mutex mutex;static std::array<Seen,64> seen{};static size_t count=0;
    {std::unique_lock lock(mutex,std::try_to_lock);if(!lock.owns_lock())return;
     for(size_t i=0;i<count;++i)if(seen[i].device==device&&seen[i].address==address&&seen[i].caller==caller&&seen[i].kind==kind)return;
     if(count==seen.size())return;seen[count++]={device,address,caller,kind};}
    const auto moduleName=[](const void* pointer){HMODULE module=nullptr;char path[MAX_PATH]{};
        if(!pointer||!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(pointer),&module)||!GetModuleFileNameA(module,path,MAX_PATH))return std::string("unresolved");
        return std::filesystem::path(path).filename().string();};
    DlssNr::VkFlight::Current().Write(DlssNr::VkFlight::Kind::Resolver,kind,0,
        reinterpret_cast<uintptr_t>(address),0,reinterpret_cast<uintptr_t>(device),reinterpret_cast<uintptr_t>(caller),0,true);
    LOG_INFO("Vulkan WSI resolver: api={} device={} address={} implementation={} caller={} boundaryRole=unproved",
        name,reinterpret_cast<uintptr_t>(device),reinterpret_cast<uintptr_t>(address),moduleName(reinterpret_cast<const void*>(address)),moduleName(caller));
}

VALIDATE_HOOK(hkvkGetDeviceProcAddr, PFN_vkGetDeviceProcAddr)
PFN_vkVoidFunction hkvkGetDeviceProcAddr(VkDevice device, const char* pName)
{
    auto orgFunc = o_vkGetDeviceProcAddr(device, pName);
    TraceVkWsiResolver(device,pName,orgFunc,_ReturnAddress());

    if (orgFunc == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;

    if(std::strcmp(pName,"vkGetDeviceProcAddr")==0)return reinterpret_cast<PFN_vkVoidFunction>(hkvkGetDeviceProcAddr);
    if(std::strcmp(pName,"vkGetInstanceProcAddr")==0)return reinterpret_cast<PFN_vkVoidFunction>(hkvkGetInstanceProcAddr);
    if(auto wsi=LoaderWsiAddress(pName,orgFunc);wsi!=orgFunc)return wsi;

    auto procName = std::string(pName);

    if (procName == std::string("vkCreateInstance"))
    {
        if (o_vkCreateInstance == nullptr)
            o_vkCreateInstance = (PFN_vkCreateInstance) orgFunc;

        LOG_DEBUG("vkCreateInstance");
        return (PFN_vkVoidFunction) hkvkCreateInstance;
    }
    else if (procName == std::string("vkCreateDevice"))
    {
        if (o_vkCreateDevice == nullptr)
            o_vkCreateDevice = (PFN_vkCreateDevice) orgFunc;

        LOG_DEBUG("vkCreateDevice");
        return (PFN_vkVoidFunction) hkvkCreateDevice;
    }

    auto result = VulkanSpoofing::hkvkGetDeviceProcAddr(orgFunc, pName);
    if (result == orgFunc) if (auto nr = NrRecordingAddress(pName)) return nr;
    if (result != VK_NULL_HANDLE)
        return result==orgFunc?NrWorkAddress(pName,result):result;

    return NrWorkAddress(pName,orgFunc);
}

#if defined(NR_DIAG_VULKAN_ONLY) && NR_DIAG_VULKAN_ONLY
static LONG diagnosticInstallStatus = ERROR_IO_PENDING;
LONG VulkanHooks::DiagnosticInstallStatus() { return diagnosticInstallStatus; }
#endif

void VulkanHooks::Hook(HMODULE vulkan1)
{
    if (vulkanModule == nullptr)
        vulkanModule = vulkan1;

    VulkanSpoofing::HookForVulkanSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanExtensionSpoofing(vulkan1);
    VulkanSpoofing::HookForVulkanVRAMSpoofing(vulkan1);

    if (o_vkCreateDevice != nullptr)
        return;

    FARPROC address = nullptr;

#define WSI_LOAD(Name,Target) Target=reinterpret_cast<PFN_vk##Name>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vk" #Name));
    WSI_LOAD(DestroySurfaceKHR,o_DestroySurfaceKHR)
    WSI_LOAD(QueuePresentKHR,o_QueuePresentKHR) WSI_LOAD(CreateSwapchainKHR,o_CreateSwapchainKHR)
    WSI_LOAD(AcquireNextImageKHR,o_AcquireNextImageKHR) WSI_LOAD(AcquireNextImage2KHR,o_AcquireNextImage2KHR)
    WSI_LOAD(GetSwapchainImagesKHR,o_GetSwapchainImagesKHR) WSI_LOAD(GetDeviceQueue,o_GetDeviceQueue) WSI_LOAD(GetDeviceQueue2,o_GetDeviceQueue2)
    WSI_LOAD(GetFenceStatus,o_GetFenceStatus) WSI_LOAD(WaitForFences,o_WaitForFences)
    WSI_LOAD(ResetFences,o_ResetFences) WSI_LOAD(DestroyFence,o_DestroyFence)
#undef WSI_LOAD
    o_vkCreateDevice = (PFN_vkCreateDevice) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateDevice");
#define NR_LOAD(Name) nr_##Name = reinterpret_cast<PFN_vk##Name>(KernelBaseProxy::GetProcAddress_()(vulkan1, "vk" #Name));
    NR_RECORDING_FUNCTIONS(NR_LOAD)
#undef NR_LOAD
    o_vkCreateInstance = (PFN_vkCreateInstance) KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateInstance");

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetInstanceProcAddr");
    o_vkGetInstanceProcAddr = (PFN_vkGetInstanceProcAddr) address;
    preparedDestroyInstance=reinterpret_cast<PFN_vkDestroyInstance>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkDestroyInstance"));

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetDeviceProcAddr");
    o_vkGetDeviceProcAddr = (PFN_vkGetDeviceProcAddr) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateWin32SurfaceKHR");
    o_vkCreateWin32SurfaceKHR = (PFN_vkCreateWin32SurfaceKHR) address;

    // address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCmdPipelineBarrier");
    // o_vkCmdPipelineBarrier = (PFN_vkCmdPipelineBarrier) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkGetPhysicalDeviceFeatures2");
    o_vkGetPhysicalDeviceFeatures2 = (PFN_vkGetPhysicalDeviceFeatures2) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkCreateSemaphore");
    o_vkCreateSemaphore = (PFN_vkCreateSemaphore) address;

    address = KernelBaseProxy::GetProcAddress_()(vulkan1, "vkSignalSemaphore");
    o_vkSignalSemaphore = (PFN_vkSignalSemaphore) address;

    NrWorkDispatch::original=reinterpret_cast<PFN_vkCmdDispatch>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDispatch"));
    NrWorkDispatchIndirect::original=reinterpret_cast<PFN_vkCmdDispatchIndirect>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDispatchIndirect"));
    NrWorkDraw::original=reinterpret_cast<PFN_vkCmdDraw>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDraw"));
    NrWorkDrawIndexed::original=reinterpret_cast<PFN_vkCmdDrawIndexed>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndexed"));
    NrWorkDrawIndirect::original=reinterpret_cast<PFN_vkCmdDrawIndirect>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndirect"));
    NrWorkDrawIndexedIndirect::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirect>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndexedIndirect"));
    NrWorkCopyImage::original=reinterpret_cast<PFN_vkCmdCopyImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyImage"));
    NrWorkCopyBufferToImage::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyBufferToImage"));
    NrWorkBlitImage::original=reinterpret_cast<PFN_vkCmdBlitImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdBlitImage"));
    NrWorkResolveImage::original=reinterpret_cast<PFN_vkCmdResolveImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdResolveImage"));
    NrWorkClearColorImage::original=reinterpret_cast<PFN_vkCmdClearColorImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdClearColorImage"));
    NrWorkClearDepthStencilImage::original=reinterpret_cast<PFN_vkCmdClearDepthStencilImage>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdClearDepthStencilImage"));
    NrWorkClearAttachments::original=reinterpret_cast<PFN_vkCmdClearAttachments>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdClearAttachments"));
    NrWorkCopyImage2::original=reinterpret_cast<PFN_vkCmdCopyImage2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyImage2"));
    NrWorkCopyImage2KHR::original=reinterpret_cast<PFN_vkCmdCopyImage2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyImage2KHR"));
    NrWorkCopyBufferToImage2::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyBufferToImage2"));
    NrWorkCopyBufferToImage2KHR::original=reinterpret_cast<PFN_vkCmdCopyBufferToImage2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdCopyBufferToImage2KHR"));
    NrWorkBlitImage2::original=reinterpret_cast<PFN_vkCmdBlitImage2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdBlitImage2"));
    NrWorkBlitImage2KHR::original=reinterpret_cast<PFN_vkCmdBlitImage2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdBlitImage2KHR"));
    NrWorkResolveImage2::original=reinterpret_cast<PFN_vkCmdResolveImage2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdResolveImage2"));
    NrWorkResolveImage2KHR::original=reinterpret_cast<PFN_vkCmdResolveImage2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdResolveImage2KHR"));
    NrWorkTraceRaysKHR::original=reinterpret_cast<PFN_vkCmdTraceRaysKHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdTraceRaysKHR"));
    NrWorkTraceRaysIndirectKHR::original=reinterpret_cast<PFN_vkCmdTraceRaysIndirectKHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdTraceRaysIndirectKHR"));
    NrWorkTraceRaysIndirect2KHR::original=reinterpret_cast<PFN_vkCmdTraceRaysIndirect2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdTraceRaysIndirect2KHR"));
    NrWorkDrawMeshTasksEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksEXT>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksEXT"));
    NrWorkDrawMeshTasksIndirectEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectEXT>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksIndirectEXT"));
    NrWorkDrawMeshTasksIndirectCountEXT::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountEXT>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksIndirectCountEXT"));
    NrWorkDrawIndirectCount::original=reinterpret_cast<PFN_vkCmdDrawIndirectCount>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndirectCount"));
    NrWorkDrawIndexedIndirectCount::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCount>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndexedIndirectCount"));
    NrWorkDispatchBase::original=reinterpret_cast<PFN_vkCmdDispatchBase>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDispatchBase"));
    NrWorkDispatchBaseKHR::original=reinterpret_cast<PFN_vkCmdDispatchBaseKHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDispatchBaseKHR"));
    NrWorkDrawIndirectCountKHR::original=reinterpret_cast<PFN_vkCmdDrawIndirectCountKHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndirectCountKHR"));
    NrWorkDrawIndexedIndirectCountKHR::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCountKHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndexedIndirectCountKHR"));
    NrWorkDrawIndirectCountAMD::original=reinterpret_cast<PFN_vkCmdDrawIndirectCountAMD>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndirectCountAMD"));
    NrWorkDrawIndexedIndirectCountAMD::original=reinterpret_cast<PFN_vkCmdDrawIndexedIndirectCountAMD>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawIndexedIndirectCountAMD"));
    NrWorkTraceRaysNV::original=reinterpret_cast<PFN_vkCmdTraceRaysNV>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdTraceRaysNV"));
    NrWorkDrawMeshTasksNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksNV>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksNV"));
    NrWorkDrawMeshTasksIndirectNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectNV>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksIndirectNV"));
    NrWorkDrawMeshTasksIndirectCountNV::original=reinterpret_cast<PFN_vkCmdDrawMeshTasksIndirectCountNV>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdDrawMeshTasksIndirectCountNV"));
    NrWorkPipelineBarrier::original=reinterpret_cast<PFN_vkCmdPipelineBarrier>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdPipelineBarrier"));
    NrWorkPipelineBarrier2::original=reinterpret_cast<PFN_vkCmdPipelineBarrier2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdPipelineBarrier2"));
    NrWorkPipelineBarrier2KHR::original=reinterpret_cast<PFN_vkCmdPipelineBarrier2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdPipelineBarrier2KHR"));
    NrWorkWaitEvents::original=reinterpret_cast<PFN_vkCmdWaitEvents>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdWaitEvents"));
    NrWorkWaitEvents2::original=reinterpret_cast<PFN_vkCmdWaitEvents2>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdWaitEvents2"));
    NrWorkWaitEvents2KHR::original=reinterpret_cast<PFN_vkCmdWaitEvents2KHR>(KernelBaseProxy::GetProcAddress_()(vulkan1,"vkCmdWaitEvents2KHR"));
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());

#define WSI_ATTACH(Name,Target) if(Target)DetourAttach(&(PVOID&)Target,hkvk##Name);
    WSI_ATTACH(DestroySurfaceKHR,o_DestroySurfaceKHR)
    WSI_ATTACH(QueuePresentKHR,o_QueuePresentKHR) WSI_ATTACH(CreateSwapchainKHR,o_CreateSwapchainKHR)
    WSI_ATTACH(AcquireNextImageKHR,o_AcquireNextImageKHR) WSI_ATTACH(AcquireNextImage2KHR,o_AcquireNextImage2KHR)
    WSI_ATTACH(GetSwapchainImagesKHR,o_GetSwapchainImagesKHR) WSI_ATTACH(GetDeviceQueue,o_GetDeviceQueue) WSI_ATTACH(GetDeviceQueue2,o_GetDeviceQueue2)
    WSI_ATTACH(GetFenceStatus,o_GetFenceStatus) WSI_ATTACH(WaitForFences,o_WaitForFences)
    WSI_ATTACH(ResetFences,o_ResetFences) WSI_ATTACH(DestroyFence,o_DestroyFence)
#undef WSI_ATTACH
    if (o_vkCreateDevice != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);
#define NR_ATTACH(Name) if (nr_##Name) DetourAttach(&(PVOID&) nr_##Name, nr##Name);
    NR_RECORDING_FUNCTIONS(NR_ATTACH)
#undef NR_ATTACH
#if !defined(NR_DIAG_VULKAN_NO_OBSERVERS) || !NR_DIAG_VULKAN_NO_OBSERVERS || (defined(NR_DIAG_VULKAN_COMMAND_ONLY) && NR_DIAG_VULKAN_COMMAND_ONLY)
    if(NrWorkDispatchBase::original)DetourAttach(&(PVOID&)NrWorkDispatchBase::original,NrWorkDispatchBase::Invoke);
    if(NrWorkDispatchBaseKHR::original)DetourAttach(&(PVOID&)NrWorkDispatchBaseKHR::original,NrWorkDispatchBaseKHR::Invoke);
    if(NrWorkDrawIndirectCountKHR::original)DetourAttach(&(PVOID&)NrWorkDrawIndirectCountKHR::original,NrWorkDrawIndirectCountKHR::Invoke);
    if(NrWorkDrawIndexedIndirectCountKHR::original)DetourAttach(&(PVOID&)NrWorkDrawIndexedIndirectCountKHR::original,NrWorkDrawIndexedIndirectCountKHR::Invoke);
    if(NrWorkDrawIndirectCountAMD::original)DetourAttach(&(PVOID&)NrWorkDrawIndirectCountAMD::original,NrWorkDrawIndirectCountAMD::Invoke);
    if(NrWorkDrawIndexedIndirectCountAMD::original)DetourAttach(&(PVOID&)NrWorkDrawIndexedIndirectCountAMD::original,NrWorkDrawIndexedIndirectCountAMD::Invoke);
    if(NrWorkTraceRaysNV::original)DetourAttach(&(PVOID&)NrWorkTraceRaysNV::original,NrWorkTraceRaysNV::Invoke);
    if(NrWorkDrawMeshTasksNV::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksNV::original,NrWorkDrawMeshTasksNV::Invoke);
    if(NrWorkDrawMeshTasksIndirectNV::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksIndirectNV::original,NrWorkDrawMeshTasksIndirectNV::Invoke);
    if(NrWorkDrawMeshTasksIndirectCountNV::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksIndirectCountNV::original,NrWorkDrawMeshTasksIndirectCountNV::Invoke);
    if(NrWorkPipelineBarrier::original)DetourAttach(&(PVOID&)NrWorkPipelineBarrier::original,NrWorkPipelineBarrier::Invoke);
    if(NrWorkPipelineBarrier2::original)DetourAttach(&(PVOID&)NrWorkPipelineBarrier2::original,NrWorkPipelineBarrier2::Invoke);
    if(NrWorkPipelineBarrier2KHR::original)DetourAttach(&(PVOID&)NrWorkPipelineBarrier2KHR::original,NrWorkPipelineBarrier2KHR::Invoke);
    if(NrWorkWaitEvents::original)DetourAttach(&(PVOID&)NrWorkWaitEvents::original,NrWorkWaitEvents::Invoke);
    if(NrWorkWaitEvents2::original)DetourAttach(&(PVOID&)NrWorkWaitEvents2::original,NrWorkWaitEvents2::Invoke);
    if(NrWorkWaitEvents2KHR::original)DetourAttach(&(PVOID&)NrWorkWaitEvents2KHR::original,NrWorkWaitEvents2KHR::Invoke);
    if(NrWorkDispatch::original)DetourAttach(&(PVOID&)NrWorkDispatch::original,NrWorkDispatch::Invoke);
    if(NrWorkDispatchIndirect::original)DetourAttach(&(PVOID&)NrWorkDispatchIndirect::original,NrWorkDispatchIndirect::Invoke);
    if(NrWorkDraw::original)DetourAttach(&(PVOID&)NrWorkDraw::original,NrWorkDraw::Invoke);
    if(NrWorkDrawIndexed::original)DetourAttach(&(PVOID&)NrWorkDrawIndexed::original,NrWorkDrawIndexed::Invoke);
    if(NrWorkDrawIndirect::original)DetourAttach(&(PVOID&)NrWorkDrawIndirect::original,NrWorkDrawIndirect::Invoke);
    if(NrWorkDrawIndexedIndirect::original)DetourAttach(&(PVOID&)NrWorkDrawIndexedIndirect::original,NrWorkDrawIndexedIndirect::Invoke);
    if(NrWorkCopyImage::original)DetourAttach(&(PVOID&)NrWorkCopyImage::original,NrWorkCopyImage::Invoke);
    if(NrWorkCopyBufferToImage::original)DetourAttach(&(PVOID&)NrWorkCopyBufferToImage::original,NrWorkCopyBufferToImage::Invoke);
    if(NrWorkBlitImage::original)DetourAttach(&(PVOID&)NrWorkBlitImage::original,NrWorkBlitImage::Invoke);
    if(NrWorkResolveImage::original)DetourAttach(&(PVOID&)NrWorkResolveImage::original,NrWorkResolveImage::Invoke);
    if(NrWorkClearColorImage::original)DetourAttach(&(PVOID&)NrWorkClearColorImage::original,NrWorkClearColorImage::Invoke);
    if(NrWorkClearDepthStencilImage::original)DetourAttach(&(PVOID&)NrWorkClearDepthStencilImage::original,NrWorkClearDepthStencilImage::Invoke);
    if(NrWorkClearAttachments::original)DetourAttach(&(PVOID&)NrWorkClearAttachments::original,NrWorkClearAttachments::Invoke);
    if(NrWorkCopyImage2::original)DetourAttach(&(PVOID&)NrWorkCopyImage2::original,NrWorkCopyImage2::Invoke);
    if(NrWorkCopyImage2KHR::original)DetourAttach(&(PVOID&)NrWorkCopyImage2KHR::original,NrWorkCopyImage2KHR::Invoke);
    if(NrWorkCopyBufferToImage2::original)DetourAttach(&(PVOID&)NrWorkCopyBufferToImage2::original,NrWorkCopyBufferToImage2::Invoke);
    if(NrWorkCopyBufferToImage2KHR::original)DetourAttach(&(PVOID&)NrWorkCopyBufferToImage2KHR::original,NrWorkCopyBufferToImage2KHR::Invoke);
    if(NrWorkBlitImage2::original)DetourAttach(&(PVOID&)NrWorkBlitImage2::original,NrWorkBlitImage2::Invoke);
    if(NrWorkBlitImage2KHR::original)DetourAttach(&(PVOID&)NrWorkBlitImage2KHR::original,NrWorkBlitImage2KHR::Invoke);
    if(NrWorkResolveImage2::original)DetourAttach(&(PVOID&)NrWorkResolveImage2::original,NrWorkResolveImage2::Invoke);
    if(NrWorkResolveImage2KHR::original)DetourAttach(&(PVOID&)NrWorkResolveImage2KHR::original,NrWorkResolveImage2KHR::Invoke);
    if(NrWorkTraceRaysKHR::original)DetourAttach(&(PVOID&)NrWorkTraceRaysKHR::original,NrWorkTraceRaysKHR::Invoke);
    if(NrWorkTraceRaysIndirectKHR::original)DetourAttach(&(PVOID&)NrWorkTraceRaysIndirectKHR::original,NrWorkTraceRaysIndirectKHR::Invoke);
    if(NrWorkTraceRaysIndirect2KHR::original)DetourAttach(&(PVOID&)NrWorkTraceRaysIndirect2KHR::original,NrWorkTraceRaysIndirect2KHR::Invoke);
    if(NrWorkDrawMeshTasksEXT::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksEXT::original,NrWorkDrawMeshTasksEXT::Invoke);
    if(NrWorkDrawMeshTasksIndirectEXT::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksIndirectEXT::original,NrWorkDrawMeshTasksIndirectEXT::Invoke);
    if(NrWorkDrawMeshTasksIndirectCountEXT::original)DetourAttach(&(PVOID&)NrWorkDrawMeshTasksIndirectCountEXT::original,NrWorkDrawMeshTasksIndirectCountEXT::Invoke);
    if(NrWorkDrawIndirectCount::original)DetourAttach(&(PVOID&)NrWorkDrawIndirectCount::original,NrWorkDrawIndirectCount::Invoke);
    if(NrWorkDrawIndexedIndirectCount::original)DetourAttach(&(PVOID&)NrWorkDrawIndexedIndirectCount::original,NrWorkDrawIndexedIndirectCount::Invoke);
#endif

    if (o_vkGetInstanceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetInstanceProcAddr, hkvkGetInstanceProcAddr);
    if(preparedDestroyInstance)DetourAttach(&(PVOID&)preparedDestroyInstance,PreparedDestroyInstance);

    if (o_vkGetDeviceProcAddr != nullptr)
        DetourAttach(&(PVOID&) o_vkGetDeviceProcAddr, hkvkGetDeviceProcAddr);

    if (o_vkCreateInstance != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourAttach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourAttach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    auto detourResult = DetourTransactionCommit();
#if defined(NR_DIAG_VULKAN_ONLY) && NR_DIAG_VULKAN_ONLY
    diagnosticInstallStatus = detourResult;
#endif
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to hook Vulkan, error code: {:X}", detourResult);
        o_vkCreateDevice = nullptr;
        o_vkCreateInstance = nullptr;
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        o_GetSwapchainImagesKHR=nullptr;o_GetDeviceQueue=nullptr;o_GetDeviceQueue2=nullptr;o_DestroySurfaceKHR=nullptr;
        o_GetFenceStatus=nullptr;o_WaitForFences=nullptr;o_ResetFences=nullptr;o_DestroyFence=nullptr;
#define NR_CLEAR(Name) nr_##Name = nullptr;
        NR_RECORDING_FUNCTIONS(NR_CLEAR)
#undef NR_CLEAR
        // o_vkCmdPipelineBarrier = nullptr;
    }
}

void VulkanHooks::Unhook()
{
    const bool legacyDetached=DlssNr::VkNrDeviceSubmit::Uninstall();
    const bool submit2Detached=DlssNr::VkNrDeviceSubmit2::Uninstall();
    if(!legacyDetached||!submit2Detached)
        LOG_WARN("Vulkan NR device submission hooks could not all be detached");
    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    if(NrWorkDispatch::original)DetourDetach(&(PVOID&)NrWorkDispatch::original,NrWorkDispatch::Invoke);
    if(NrWorkDispatchIndirect::original)DetourDetach(&(PVOID&)NrWorkDispatchIndirect::original,NrWorkDispatchIndirect::Invoke);
    if(NrWorkDraw::original)DetourDetach(&(PVOID&)NrWorkDraw::original,NrWorkDraw::Invoke);
    if(NrWorkDrawIndexed::original)DetourDetach(&(PVOID&)NrWorkDrawIndexed::original,NrWorkDrawIndexed::Invoke);
    if(NrWorkDrawIndirect::original)DetourDetach(&(PVOID&)NrWorkDrawIndirect::original,NrWorkDrawIndirect::Invoke);
    if(NrWorkDrawIndexedIndirect::original)DetourDetach(&(PVOID&)NrWorkDrawIndexedIndirect::original,NrWorkDrawIndexedIndirect::Invoke);
    if(NrWorkCopyImage::original)DetourDetach(&(PVOID&)NrWorkCopyImage::original,NrWorkCopyImage::Invoke);
    if(NrWorkCopyBufferToImage::original)DetourDetach(&(PVOID&)NrWorkCopyBufferToImage::original,NrWorkCopyBufferToImage::Invoke);
    if(NrWorkBlitImage::original)DetourDetach(&(PVOID&)NrWorkBlitImage::original,NrWorkBlitImage::Invoke);
    if(NrWorkResolveImage::original)DetourDetach(&(PVOID&)NrWorkResolveImage::original,NrWorkResolveImage::Invoke);
    if(NrWorkClearColorImage::original)DetourDetach(&(PVOID&)NrWorkClearColorImage::original,NrWorkClearColorImage::Invoke);
    if(NrWorkClearDepthStencilImage::original)DetourDetach(&(PVOID&)NrWorkClearDepthStencilImage::original,NrWorkClearDepthStencilImage::Invoke);
    if(NrWorkClearAttachments::original)DetourDetach(&(PVOID&)NrWorkClearAttachments::original,NrWorkClearAttachments::Invoke);
    if(NrWorkCopyImage2::original)DetourDetach(&(PVOID&)NrWorkCopyImage2::original,NrWorkCopyImage2::Invoke);
    if(NrWorkCopyImage2KHR::original)DetourDetach(&(PVOID&)NrWorkCopyImage2KHR::original,NrWorkCopyImage2KHR::Invoke);
    if(NrWorkCopyBufferToImage2::original)DetourDetach(&(PVOID&)NrWorkCopyBufferToImage2::original,NrWorkCopyBufferToImage2::Invoke);
    if(NrWorkCopyBufferToImage2KHR::original)DetourDetach(&(PVOID&)NrWorkCopyBufferToImage2KHR::original,NrWorkCopyBufferToImage2KHR::Invoke);
    if(NrWorkBlitImage2::original)DetourDetach(&(PVOID&)NrWorkBlitImage2::original,NrWorkBlitImage2::Invoke);
    if(NrWorkBlitImage2KHR::original)DetourDetach(&(PVOID&)NrWorkBlitImage2KHR::original,NrWorkBlitImage2KHR::Invoke);
    if(NrWorkResolveImage2::original)DetourDetach(&(PVOID&)NrWorkResolveImage2::original,NrWorkResolveImage2::Invoke);
    if(NrWorkResolveImage2KHR::original)DetourDetach(&(PVOID&)NrWorkResolveImage2KHR::original,NrWorkResolveImage2KHR::Invoke);
    if(NrWorkTraceRaysKHR::original)DetourDetach(&(PVOID&)NrWorkTraceRaysKHR::original,NrWorkTraceRaysKHR::Invoke);
    if(NrWorkTraceRaysIndirectKHR::original)DetourDetach(&(PVOID&)NrWorkTraceRaysIndirectKHR::original,NrWorkTraceRaysIndirectKHR::Invoke);
    if(NrWorkTraceRaysIndirect2KHR::original)DetourDetach(&(PVOID&)NrWorkTraceRaysIndirect2KHR::original,NrWorkTraceRaysIndirect2KHR::Invoke);
    if(NrWorkDrawMeshTasksEXT::original)DetourDetach(&(PVOID&)NrWorkDrawMeshTasksEXT::original,NrWorkDrawMeshTasksEXT::Invoke);
    if(NrWorkDrawMeshTasksIndirectEXT::original)DetourDetach(&(PVOID&)NrWorkDrawMeshTasksIndirectEXT::original,NrWorkDrawMeshTasksIndirectEXT::Invoke);
    if(NrWorkDrawMeshTasksIndirectCountEXT::original)DetourDetach(&(PVOID&)NrWorkDrawMeshTasksIndirectCountEXT::original,NrWorkDrawMeshTasksIndirectCountEXT::Invoke);
    if(NrWorkDrawIndirectCount::original)DetourDetach(&(PVOID&)NrWorkDrawIndirectCount::original,NrWorkDrawIndirectCount::Invoke);
    if(NrWorkDrawIndexedIndirectCount::original)DetourDetach(&(PVOID&)NrWorkDrawIndexedIndirectCount::original,NrWorkDrawIndexedIndirectCount::Invoke);
    if(NrWorkPipelineBarrier::original)DetourDetach(&(PVOID&)NrWorkPipelineBarrier::original,NrWorkPipelineBarrier::Invoke);
    if(NrWorkPipelineBarrier2::original)DetourDetach(&(PVOID&)NrWorkPipelineBarrier2::original,NrWorkPipelineBarrier2::Invoke);
    if(NrWorkPipelineBarrier2KHR::original)DetourDetach(&(PVOID&)NrWorkPipelineBarrier2KHR::original,NrWorkPipelineBarrier2KHR::Invoke);
    if(NrWorkWaitEvents::original)DetourDetach(&(PVOID&)NrWorkWaitEvents::original,NrWorkWaitEvents::Invoke);
    if(NrWorkWaitEvents2::original)DetourDetach(&(PVOID&)NrWorkWaitEvents2::original,NrWorkWaitEvents2::Invoke);
    if(NrWorkWaitEvents2KHR::original)DetourDetach(&(PVOID&)NrWorkWaitEvents2KHR::original,NrWorkWaitEvents2KHR::Invoke);
#define NR_DETACH(Name) if (nr_##Name) DetourDetach(&(PVOID&) nr_##Name, nr##Name);
    NR_RECORDING_FUNCTIONS(NR_DETACH)
#undef NR_DETACH

    if(o_DestroySurfaceKHR)DetourDetach(&(PVOID&)o_DestroySurfaceKHR,hkvkDestroySurfaceKHR);
    if(o_GetSwapchainImagesKHR)DetourDetach(&(PVOID&)o_GetSwapchainImagesKHR,hkvkGetSwapchainImagesKHR);
    if(o_GetDeviceQueue)DetourDetach(&(PVOID&)o_GetDeviceQueue,hkvkGetDeviceQueue);
    if(o_GetDeviceQueue2)DetourDetach(&(PVOID&)o_GetDeviceQueue2,hkvkGetDeviceQueue2);
    if(o_GetFenceStatus)DetourDetach(&(PVOID&)o_GetFenceStatus,hkvkGetFenceStatus);
    if(o_WaitForFences)DetourDetach(&(PVOID&)o_WaitForFences,hkvkWaitForFences);
    if(o_ResetFences)DetourDetach(&(PVOID&)o_ResetFences,hkvkResetFences);
    if(o_DestroyFence)DetourDetach(&(PVOID&)o_DestroyFence,hkvkDestroyFence);
    if (o_QueuePresentKHR != nullptr)
        DetourDetach(&(PVOID&) o_QueuePresentKHR, hkvkQueuePresentKHR);

    if (o_CreateSwapchainKHR != nullptr)
        DetourDetach(&(PVOID&) o_CreateSwapchainKHR, hkvkCreateSwapchainKHR);
    if (o_AcquireNextImageKHR != nullptr)
        DetourDetach(&(PVOID&) o_AcquireNextImageKHR, hkvkAcquireNextImageKHR);
    if (o_AcquireNextImage2KHR != nullptr)
        DetourDetach(&(PVOID&) o_AcquireNextImage2KHR, hkvkAcquireNextImage2KHR);

    if (o_vkCreateDevice != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateDevice, hkvkCreateDevice);

    if (o_vkCreateInstance != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateInstance, hkvkCreateInstance);
    if(preparedDestroyInstance)DetourDetach(&(PVOID&)preparedDestroyInstance,PreparedDestroyInstance);

    if (o_vkCreateWin32SurfaceKHR != nullptr)
        DetourDetach(&(PVOID&) o_vkCreateWin32SurfaceKHR, hkvkCreateWin32SurfaceKHR);

    // if (o_vkCmdPipelineBarrier != nullptr)
    //     DetourDetach(&(PVOID&) o_vkCmdPipelineBarrier, hkvkCmdPipelineBarrier);

    auto detourResult = DetourTransactionCommit();
    if (detourResult != NO_ERROR)
    {
        LOG_ERROR("Failed to unhook Vulkan, error code: {:X}", detourResult);
    }
    else
    {
        o_QueuePresentKHR = nullptr;
        o_CreateSwapchainKHR = nullptr;
        o_AcquireNextImageKHR = nullptr;
        o_AcquireNextImage2KHR = nullptr;
        o_vkCreateDevice = nullptr;
        o_vkCreateInstance = nullptr;
        o_vkGetInstanceProcAddr = nullptr;
        o_vkGetDeviceProcAddr = nullptr;
        o_vkCreateWin32SurfaceKHR = nullptr;
        o_GetSwapchainImagesKHR=nullptr;o_GetDeviceQueue=nullptr;o_GetDeviceQueue2=nullptr;o_DestroySurfaceKHR=nullptr;
#define NR_CLEAR(Name) nr_##Name = nullptr;
        NR_RECORDING_FUNCTIONS(NR_CLEAR)
#undef NR_CLEAR
        // o_vkCmdPipelineBarrier = nullptr;
    }
}
