#include "NativeFgVulkan.h"
#include "NativeGuideRoute.h"
#include "NativeFgLayouts.h"
#include <filesystem>
#include <map>
#include <set>
#include <vector>
namespace DlssNr::NativeFg {
namespace {
thread_local unsigned internal=0;
std::recursive_mutex mutex;
struct PrivateQueues{unsigned family;VkQueue asyncQueue{},present{},acquire{};};
std::map<VkDevice,PrivateQueues> privateQueues;
std::set<VkDevice> maintenanceDevices;
std::set<VkPhysicalDevice> maintenancePhysical;
std::set<VkDevice> failedDevices;
BridgeApi api;HMODULE module=nullptr;bool tried=false;
struct Entry {VkDevice device{};VkQueue queue{};unsigned family=UINT32_MAX;std::uint64_t frame=0,real=0,generated=0;BridgeFrame pending;bool ready=false,retired=false,bound=false,physical=false,destroyRequested=false;};
std::map<VkSwapchainKHR,Entry> entries;
uint64_t presentationEpoch=1;
std::string reason="Standalone FSR frame generation is off";
void CollectDestroyed(){
    for(auto it=entries.begin();it!=entries.end();){
        if(it->second.destroyRequested&&api.destroy(it->second.device,it->first)){
            Layouts::Forget(it->first);it=entries.erase(it);++presentationEpoch;
        }else ++it;
    }
}
void Enter(bool queue){if(queue)QueueMutex().lock();++internal;}
void Leave(bool queue){if(internal)--internal;if(queue)QueueMutex().unlock();}
bool Load(){
    if(api.create)return true;if(tried)return false;tried=true;
    HMODULE host=nullptr;wchar_t path[32768]{};
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&Load),&host)||!GetModuleFileNameW(host,path,32768)){reason="Standalone FG component path unavailable";return false;}
    const auto dir=std::filesystem::path(path).parent_path();
    for(const auto& candidate:{dir/L"NeuRotic.Fsr3.Vulkan.dll",dir/L"OptiScaler"/L"NeuRotic.Fsr3.Vulkan.dll"}){
        module=LoadLibraryExW(candidate.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);if(module)break;}
    if(!module){reason="NeuRotic.Fsr3.Vulkan.dll unavailable; original swapchain retained";return false;}
#define IMPORT(field,name) api.field=reinterpret_cast<decltype(api.field)>(GetProcAddress(module,name))
    IMPORT(init,"NrFgInitialize");IMPORT(create,"NrFgCreate");IMPORT(images,"NrFgGetImages");IMPORT(acquire,"NrFgAcquire");
    IMPORT(configure,"NrFgConfigure");IMPORT(present,"NrFgPresent");IMPORT(drain,"NrFgDrain");IMPORT(destroy,"NrFgDestroy");
    IMPORT(bind,"NrFgBindQueue");
    IMPORT(query,"NrFgQueryProgress");
    IMPORT(teardown,"NrFgTeardownDevice");
