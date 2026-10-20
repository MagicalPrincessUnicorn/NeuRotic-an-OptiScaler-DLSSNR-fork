#include "../../OptiScaler/dlssnr/NativeFgBridge.h"
#include "vendor/FrameInterpolationSwapchainVK.h"
#include "PhysicalSwapchain.h"
#include <map>
#include <memory>
#include <mutex>
#include <condition_variable>
using namespace DlssNr::NativeFg;
namespace NrFgSdk {void (*enter)(bool)=nullptr;void (*leave)(bool)=nullptr;}
namespace {
struct Owner {FrameInterpolationSwapChainVK* swap=nullptr;std::unique_ptr<PhysicalSwapchain> physical;BridgeFrame frame;bool retired=false,busy=false,disposed=false,tearingDown=false;unsigned acquireUsers=0;BridgeProgress progress;unsigned width=0,height=0;bool enabled=false;VkDevice device{};};
std::recursive_mutex mutex;std::condition_variable_any completed;std::map<VkSwapchainKHR,std::shared_ptr<Owner>> owners;
PFN_vkGetDeviceProcAddr deviceDispatch=nullptr;
bool Idle(Owner& o){return !o.busy&&!o.acquireUsers&&(o.physical?o.physical->Drain():(o.retired||o.swap->waitForPresents()));}
bool Dispose(Owner& o){return !o.busy&&!o.acquireUsers&&(o.physical?o.physical->Destroy():(o.retired||o.swap->destroySwapchain(o.device,nullptr)));}
FfxErrorCode Record(const FfxFrameGenerationDispatchDescription* desc,void* context){
    const auto& o=*static_cast<Owner*>(context);const auto& f=o.frame;
    if(!f.generated||!f.copy||!desc||desc->frameID!=f.frame||desc->numInterpolatedFrames!=1)return FFX_ERROR_INVALID_ARGUMENT;
    const auto& out=desc->outputs[0];
    return f.copy(f.context,static_cast<VkCommandBuffer>(desc->commandList),static_cast<VkImage>(out.resource),out.description.width,out.description.height,f.frame)==VK_SUCCESS?FFX_OK:FFX_ERROR_BACKEND_API_ERROR;
}
}
extern "C" __declspec(dllexport) bool NrFgInitialize(const BridgeCallbacks* callbacks){
    if(!callbacks||callbacks->version!=BridgeVersion||!callbacks->enter||!callbacks->leave)return false;
    NrFgSdk::enter=callbacks->enter;NrFgSdk::leave=callbacks->leave;deviceDispatch=callbacks->deviceDispatch?callbacks->deviceDispatch:vkGetDeviceProcAddr;return true;
}
extern "C" __declspec(dllexport) VkResult NrFgCreate(const BridgeCreate* input,VkSwapchainKHR* output){
    if(NrFgSdk::failure.load(std::memory_order_relaxed)!=VK_SUCCESS)return VK_ERROR_DEVICE_LOST;
    if(!input||!output||input->version!=BridgeVersion||!input->device||!input->physical||!input->queue||SwapchainChainRefusal(input->info.pNext,input->physicalMode)||input->info.flags||input->info.imageArrayLayers!=1)return VK_ERROR_INITIALIZATION_FAILED;
    std::lock_guard lock(mutex);Owner* old=nullptr;
    for(auto& [key,o]:owners)if(!o->retired){if(key!=input->info.oldSwapchain)return VK_ERROR_INITIALIZATION_FAILED;old=o.get();}
    // The old physical swapchain remains owned until its application Destroy.
    // Pending WSI retirement must not prevent a new swapchain from making progress.
    if(old&&(old->physical?(old->busy||old->acquireUsers||!old->physical->IdleGpu()):!Idle(*old)))return VK_ERROR_DEVICE_LOST;
    auto ci=input->info;if(old)ci.oldSwapchain=old->physical?old->physical->Handle():old->swap->physicalSwapchain();
    if(input->physicalMode){
        NrFgSdk::Guard internal(false);
        auto o=std::make_shared<Owner>();o->device=input->device;o->width=ci.imageExtent.width;o->height=ci.imageExtent.height;o->physical=std::make_unique<PhysicalSwapchain>();
        auto physicalInput=*input;physicalInput.info=ci;
        const auto result=o->physical->Initialize(physicalInput,deviceDispatch,NrFgSdk::enter,NrFgSdk::leave);
        // The native create retires oldSwapchain even when it returns failure.
        // Private allocation failures before that call leave the old owner active.
        if(old&&old->physical&&o->physical->CreationAttempted())old->retired=true;
        if(result!=VK_SUCCESS){if(!o->physical->Destroy()){owners.emplace(o->physical->Handle(),o);NrFgSdk::failure.store(VK_ERROR_DEVICE_LOST);return VK_ERROR_DEVICE_LOST;}return result;}
        // Physical oldSwapchain belongs to the game until its own Destroy call.
        // Our drain proves only our work, not outstanding game recordings.
        if(old){if(!old->physical&&!Dispose(*old)){owners.emplace(o->physical->Handle(),o);NrFgSdk::failure.store(VK_ERROR_DEVICE_LOST);return VK_ERROR_DEVICE_LOST;}old->retired=true;}
        *output=o->physical->Handle();owners.emplace(*output,std::move(o));return VK_SUCCESS;
    }
    VkFrameInterpolationInfoFFX info{};info.device=input->device;info.physicalDevice=input->physical;
    info.gameQueue={input->queue,input->family,nullptr};
    info.asyncComputeQueue={input->asyncQueue,input->family,nullptr};
    info.presentQueue={input->presentQueue,input->family,nullptr};
    info.imageAcquireQueue={input->acquireQueue,input->family,nullptr};
    info.compositionMode=VK_COMPOSITION_MODE_PRESENT_QUEUE_FFX;
    auto o=std::make_shared<Owner>();o->device=input->device;o->width=ci.imageExtent.width;o->height=ci.imageExtent.height;o->swap=new FrameInterpolationSwapChainVK();
    const auto result=o->swap->init(&ci,&info);
    if(result!=VK_SUCCESS){if(o->swap->destroySwapchain(input->device,nullptr)){delete o->swap;return result;}NrFgSdk::failure.store(VK_ERROR_DEVICE_LOST);return VK_ERROR_DEVICE_LOST;}
    if(old){if(!old->physical&&!Dispose(*old)){NrFgSdk::failure.store(VK_ERROR_DEVICE_LOST);return VK_ERROR_DEVICE_LOST;}old->retired=true;}
    *output=reinterpret_cast<VkSwapchainKHR>(o->swap);owners.emplace(*output,std::move(o));return VK_SUCCESS;
}
extern "C" __declspec(dllexport) VkResult NrFgGetImages(VkDevice d,VkSwapchainKHR s,unsigned* count,VkImage* images){
    std::unique_lock lock(mutex);auto it=owners.find(s);if(it==owners.end())return VK_ERROR_INITIALIZATION_FAILED;
    const auto owner=it->second;while(owner->busy)completed.wait(lock);if(owner->disposed||owner->tearingDown)return VK_ERROR_INITIALIZATION_FAILED;
    if(owner->retired&&!owner->physical)return VK_ERROR_OUT_OF_DATE_KHR;
    if(owner->physical){NrFgSdk::Guard internal(false);return d==owner->device?owner->physical->Images(count,images):VK_ERROR_INITIALIZATION_FAILED;}
    return owner->swap->getSwapchainImages(d,count,images);
}
extern "C" __declspec(dllexport) VkResult NrFgAcquire(VkDevice d,VkSwapchainKHR s,std::uint64_t timeout,VkSemaphore semaphore,VkFence fence,unsigned* index){
    std::unique_lock lock(mutex);auto it=owners.find(s);if(it==owners.end()||it->second->tearingDown||it->second->retired)return VK_ERROR_OUT_OF_DATE_KHR;
    const auto owner=it->second;
    if(!owner->physical){while(owner->busy)completed.wait(lock);if(owner->retired)return VK_ERROR_OUT_OF_DATE_KHR;}
    if(owner->physical){
        if(d!=owner->device)return VK_ERROR_INITIALIZATION_FAILED;
        // A blocking driver Acquire must leave presentation free to release
        // images. Retain the owner and forbid destruction until it returns.
        ++owner->acquireUsers;lock.unlock();NrFgSdk::Guard internal(false);
        const auto r=owner->physical->Acquire(timeout,semaphore,fence,index);
        lock.lock();--owner->acquireUsers;completed.notify_all();return r;
    }
    return owner->swap->acquireNextImage(d,s,timeout,semaphore,fence,index);
}
extern "C" __declspec(dllexport) bool NrFgConfigure(VkSwapchainKHR s,const BridgeFrame* frame){
    if(NrFgSdk::failure.load(std::memory_order_relaxed)!=VK_SUCCESS)return false;
    std::lock_guard lock(mutex);auto it=owners.find(s);if(it==owners.end()||it->second->tearingDown||(it->second->retired&&!it->second->physical)||!frame||!frame->frame)return false;
    auto& o=*it->second;
    if(o.physical){if(o.busy)return false;o.frame=*frame;if(o.retired)o.frame.generated=false;return !o.physical->Unsafe();}
    if(o.enabled!=frame->generated&&!o.swap->waitForPresents())return false;
    o.enabled=frame->generated;o.frame=*frame;FfxFrameGenerationConfig config{};config.swapChain=reinterpret_cast<FfxSwapchain>(s);config.frameID=frame->frame;
    config.interpolationRect={0,0,static_cast<int>(o.width),static_cast<int>(o.height)};
    config.frameGenerationEnabled=frame->generated;config.allowAsyncWorkloads=false;
    config.frameGenerationCallback=frame->generated?Record:nullptr;config.frameGenerationCallbackContext=&o;
    o.swap->setFrameGenerationConfig(&config);return true;
}
extern "C" __declspec(dllexport) VkResult NrFgPresent(VkQueue queue,const VkPresentInfoKHR* info){
    if(!info||info->swapchainCount!=1||!info->pSwapchains||!info->pImageIndices||info->pNext)return VK_ERROR_INITIALIZATION_FAILED;
    std::unique_lock lock(mutex);auto it=owners.find(info->pSwapchains[0]);if(it==owners.end()||it->second->tearingDown||(it->second->retired&&!it->second->physical))return VK_ERROR_OUT_OF_DATE_KHR;
    if(it->second->physical){
        const auto o=it->second;if(o->busy)return VK_ERROR_DEVICE_LOST;o->busy=true;const auto frame=o->frame;lock.unlock();
        NrFgSdk::Guard internal(false);VkResult result=VK_ERROR_DEVICE_LOST;
        try{result=o->physical->Present(queue,*info,frame);}catch(...){o->physical->Poison();}
        lock.lock();const auto& p=*o->physical;o->progress={p.generatedSubmitted,p.generatedRetired,p.realRetired,p.skipped};
        o->progress.composed=p.composed;o->progress.pendingPresents=p.PendingPresents();
        o->progress.lastAcquire=p.lastAcquire;o->progress.lastSubmit=p.lastSubmit;o->progress.lastPresent=p.lastPresent;
        o->progress.lastWait=p.lastWait;o->progress.waitKind=p.waitKind;
        o->busy=false;completed.notify_all();return result;
    }
    return it->second->swap->queuePresent(queue,info);
}
extern "C" __declspec(dllexport) bool NrFgDrain(VkSwapchainKHR s){std::lock_guard lock(mutex);NrFgSdk::Guard internal(false);auto it=owners.find(s);return NrFgSdk::failure.load(std::memory_order_relaxed)==VK_SUCCESS&&it!=owners.end()&&Idle(*it->second);}
extern "C" __declspec(dllexport) bool NrFgBindQueue(VkDevice d,VkSwapchainKHR s,VkQueue q,unsigned family){
    if(NrFgSdk::failure.load(std::memory_order_relaxed)!=VK_SUCCESS)return false;
    std::lock_guard lock(mutex);auto it=owners.find(s);
    if(it!=owners.end()&&!it->second->tearingDown&&it->second->physical){NrFgSdk::Guard internal(false);return !it->second->busy&&it->second->device==d&&it->second->physical->Bind(q,family);}
    return it!=owners.end()&&!it->second->retired&&it->second->device==d&&it->second->swap->bindGameQueue(q,family);
}
extern "C" __declspec(dllexport) bool NrFgDestroy(VkDevice d,VkSwapchainKHR s){std::lock_guard lock(mutex);auto it=owners.find(s);if(it==owners.end())return true;
    NrFgSdk::Guard internal(false);
    if(it->second->device!=d||NrFgSdk::failure.load()!=VK_SUCCESS)return false;
    if(it->second->physical)it->second->retired=true;
    if(!Dispose(*it->second))return false;
    it->second->retired=true;it->second->disposed=true;delete it->second->swap;owners.erase(it);return true;}
