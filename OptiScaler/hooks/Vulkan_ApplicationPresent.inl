// Included by Vulkan_Hooks.cpp: these originals belong to the authenticated
// game's interposer, never the physical loader dispatch.
namespace {
struct ApplicationWsi {
    HMODULE module=nullptr;
    PFN_vkCreateSwapchainKHR create=nullptr;
    PFN_vkGetSwapchainImagesKHR images=nullptr;
    PFN_vkAcquireNextImageKHR acquire=nullptr;
    PFN_vkQueuePresentKHR present=nullptr;
    PFN_vkDestroySwapchainKHR destroy=nullptr;
} appWsi;
std::mutex appWsiInstallMutex;
thread_local bool appPresentForwarding=false;

static std::vector<VkImage> AppImages(VkDevice device,VkSwapchainKHR swap)
{
    for(int attempt=0;attempt<3;++attempt){
        uint32_t count=0;
        if(appWsi.images(device,swap,&count,nullptr)!=VK_SUCCESS||!count||count>256)return {};
        std::vector<VkImage> images(count);const auto capacity=count;
        const auto result=appWsi.images(device,swap,&count,images.data());
        if(result==VK_SUCCESS&&count&&count<=capacity){images.resize(count);return images;}
        if(result!=VK_INCOMPLETE)return {};
    }
    return {};
}
static VkResult VKAPI_CALL AppCreate(VkDevice device,const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator,VkSwapchainKHR* swap)
{
    if(!info)return appWsi.create(device,info,allocator,swap);
    auto& registry=DlssNr::GetVulkanApplicationPresentRegistry();
    registry.ImportDeviceAndQueues(DlssNr::GetVulkanPresentRegistry(),device);
    const auto physical=registry.PhysicalDevice(device);
    VkImageUsageFlags supported=0;std::vector<uint32_t> families;
    const bool known=DlssNr::IsVkSwapchainUsageChainKnown(info->pNext);
    if(physical&&o_vkGetInstanceProcAddr){
        const auto instance=State::Instance().VulkanInstance;
        auto caps=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilitiesKHR>(o_vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceCapabilitiesKHR"));
        auto caps2=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(o_vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceCapabilities2KHR"));
        supported=DlssNr::QueryVkSwapchainSurfaceUsage(physical,*info,caps,caps2);
        auto support=reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceSupportKHR>(o_vkGetInstanceProcAddr(instance,"vkGetPhysicalDeviceSurfaceSupportKHR"));
        if(support)for(auto family:registry.CreatedQueueFamilies(device)){
            VkBool32 yes=VK_FALSE;
            if(support(physical,family,info->surface,&yes)==VK_SUCCESS&&yes)families.push_back(family);
        }
    }
    const auto settings=TryNrConfigSnapshot(*Config::Instance());
    auto local=*info;
    local.imageUsage=DlssNr::PrepareVkSwapchainUsage(info->imageUsage,supported,known,
        settings&&settings->DlssNrVulkanPrepare.value_or_default()&&registry.PresentPrepared(device)).usage;
    const auto result=appWsi.create(device,&local,allocator,swap);
    if(result==VK_SUCCESS&&swap&&*swap){
        registry.ImportDeviceAndQueues(DlssNr::GetVulkanPresentRegistry(),device);
        auto images=AppImages(device,*swap);
        registry.SwapchainCreated(device,*swap,local,supported,known,std::move(images),std::move(families));
        LOG_INFO("Vulkan application swapchain observed: device={} swapchain={} extent={}x{} usage=0x{:X}",
            (uintptr_t)device,(uintptr_t)*swap,local.imageExtent.width,local.imageExtent.height,local.imageUsage);
    }
    return result;
}
static VkResult VKAPI_CALL AppGetImages(VkDevice device,VkSwapchainKHR swap,uint32_t* count,VkImage* images)
{
    const auto capacity=count?*count:0;
    const auto result=appWsi.images(device,swap,count,images);
    if(result==VK_SUCCESS&&images&&count&&*count<=capacity&&*count<=256)
        DlssNr::GetVulkanApplicationPresentRegistry().SwapchainImages(device,swap,{images,images+*count});
    return result;
}
static VkResult VKAPI_CALL AppAcquire(VkDevice device,VkSwapchainKHR swap,uint64_t timeout,
    VkSemaphore semaphore,VkFence fence,uint32_t* index)
{
    const auto result=appWsi.acquire(device,swap,timeout,semaphore,fence,index);
    if((result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)&&index)
        DlssNr::GetVulkanApplicationPresentRegistry().ImageAcquired(device,swap,*index,semaphore,fence);
    return result;
}
static void VKAPI_CALL AppDestroy(VkDevice device,VkSwapchainKHR swap,const VkAllocationCallbacks* allocator)
{
    DlssNr::GetVulkanApplicationPresentRegistry().SwapchainDestroyed(swap);
    appWsi.destroy(device,swap,allocator);
}
static bool AppImageQualified(const DlssNr::VkObservedPresent& observed)
{
    const auto image=DlssNr::VulkanNrImageFacts().Image(observed.device,observed.image);
    constexpr auto transfers=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    // A provider's proxy is a separately allocated image. Requested swapchain
    // usage cannot stand in for its actual allocation facts.
    return image&&image->chainKnown&&image->format==observed.request.format&&
        image->extent.width==observed.request.extent.width&&image->extent.height==observed.request.extent.height&&
        image->extent.depth==1&&image->layers==1&&image->mips==1&&image->type==VK_IMAGE_TYPE_2D&&
        image->samples==VK_SAMPLE_COUNT_1_BIT&&(image->usage&transfers)==transfers&&
        !(image->flags&(VK_IMAGE_CREATE_PROTECTED_BIT|VK_IMAGE_CREATE_SPARSE_BINDING_BIT|VK_IMAGE_CREATE_ALIAS_BIT))&&
        (image->sharing==VK_SHARING_MODE_EXCLUSIVE||
         (image->sharing==VK_SHARING_MODE_CONCURRENT&&std::find(image->families.begin(),image->families.end(),observed.queueFamily)!=image->families.end()));
}
static VkResult VKAPI_CALL AppPresent(VkQueue queue,const VkPresentInfoKHR* info)
{
    if(appPresentForwarding||!info)return appWsi.present(queue,info);
    struct Scope {Scope(){appPresentForwarding=true;}~Scope(){appPresentForwarding=false;}} scope;
    auto& adapter=DlssNr::VulkanNrStreamlineAdapter();
    // FG-off retains the accepted physical route, including its public marker.
    if(adapter.Activity()!=DlssNr::VkNrFgActivity::On){
        DlssNr::GetVulkanPresentStatus().ApplicationProvider(0);return appWsi.present(queue,info);
    }
    DlssNr::GetVulkanPresentStatus().ApplicationProvider(adapter.Generation());
    using Clock=std::chrono::steady_clock;const auto started=Clock::now();
    auto& registry=DlssNr::GetVulkanApplicationPresentRegistry();
    const auto device=DlssNr::VulkanLoaderWsiDevices().ForQueue(queue).device;
    registry.ImportDeviceAndQueues(DlssNr::GetVulkanPresentRegistry(),device);
    const auto settings=TryNrConfigSnapshot(*Config::Instance());
    const auto runtime=settings?settings->GetDlssNrRuntimeSnapshot():NrConfigState::RuntimeSnapshot{};
    if(settings&&!runtime.enabled)DlssNr::RetireInactiveVulkanPresentGuides(device);
    const bool nrPresentEnabled=runtime.enabled&&!DlssNr::PreparedGuides::OwnsPresentOutput();
    const auto route=settings?settings->DlssNrRoute.value_or_default():0;
    auto& status=DlssNr::GetVulkanPresentStatus();
    const auto call=status.BeginCall(route,std::chrono::duration<double,std::milli>(started.time_since_epoch()).count());
    struct Finish {
        DlssNr::PresentPacing::CallToken call;Clock::time_point start;double adapter=0,original=0;bool failed=true;
        ~Finish(){DlssNr::GetVulkanPresentStatus().FinishCall(call,adapter,std::chrono::duration<double,std::milli>(Clock::now()-start).count(),original,failed);}
    } finish{call,started};
    auto observed=registry.SnapshotForPresent(queue,*info,nrPresentEnabled,route,
        settings?DlssNr::PresentInput::Selected(*settings):DlssNr::PresentInput::Policy::ImageOnly,true);
    if(observed)observed->applicationFacing=true;
    const auto frame=DlssNr::VulkanPublicPresentFrames().Take(adapter.Generation(),GetCurrentThreadId());
    const auto tag=frame&&observed?DlssNr::MakeVkApplicationPresentTag(*frame,*observed):std::nullopt;
    const auto guides=DlssNr::BeginVulkanGuidePresent(queue,tag?&*tag:nullptr);
    auto& executor=DlssNr::GetVulkanPresentExecutor();
    if(observed&&settings){
        DlssNr::PollVkNrCompletions();executor.Maintain(*observed,!nrPresentEnabled||(route!=1&&route!=2));
        DlssNr::RetireInactiveVk(observed->device,nrPresentEnabled,route);
    }
    DlssNr::VkPresentExecution execution{};auto local=*info;
    std::string refusal;
    if(nrPresentEnabled&&(route==1||route==2)){
        if(!observed)refusal="Application Vulkan swapchain/acquisition unavailable";
        else if(!frame||!tag)refusal="Application Present needs a current public frame and viewport";
        else if(!AppImageQualified(*observed))refusal="Application Present proxy image transfer allocation is unqualified";
        else if(State::Instance().activeFgInput!=FGInput::NoFG||State::Instance().activeFgOutput!=FGOutput::NoFG||DlssNr::VulkanNrFfxStatus().providerId)
            refusal="Application Present requires the game's native Streamline frame generator";
        else refusal=adapter.Reason(frame->viewport);
        if(refusal.empty()&&observed&&frame){
            const auto instance=_instance?_instance:State::Instance().VulkanInstance;
            execution=executor.BeforePresent(*observed,instance,*info,std::make_shared<NrConfigSnapshot<Config>>(*settings),guides,{},
                [id=*frame]{return StreamlineHooks::PrepareVulkanFullFrame(id.provider,id.frame,id.viewport);});
            if(execution.handoff.state==DlssNr::VkPresentHandoffState::Accepted)DlssNr::ApplyVkPresentHandoff(local,execution.handoff);
            else if(execution.handoff.state==DlssNr::VkPresentHandoffState::Uncertain){
                ReportVkPresentResult(queue,*info,&*observed,&execution,false,execution.handoff.result,execution.reason.c_str());
                return execution.handoff.result;
            }
        }
    }
    finish.adapter=std::chrono::duration<double,std::milli>(Clock::now()-started).count();
    if(execution.handoff.state==DlssNr::VkPresentHandoffState::Accepted&&!execution.record.preparationOnly)
        status.ExpectGpu(call,execution.record.use,execution.record.requestedPasses,std::chrono::duration<double,std::milli>(Clock::now().time_since_epoch()).count());
    const auto originalStart=Clock::now();
    const auto result=appWsi.present(queue,&local);
    finish.original=std::chrono::duration<double,std::milli>(Clock::now()-originalStart).count();
    finish.failed=result!=VK_SUCCESS&&result!=VK_SUBOPTIMAL_KHR;
    const auto selected=DlssNr::VkWsiSelectedPresentResult(true,result,local);
    if((result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)&&local.pSwapchains&&local.pImageIndices)
        for(uint32_t i=0;i<local.swapchainCount;++i){
            const auto r=local.pResults?local.pResults[i]:result;
            if(r==VK_SUCCESS||r==VK_SUBOPTIMAL_KHR)registry.PresentSucceeded(local.pSwapchains[i],local.pImageIndices[i],queue);
        }
    if(execution.ticket)executor.OriginalPresentReturned(execution.ticket,selected,
        observed?registry.LastPresentSerial(observed->swapchain,observed->request.imageIndex):0);
    if(nrPresentEnabled&&(route==1||route==2))ReportVkPresentResult(queue,*info,observed?&*observed:nullptr,&execution,true,selected,
        refusal.empty()?execution.reason.c_str():refusal.c_str());
    return result;
}
}

void VulkanHooks::HookApplicationInterposer(HMODULE module)
{
    std::lock_guard lock(appWsiInstallMutex);
    if(!module||appWsi.module)return;
    ApplicationWsi candidate;candidate.module=module;
#define APP_RESOLVE(member,name) candidate.member=reinterpret_cast<decltype(candidate.member)>(KernelBaseProxy::GetProcAddress_()(module,name))
    APP_RESOLVE(create,"vkCreateSwapchainKHR");APP_RESOLVE(images,"vkGetSwapchainImagesKHR");
    APP_RESOLVE(acquire,"vkAcquireNextImageKHR");APP_RESOLVE(present,"vkQueuePresentKHR");APP_RESOLVE(destroy,"vkDestroySwapchainKHR");
#undef APP_RESOLVE
    if(!candidate.create||!candidate.images||!candidate.acquire||!candidate.present||!candidate.destroy)return;
    for(auto address:{reinterpret_cast<const void*>(candidate.create),reinterpret_cast<const void*>(candidate.images),
        reinterpret_cast<const void*>(candidate.acquire),reinterpret_cast<const void*>(candidate.present),reinterpret_cast<const void*>(candidate.destroy)}){
        HMODULE owner=nullptr;
        if(!GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCSTR>(address),&owner)||owner!=module)return;
    }
    appWsi=candidate;
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    LONG error=NO_ERROR;
#define APP_ATTACH(member,hook) if(error==NO_ERROR)error=DetourAttach(reinterpret_cast<PVOID*>(&appWsi.member),hook)
    APP_ATTACH(create,AppCreate);APP_ATTACH(images,AppGetImages);APP_ATTACH(acquire,AppAcquire);APP_ATTACH(present,AppPresent);APP_ATTACH(destroy,AppDestroy);
#undef APP_ATTACH
    if(error!=NO_ERROR)DetourTransactionAbort();else error=DetourTransactionCommit();
    if(error!=NO_ERROR){appWsi={};LOG_WARN("Vulkan application interposer WSI hooks failed: {}",error);}
    else LOG_INFO("Vulkan application interposer WSI hooks installed; physical WSI remains separate");
}
void VulkanHooks::UnhookApplicationInterposer()
{
    std::lock_guard lock(appWsiInstallMutex);if(!appWsi.module)return;
    DetourTransactionBegin();DetourUpdateThread(GetCurrentThread());
    LONG error=NO_ERROR;
#define APP_DETACH(member,hook) if(error==NO_ERROR)error=DetourDetach(reinterpret_cast<PVOID*>(&appWsi.member),hook)
    APP_DETACH(create,AppCreate);APP_DETACH(images,AppGetImages);APP_DETACH(acquire,AppAcquire);APP_DETACH(present,AppPresent);APP_DETACH(destroy,AppDestroy);
#undef APP_DETACH
    if(error!=NO_ERROR)DetourTransactionAbort();else error=DetourTransactionCommit();
    if(error==NO_ERROR){appWsi={};DlssNr::GetVulkanPresentStatus().ApplicationProvider(0);}else LOG_WARN("Vulkan application interposer WSI detach failed: {}",error);
}