#undef IMPORT
    const BridgeCallbacks callbacks{BridgeVersion,Enter,Leave};
    if(!api.init||!api.create||!api.images||!api.acquire||!api.configure||!api.present||!api.drain||!api.destroy||!api.bind||!api.query||!api.teardown||!api.init(&callbacks)){
        api={};reason="Standalone Vulkan FG component ABI refused";return false;}
    return true;
}
VkResult VKAPI_CALL Present(VkQueue queue,const VkPresentInfoKHR* info){
    if(!info||info->swapchainCount!=1||!info->pSwapchains||!info->pImageIndices||info->pNext)return VK_ERROR_FEATURE_NOT_PRESENT;
    bool generated=false;
    {std::lock_guard lock(mutex);const auto it=entries.find(info->pSwapchains[0]);
     if(it==entries.end()||!it->second.bound||it->second.queue!=queue)return VK_ERROR_INITIALIZATION_FAILED;
     if(!it->second.ready)return VK_ERROR_DEVICE_LOST;
     generated=it->second.pending.generated;}
    // No metadata lock while the SDK records the shared-image callback.
    const auto result=api.present(queue,info);
    {std::lock_guard lock(mutex);const auto it=entries.find(info->pSwapchains[0]);
     if(it!=entries.end()&&(result==VK_SUCCESS||result==VK_SUBOPTIMAL_KHR)){++it->second.real;if(generated)++it->second.generated;}
     else reason="Standalone Vulkan FG presentation failed: "+std::to_string(result);}
    return result;
}
}
bool Internal(){return internal!=0;}
void InstanceMaintenance(VkPhysicalDevice p,bool enabled){std::lock_guard lock(mutex);if(enabled)maintenancePhysical.insert(p);else maintenancePhysical.erase(p);}
bool SurfaceMaintenance(VkPhysicalDevice p){std::lock_guard lock(mutex);return maintenancePhysical.contains(p);}
void DeviceMaintenance(VkDevice d,bool enabled){std::lock_guard lock(mutex);if(enabled)maintenanceDevices.insert(d);else maintenanceDevices.erase(d);}
InternalScope::InternalScope(){++internal;}
InternalScope::~InternalScope(){--internal;}
void DeviceCreated(VkDevice device,unsigned family,unsigned first,PFN_vkGetDeviceQueue get){
 if(family==UINT32_MAX||!get)return;PrivateQueues q{family};InternalScope scope;
 get(device,family,first,&q.asyncQueue);get(device,family,first+1,&q.present);get(device,family,first+2,&q.acquire);
 if(q.asyncQueue&&q.present&&q.acquire){std::lock_guard lock(mutex);privateQueues[device]=q;}
}
bool Selected(){
#ifndef _WIN64
    return false;
#else
    static const bool selected=[] {wchar_t value[2]{};return NativeGuides::Selected() &&
        (configured.load(std::memory_order_relaxed)||
         (GetEnvironmentVariableW(L"NEUROTIC_NATIVE_FG",value,2)==1&&value[0]==L'1'));}();return selected;
#endif
}
std::optional<VkResult> Create(VkDevice device,VkPhysicalDevice physical,VkQueue queue,unsigned family,const VkSwapchainCreateInfoKHR* info,
    const VkAllocationCallbacks* allocator,VkSwapchainKHR* out,bool qualified,const char* qualificationRefusal){
    std::lock_guard lock(mutex);
    CollectDestroyed();
    if(failedDevices.contains(device))return VK_ERROR_DEVICE_LOST;
    const auto refuse=[&](std::string detail)->std::optional<VkResult>{reason="Standalone FG unavailable: "+detail+"; NR retained";return {};};
    if(!qualified)return refuse(qualificationRefusal?qualificationRefusal:"device admission was not established");
    if(!info||!out||!device||!physical||!queue)return refuse("device, queue or swapchain creation arguments are missing");
    if(allocator)return refuse("custom swapchain allocation callbacks are unsupported");
    const bool physicalMode=maintenanceDevices.contains(device);
    if(const auto why=SwapchainChainRefusal(info->pNext,physicalMode))return refuse(why);
    if(info->flags)return refuse("swapchain creation flags="+std::to_string(info->flags)+" are unsupported");
    if(info->imageArrayLayers!=1)return refuse("swapchain layers="+std::to_string(info->imageArrayLayers)+"; one layer required");
    if(info->compositeAlpha!=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR)return refuse("swapchain alpha mode="+std::to_string(info->compositeAlpha)+"; opaque required");
    if(!info->minImageCount||info->minImageCount>6)return refuse("requested swapchain images="+std::to_string(info->minImageCount)+"; supported range is 1 to 6");
    if(info->imageSharingMode!=VK_SHARING_MODE_EXCLUSIVE)return refuse("concurrent swapchain queue sharing is unsupported");
    if(info->imageColorSpace!=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR)return refuse("swapchain color space="+std::to_string(info->imageColorSpace)+"; SDR required");
    if(info->imageFormat!=VK_FORMAT_R8G8B8A8_UNORM&&info->imageFormat!=VK_FORMAT_R8G8B8A8_SRGB&&
       info->imageFormat!=VK_FORMAT_B8G8R8A8_UNORM&&info->imageFormat!=VK_FORMAT_B8G8R8A8_SRGB)return refuse("swapchain format="+std::to_string(info->imageFormat)+"; RGBA8/BGRA8 required");
    if(entries.size()>=8)return refuse("swapchain tracking limit reached");
    if(!Load())return {};
    for(const auto& [s,e]:entries)if(!e.retired&&s!=info->oldSwapchain)return refuse("another active FG swapchain exists");
    if(info->oldSwapchain&&!entries.contains(info->oldSwapchain))return refuse("existing swapchain was created without FG; restart the game with FG enabled");
    const auto queues=privateQueues.find(device);
    if(!physicalMode&&(queues==privateQueues.end()||queues->second.family!=family)){reason="Standalone FG unavailable: presentation maintenance fences unavailable and three private graphics queues were not created; NR retained";return {};}
    BridgeCreate create{BridgeVersion,physical,device,queue,family,*info};create.physicalMode=physicalMode;
    if(!physicalMode){create.asyncQueue=queues->second.asyncQueue;create.presentQueue=queues->second.present;create.acquireQueue=queues->second.acquire;}
    const auto result=api.create(&create,out);
    if(physicalMode&&info->oldSwapchain&&api.query){BridgeProgress old;if(api.query(info->oldSwapchain,&old)&&old.retired)entries.at(info->oldSwapchain).retired=true;}
    if(result!=VK_SUCCESS){reason="Standalone FG swapchain creation failed: "+std::to_string(result);if(result==VK_ERROR_DEVICE_LOST){failedDevices.insert(device);return result;}if(!info->oldSwapchain)return {};return result;}
    if(info->oldSwapchain)entries.at(info->oldSwapchain).retired=true;
    auto entry=entries.emplace(*out,Entry{device,queue,family}).first;entry->second.physical=physicalMode;++presentationEpoch;
    reason=physicalMode?"Standalone FSR 2x physical presentation selected; no private queues":"Standalone FSR 2x selected; waiting for observed Present queue and NR guides";return result;
}
bool Owns(VkSwapchainKHR swap){std::lock_guard lock(mutex);const auto it=entries.find(swap);return it!=entries.end()&&!it->second.destroyRequested;}
bool BindPresentQueue(VkDevice device,VkSwapchainKHR swap,VkQueue queue,unsigned family){
    std::unique_lock lock(mutex);if(failedDevices.contains(device))return false;
    auto it=entries.find(swap);auto queues=privateQueues.find(device);
    if(it==entries.end()||(it->second.retired&&!it->second.physical)||it->second.device!=device||!queue||it->second.family!=family||
       (!it->second.physical&&(queues==privateQueues.end()||queues->second.family!=family||queue==queues->second.asyncQueue||queue==queues->second.present||queue==queues->second.acquire))){
        reason="Standalone FG Present queue device/family/private ownership refused";return false;}
    auto& e=it->second;if(e.bound&&e.queue==queue)return true;
    const auto bind=api.bind;lock.unlock();const bool bound=bind&&bind(device,swap,queue,family);lock.lock();
    it=entries.find(swap);
    if(!bound||failedDevices.contains(device)||it==entries.end()||it->second.device!=device||(it->second.retired&&!it->second.physical)){reason="Standalone FG Present queue binding/drain failed; owner retained";failedDevices.insert(device);return false;}
    it->second.queue=queue;it->second.bound=true;it->second.ready=false;return true;
}
std::optional<VkResult> Images(VkDevice d,VkSwapchainKHR s,unsigned* c,VkImage* images){std::unique_lock lock(mutex);
    const auto it=entries.find(s);if(it==entries.end())return {};if(it->second.device!=d)return VK_ERROR_INITIALIZATION_FAILED;
    const bool physical=it->second.physical;const auto fn=api.images;lock.unlock();const auto result=fn(d,s,c,images);
    if(result==VK_SUCCESS&&images&&c&&!physical)Layouts::Register(d,s,{images,*c});return result;}