extern "C" __declspec(dllexport) bool NrFgTeardownDevice(VkDevice d){
    std::vector<std::shared_ptr<Owner>> selected;
    {
        std::unique_lock lock(mutex);
        for(auto& [s,o]:owners) if(o->device==d) {o->retired=true;o->tearingDown=true;selected.push_back(o);}
        for(auto& o:selected) while(o->busy||o->acquireUsers) completed.wait(lock);
    }
    // The metadata lock and queue gate must not be held while joining callbacks.
    for(auto& o:selected) if(o->swap&&!o->swap->stopSchedulerForDeviceDestroy())return false;
    if(selected.empty())return true;
    NrFgSdk::Guard internal(false);
    const auto idle=reinterpret_cast<PFN_vkDeviceWaitIdle>(deviceDispatch(d,"vkDeviceWaitIdle"));
    if(!idle)return false;
    // Call the actual API, bypassing the SDK's sticky synthetic failure. A
    // timeout/error is not device loss; only this native result grants cleanup.
    const auto result=idle(d);
    if(result!=VK_SUCCESS && result!=VK_ERROR_DEVICE_LOST)return false;
    std::lock_guard lock(mutex);
    for(auto& o:selected) {
        if(o->physical) {if(!o->physical->Destroy(true))return false;}
        else if(o->swap&&!o->swap->destroySwapchain(d,nullptr,true))return false;
        o->disposed=true;delete o->swap;o->swap=nullptr;
    }
    std::erase_if(owners,[&](const auto& item){return item.second->device==d;});
    // Failure belonged to this stopped SDK generation. Never clear a live peer.
    if(owners.empty())NrFgSdk::failure.store(VK_SUCCESS,std::memory_order_release);
    return true;
}
extern "C" __declspec(dllexport) bool NrFgQueryProgress(VkSwapchainKHR s,BridgeProgress* p){
    if(!p)return false;std::lock_guard lock(mutex);auto it=owners.find(s);if(it==owners.end()||!it->second->physical)return false;
    *p=it->second->progress;p->retired=it->second->retired;return true;
}