std::optional<VkResult> Acquire(VkDevice d,VkSwapchainKHR s,std::uint64_t t,VkSemaphore sem,VkFence fence,unsigned* index){std::unique_lock lock(mutex);
    const auto it=entries.find(s);if(it==entries.end())return {};if(it->second.device!=d)return VK_ERROR_INITIALIZATION_FAILED;
    const auto fn=api.acquire;lock.unlock();return fn(d,s,t,sem,fence,index);}
bool Begin(VkSwapchainKHR s){std::lock_guard lock(mutex);CollectDestroyed();auto it=entries.find(s);if(it==entries.end()||it->second.destroyRequested||it->second.frame>=UINT64_MAX-1)return false;
    auto& e=it->second;if(!e.bound)return false;e.ready=false;e.pending={++e.frame,false,nullptr,nullptr};return true;}
std::uint64_t Frame(VkSwapchainKHR s){std::lock_guard lock(mutex);auto it=entries.find(s);return it==entries.end()?0:it->second.frame;}
bool Publish(VkSwapchainKHR s,CopyFn copy,void* context){std::lock_guard lock(mutex);auto it=entries.find(s);if(it==entries.end()||it->second.retired||!it->second.pending.frame||!copy)return false;
    it->second.pending.generated=true;it->second.pending.copy=copy;it->second.pending.context=context;return true;}
bool Finish(VkSwapchainKHR s){std::lock_guard lock(mutex);auto it=entries.find(s);if(it==entries.end())return false;
    const bool result=api.configure(s,&it->second.pending);it->second.ready=result;reason=result?(it->second.pending.generated?
        (it->second.physical?"Standalone FSR 2x: generated output ready; physical presentation pending":"Standalone FSR 2x: generated output ready; SDK presentation scheduled"):
        "Standalone FSR: real frame only (NR inactive, missing guides or history reset)"):"Standalone FSR swapchain configuration failed";return result;}
bool Composition(VkSwapchainKHR s,CopyFn compose,void* context){
    std::lock_guard lock(mutex);auto it=entries.find(s);
    if(it==entries.end()||!it->second.physical||!it->second.ready||it->second.destroyRequested)return false;
    auto frame=it->second.pending;frame.compose=compose;frame.composeContext=context;
    if(!api.configure(s,&frame))return false;it->second.pending=frame;return true;
}
PFN_vkQueuePresentKHR ResolvePresent(const VkPresentInfoKHR* info,PFN_vkQueuePresentKHR original){
    if(info&&info->pSwapchains)for(unsigned i=0;i<info->swapchainCount;++i)if(Owns(info->pSwapchains[i]))return Present;return original;}
VkResult CallPresent(PFN_vkQueuePresentKHR fn,VkQueue queue,const VkPresentInfoKHR* info){
    return fn==Present?fn(queue,info):QueueCall(fn,queue,info);
}
bool Drain(VkSwapchainKHR s){std::lock_guard lock(mutex);if(!entries.contains(s))return true;return api.drain(s);}
bool DrainQueue(VkQueue q,bool includePhysical){std::lock_guard lock(mutex);for(const auto& [s,e]:entries)if(e.queue==q&&(includePhysical||!e.physical)&&!api.drain(s))return false;return true;}
bool DrainDevice(VkDevice d,bool includePhysical){std::lock_guard lock(mutex);if(failedDevices.contains(d))return false;for(const auto& [s,e]:entries)if(e.device==d&&(includePhysical||!e.physical)&&!api.drain(s)){reason="Standalone FG scheduler drain unconfirmed; owner retained";return false;}return true;}
std::optional<bool> Destroy(VkDevice d,VkSwapchainKHR s){std::lock_guard lock(mutex);auto it=entries.find(s);if(it==entries.end())return {};
    if(it->second.device!=d)return false;
    if(it->second.physical){
        it->second.destroyRequested=true;it->second.retired=true;
        if(!api.destroy(d,s)){reason="Standalone FG destruction deferred until presentation fences retire";return true;}
    }else if(!api.drain(s)||!api.destroy(d,s)){reason="Standalone FG destruction deferred: scheduler drain unconfirmed";return false;}
    Layouts::Forget(s);entries.erase(it);++presentationEpoch;return true;}