// The presentation-only component needs these three lightweight VK conversions,
// not the shader backend or a second FSR algorithm implementation.
FfxCommandList ffxGetCommandListVK(VkCommandBuffer cb){return reinterpret_cast<FfxCommandList>(cb);}
FfxResource ffxGetResourceVK(void* image,FfxResourceDescription desc,const wchar_t*,FfxResourceStates state){FfxResource r{};r.resource=image;r.description=desc;r.state=state;return r;}
FfxSurfaceFormat ffxGetSurfaceFormatVK(VkFormat fmt){switch(fmt){
    case VK_FORMAT_R8G8B8A8_UNORM:return FFX_SURFACE_FORMAT_R8G8B8A8_UNORM;
    case VK_FORMAT_R8G8B8A8_SRGB:return FFX_SURFACE_FORMAT_R8G8B8A8_SRGB;
    case VK_FORMAT_B8G8R8A8_UNORM:return FFX_SURFACE_FORMAT_B8G8R8A8_UNORM;
    case VK_FORMAT_B8G8R8A8_SRGB:return FFX_SURFACE_FORMAT_B8G8R8A8_SRGB;
    default:return FFX_SURFACE_FORMAT_UNKNOWN;}}
VkFormat ffxGetVkFormatFromSurfaceFormat(FfxSurfaceFormat fmt){switch(fmt){
    case FFX_SURFACE_FORMAT_R8G8B8A8_UNORM:return VK_FORMAT_R8G8B8A8_UNORM;
    case FFX_SURFACE_FORMAT_R8G8B8A8_SRGB:return VK_FORMAT_R8G8B8A8_SRGB;
    case FFX_SURFACE_FORMAT_B8G8R8A8_UNORM:return VK_FORMAT_B8G8R8A8_UNORM;
    case FFX_SURFACE_FORMAT_B8G8R8A8_SRGB:return VK_FORMAT_B8G8R8A8_SRGB;
    default:return VK_FORMAT_UNDEFINED;}}
VkAccessFlags getVKAccessFlagsFromResourceState2(FfxResourceStates);
VkImageLayout getVKImageLayoutFromResourceState2(FfxResourceStates);
VkAccessFlags getVKAccessFlagsFromResourceState(FfxResourceStates s){return getVKAccessFlagsFromResourceState2(s);}
VkPipelineStageFlags getVKPipelineStageFlagsFromResourceState(FfxResourceStates){return VK_PIPELINE_STAGE_ALL_COMMANDS_BIT;}
VkImageLayout getVKImageLayoutFromResourceState(FfxResourceStates s){return getVKImageLayoutFromResourceState2(s);}