bool DestroyDevice(VkDevice d){
    std::unique_lock lock(mutex);
    bool owned=false;
    for(auto& [s,e]:entries)if(e.device==d){e.destroyRequested=true;e.retired=true;e.ready=false;owned=true;}
    const auto teardown=api.teardown;
    // Stop admission, then join CPU callbacks without holding their metadata lock.
    failedDevices.insert(d);lock.unlock();
    const bool retired=!owned || (teardown&&teardown(d));
    lock.lock();
    if(!retired){reason="Standalone FG native completion unknown; owner retained";return false;}
    std::erase_if(entries,[&](const auto& item){if(item.second.device!=d)return false;Layouts::Forget(item.first);return true;});
    failedDevices.erase(d);privateQueues.erase(d);maintenanceDevices.erase(d);Layouts::ForgetDevice(d);return true;
}
PresentationProgress Progress(){std::unique_lock lock(mutex);PresentationProgress result;std::vector<VkSwapchainKHR> physicalOwners;
    result.identity=presentationEpoch;result.reason=reason;
    for(const auto& [s,e]:entries){if(e.retired||e.destroyRequested)continue;result.real+=e.real;result.requested+=e.generated;if(e.ready&&e.bound)++result.owners;if(e.physical)physicalOwners.push_back(s);}
    const auto query=api.query;lock.unlock();
    if(query)for(auto s:physicalOwners){BridgeProgress p;if(query(s,&p)){result.physicalAvailable=true;auto& total=result.physical;total.generatedSubmitted+=p.generatedSubmitted;total.generatedRetired+=p.generatedRetired;total.realRetired+=p.realRetired;total.skipped+=p.skipped;total.composed+=p.composed;total.pendingPresents+=p.pendingPresents;total.lastAcquire=p.lastAcquire;total.lastSubmit=p.lastSubmit;total.lastPresent=p.lastPresent;total.lastWait=p.lastWait;total.waitKind=p.waitKind;}}
    return result;
}
std::string Status(){const auto snapshot=Progress();const auto& total=snapshot.physical;const bool physical=snapshot.physicalAvailable;
    const auto detail=physical?"\nPhysical FSR: generated copies submitted "+std::to_string(total.generatedSubmitted)+
        " | Generated presents retired "+std::to_string(total.generatedRetired)+" | Real presents retired "+std::to_string(total.realRetired)+" | Extra image unavailable/recording skipped "+std::to_string(total.skipped)+
        "\nGenerated UI compositions "+std::to_string(total.composed)+" | Pending presentation retirements "+std::to_string(total.pendingPresents)+
        " | Native results: acquire="+std::to_string(total.lastAcquire)+" submit="+std::to_string(total.lastSubmit)+" present="+std::to_string(total.lastPresent)+" wait="+std::to_string(total.lastWait)+" kind="+std::to_string(total.waitKind):"";
    return snapshot.reason+"\nReal frames enqueued "+std::to_string(snapshot.real)+" | Generated frames requested "+std::to_string(snapshot.requested)+detail+
        "\nMotion is estimated; camera is policy; HUD included. Counters do not verify display.";}
#ifdef NRPG_CPU_MAINLINE_TESTING
void InjectBridge(BridgeApi value){std::lock_guard lock(mutex);api=value;const BridgeCallbacks callbacks{BridgeVersion,Enter,Leave};api.init(&callbacks);}
#endif
}
