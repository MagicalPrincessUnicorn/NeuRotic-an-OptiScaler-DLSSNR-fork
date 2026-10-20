#include "../addons/native-framegen/PhysicalSwapchain.h"
#include "../OptiScaler/dlssnr/NativeFgMaintenance.h"
#include <cassert>
#include <cstring>
#include <map>
#include <cstdio>
#include <future>
#include <condition_variable>
using namespace DlssNr::NativeFg;
template<class T>T H(uintptr_t n){return reinterpret_cast<T>(n);}
static std::map<VkFence,bool> fences;static std::vector<unsigned> order;
static std::map<VkFence,VkSemaphore> pendingWaits;static unsigned semaphoreSerial=2000;
static unsigned serial=30,copies=0,releases=0,submits=0,appWaits=0,destroys=0,compositions=0;
static VkResult acquireResult=VK_SUCCESS,copyResult=VK_SUCCESS,submitResult=VK_SUCCESS,presentResult=VK_SUCCESS;
static bool timeout=false,missing=false,deferPresent=false;static VkQueue lastQueue{};static unsigned gateDepth=0;
static void Enter(bool q){if(q){assert(!gateDepth);++gateDepth;}}
static void Leave(bool q){if(q){assert(gateDepth==1);--gateDepth;}}
static VkResult idleResult=VK_SUCCESS;
static VkResult VKAPI_CALL DeviceIdle(VkDevice){if(idleResult==VK_SUCCESS||idleResult==VK_ERROR_DEVICE_LOST)pendingWaits.clear();return idleResult;}
static VkResult createResult=VK_SUCCESS;static uintptr_t createHandle=20;
static BridgeQuery queryDuringCopy=nullptr;static BridgeDrain drainDuringCopy=nullptr;
static std::mutex acquireMutex;static std::condition_variable acquireCondition;static bool gameAcquiring=false,releaseGameAcquire=false;
static VkResult VKAPI_CALL Create(VkDevice,const VkSwapchainCreateInfoKHR* ci,const VkAllocationCallbacks*,VkSwapchainKHR* s){assert(ci->pNext);if(createResult!=VK_SUCCESS)return createResult;*s=H<VkSwapchainKHR>(createHandle);return VK_SUCCESS;}
static void VKAPI_CALL Destroy(VkDevice,VkSwapchainKHR,const VkAllocationCallbacks*){++destroys;}
static VkResult VKAPI_CALL Images(VkDevice,VkSwapchainKHR,unsigned* c,VkImage* i){if(i){assert(*c>=3);for(unsigned n=0;n<3;++n)i[n]=H<VkImage>(100+n);}*c=3;return VK_SUCCESS;}
static VkResult VKAPI_CALL Acquire(VkDevice,VkSwapchainKHR,uint64_t t,VkSemaphore s,VkFence f,unsigned* i){
 if(t==UINT64_MAX&&s==H<VkSemaphore>(10)){
  std::unique_lock lock(acquireMutex);gameAcquiring=true;acquireCondition.notify_all();
  assert(acquireCondition.wait_for(lock,std::chrono::seconds(1),[]{return releaseGameAcquire;}));gameAcquiring=false;*i=2;return VK_SUCCESS;
 }
 {std::lock_guard lock(acquireMutex);assert(!gameAcquiring);} // driver Acquire host synchronization
 assert(t==0&&!s&&f&&!fences[f]);if(acquireResult==VK_SUCCESS||acquireResult==VK_SUBOPTIMAL_KHR){*i=1;fences[f]=true;}return acquireResult;
}
static VkResult VKAPI_CALL Release(VkDevice,const VkReleaseSwapchainImagesInfoEXT* r){assert(r->imageIndexCount==1&&*r->pImageIndices==1);++releases;return VK_SUCCESS;}
static VkResult VKAPI_CALL Pool(VkDevice,const VkCommandPoolCreateInfo* c,const VkAllocationCallbacks*,VkCommandPool* p){assert(c->queueFamilyIndex==0);*p=H<VkCommandPool>(21);return VK_SUCCESS;}
static void VKAPI_CALL FreePool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){}
static VkResult VKAPI_CALL Allocate(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* c){*c=H<VkCommandBuffer>(22);return VK_SUCCESS;}
static VkResult VKAPI_CALL ResetPool(VkDevice,VkCommandPool,VkCommandPoolResetFlags){return VK_SUCCESS;}
static VkResult VKAPI_CALL Begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
static VkResult VKAPI_CALL End(VkCommandBuffer){return VK_SUCCESS;}
static void VKAPI_CALL Barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned,const VkImageMemoryBarrier*){}
static VkResult VKAPI_CALL Semaphore(VkDevice,const VkSemaphoreCreateInfo*,const VkAllocationCallbacks*,VkSemaphore* s){*s=H<VkSemaphore>(semaphoreSerial++);return VK_SUCCESS;}
static void VKAPI_CALL FreeSemaphore(VkDevice,VkSemaphore s,const VkAllocationCallbacks*){for(const auto& [f,w]:pendingWaits){(void)f;assert(w!=s);}}
static VkResult VKAPI_CALL Fence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* f){*f=H<VkFence>(serial++);fences[*f]=false;return VK_SUCCESS;}
static void VKAPI_CALL FreeFence(VkDevice,VkFence,const VkAllocationCallbacks*){}
static VkResult VKAPI_CALL Reset(VkDevice,unsigned c,const VkFence* f){for(unsigned n=0;n<c;++n)fences[f[n]]=false;return VK_SUCCESS;}
static VkResult VKAPI_CALL Wait(VkDevice,unsigned c,const VkFence* f,VkBool32,uint64_t t){assert(!gateDepth&&t<=5000000000ull);if(timeout)return VK_TIMEOUT;for(unsigned n=0;n<c;++n)if(!fences[f[n]])return VK_TIMEOUT;for(unsigned n=0;n<c;++n)pendingWaits.erase(f[n]);return VK_SUCCESS;}
static VkResult VKAPI_CALL Submit(VkQueue q,unsigned c,const VkSubmitInfo* s,VkFence f){assert(gateDepth==1&&c==1&&s->commandBufferCount==1);for(const auto& [pf,w]:pendingWaits){(void)pf;assert(w!=*s->pSignalSemaphores);}lastQueue=q;++submits;if(s->waitSemaphoreCount){assert(s->waitSemaphoreCount==1&&*s->pWaitSemaphores==H<VkSemaphore>(9));++appWaits;}if(submitResult==VK_SUCCESS)fences[f]=true;return submitResult;}
static VkResult VKAPI_CALL Present(VkQueue q,const VkPresentInfoKHR* p){lastQueue=q;assert(gateDepth==1&&p->swapchainCount==1&&(*p->pSwapchains==H<VkSwapchainKHR>(20)||*p->pSwapchains==H<VkSwapchainKHR>(24)));
 auto* fence=static_cast<const VkSwapchainPresentFenceInfoEXT*>(p->pNext);assert(fence&&fence->sType==VK_STRUCTURE_TYPE_SWAPCHAIN_PRESENT_FENCE_INFO_EXT&&fence->swapchainCount==1&&!fences[*fence->pFences]);
 if(presentResult==VK_ERROR_OUT_OF_HOST_MEMORY||presentResult==VK_ERROR_OUT_OF_DEVICE_MEMORY)return presentResult;
 if(p->waitSemaphoreCount&&*p->pWaitSemaphores==H<VkSemaphore>(9))++appWaits;
 if(p->waitSemaphoreCount&&*p->pWaitSemaphores!=H<VkSemaphore>(9))pendingWaits[*fence->pFences]=*p->pWaitSemaphores;
 order.push_back(*p->pImageIndices);fences[*fence->pFences]=!deferPresent;
 if(*p->pImageIndices==0){std::lock_guard lock(acquireMutex);if(gameAcquiring){releaseGameAcquire=true;acquireCondition.notify_all();}}
 return presentResult;}
static PFN_vkVoidFunction VKAPI_CALL Get(VkDevice,const char* n){
 if(missing&&!std::strcmp(n,"vkReleaseSwapchainImagesEXT"))return nullptr;
#define F(name,fn) if(!std::strcmp(n,"vk" #name))return reinterpret_cast<PFN_vkVoidFunction>(fn)
 F(DeviceWaitIdle,DeviceIdle);F(CreateSwapchainKHR,Create);F(DestroySwapchainKHR,Destroy);F(GetSwapchainImagesKHR,Images);F(AcquireNextImageKHR,Acquire);F(ReleaseSwapchainImagesEXT,Release);F(QueuePresentKHR,Present);
 F(CreateCommandPool,Pool);F(DestroyCommandPool,FreePool);F(AllocateCommandBuffers,Allocate);F(ResetCommandPool,ResetPool);F(BeginCommandBuffer,Begin);F(EndCommandBuffer,End);F(CmdPipelineBarrier,Barrier);F(QueueSubmit,Submit);
 F(CreateSemaphore,Semaphore);F(DestroySemaphore,FreeSemaphore);F(CreateFence,Fence);F(DestroyFence,FreeFence);F(ResetFences,Reset);F(WaitForFences,Wait);
#undef F
 return nullptr;
}
static VkResult Copy(void*,VkCommandBuffer,VkImage i,unsigned w,unsigned h,uint64_t f){assert(!gateDepth&&i==H<VkImage>(101)&&w==128&&h==128&&f==2);++copies;
 if(queryDuringCopy){auto query=std::async(std::launch::async,[]{BridgeProgress p;return queryDuringCopy(H<VkSwapchainKHR>(20),&p);});assert(query.wait_for(std::chrono::seconds(1))==std::future_status::ready&&query.get());assert(!drainDuringCopy(H<VkSwapchainKHR>(20)));}
 return copyResult;
}
static VkResult Compose(void*,VkCommandBuffer,VkImage i,unsigned w,unsigned h,uint64_t f){assert(!gateDepth&&i==H<VkImage>(101)&&w==128&&h==128&&f==2);++compositions;return VK_SUCCESS;}
int main(){
 // Device feature preparation respects dependencies, source memory and opt-out.
 const char* instanceNames[]={"VK_KHR_surface"};VkInstanceCreateInfo ici{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};ici.enabledExtensionCount=1;ici.ppEnabledExtensionNames=instanceNames;
 MaintenanceInstancePreparation ip(ici,{"VK_KHR_get_surface_capabilities2",SurfaceMaintenanceExtension},true);
 assert(HasInstanceExtension(ip.Apply(ici),SurfaceMaintenanceExtension)&&ici.enabledExtensionCount==1);
 VkDeviceCreateInfo dci{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};MaintenancePreparation offscreen(dci,{MaintenanceExtension},true,true);assert(!ObserveMaintenance(offscreen.Apply(dci),VK_SUCCESS));
 const char* deviceNames[]={"VK_KHR_swapchain"};dci.enabledExtensionCount=1;dci.ppEnabledExtensionNames=deviceNames;MaintenancePreparation prep(dci,{MaintenanceExtension},true,true);
 const auto amended=prep.Apply(dci);assert(ObserveMaintenance(amended,VK_SUCCESS)&&!ObserveMaintenance(amended,VK_ERROR_INITIALIZATION_FAILED)&&!dci.pNext);
 VkPhysicalDeviceSwapchainMaintenance1FeaturesEXT disabled{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SWAPCHAIN_MAINTENANCE_1_FEATURES_EXT};dci.pNext=&disabled;
 MaintenancePreparation no(dci,{MaintenanceExtension},true,true);assert(!ObserveMaintenance(no.Apply(dci),VK_SUCCESS)&&!disabled.swapchainMaintenance1);
 disabled.swapchainMaintenance1=VK_TRUE;disabled.pNext=&disabled;assert(!InspectMaintenance(&disabled).complete);disabled.pNext=nullptr;
 BridgeCreate ci;ci.device=H<VkDevice>(1);ci.queue=H<VkQueue>(2);ci.family=0;ci.info.sType=VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
 ci.info.imageExtent={128,128};ci.info.imageUsage=VK_IMAGE_USAGE_TRANSFER_DST_BIT;ci.info.imageArrayLayers=1;
 ci.info.presentMode=VK_PRESENT_MODE_FIFO_KHR;
 VkSurfaceFullScreenExclusiveInfoEXT fullscreen{VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT};fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;ci.info.pNext=&fullscreen;
 assert(!SwapchainChainRefusal(&fullscreen,true));PhysicalSwapchain s;assert(s.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS&&s.Handle()==H<VkSwapchainKHR>(20));
 assert(s.Bind(H<VkQueue>(5),0)&&!s.Bind(H<VkQueue>(5),1));
 unsigned idx=0;auto handle=s.Handle();auto sem=H<VkSemaphore>(9);VkPresentInfoKHR pi{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};pi.swapchainCount=1;pi.pSwapchains=&handle;pi.pImageIndices=&idx;pi.waitSemaphoreCount=1;pi.pWaitSemaphores=&sem;
 BridgeFrame frame{2,true,Copy,nullptr};frame.compose=Compose;assert(s.Present(H<VkQueue>(5),pi,frame)==VK_SUCCESS);
 assert(compositions==1); // generated image receives the same post-NR UI composition
 assert((order==std::vector<unsigned>{1,0})&&appWaits==1&&copies==1&&submits==1&&lastQueue==H<VkQueue>(5));
 assert(s.generatedSubmitted==1&&s.generatedRetired==1&&s.realRetired==1&&s.Drain());
 acquireResult=VK_NOT_READY;assert(s.Present(H<VkQueue>(5),pi,frame)==VK_SUCCESS&&s.skipped==1&&order.back()==0&&appWaits==2&&submits==1);
 acquireResult=VK_SUCCESS;copyResult=VK_ERROR_INITIALIZATION_FAILED;assert(s.Present(H<VkQueue>(5),pi,frame)==VK_SUCCESS&&releases==1&&submits==1&&appWaits==3);
 copyResult=VK_SUCCESS;presentResult=VK_ERROR_OUT_OF_DATE_KHR;assert(s.Present(H<VkQueue>(5),pi,frame)==VK_ERROR_OUT_OF_DATE_KHR&&!s.Unsafe()&&s.Drain());presentResult=VK_SUCCESS;
 const auto waitsBefore=appWaits;const auto presentsBefore=order.size();acquireResult=VK_ERROR_OUT_OF_DATE_KHR;
 assert(s.Present(H<VkQueue>(5),pi,frame)==VK_SUCCESS&&appWaits==waitsBefore+1&&order.size()==presentsBefore+1&&order.back()==0);acquireResult=VK_SUCCESS;
 presentResult=VK_ERROR_OUT_OF_HOST_MEMORY;BridgeFrame realOnly{3,false,nullptr,nullptr};const auto noEnqueue=appWaits;
 assert(s.Present(H<VkQueue>(5),pi,realOnly)==VK_ERROR_OUT_OF_HOST_MEMORY&&!s.Unsafe()&&appWaits==noEnqueue);presentResult=VK_SUCCESS;
 const auto submitsBefore=submits;unsigned gameIndex=0;
 auto gameAcquire=std::async(std::launch::async,[&]{return s.Acquire(UINT64_MAX,H<VkSemaphore>(10),VK_NULL_HANDLE,&gameIndex);});
 {std::unique_lock lock(acquireMutex);assert(acquireCondition.wait_for(lock,std::chrono::seconds(1),[]{return gameAcquiring;}));}
 assert(s.Present(H<VkQueue>(5),pi,frame)==VK_SUCCESS&&submits==submitsBefore&&gameAcquire.get()==VK_SUCCESS&&gameIndex==2);
 assert(s.Destroy()&&destroys==1);
 // Exercise the real compiled bridge, not a reimplementation of its metadata locks.
 auto dll=LoadLibraryW(L"builds/native-framegen/bridge/NeuRotic.Fsr3.Vulkan.dll");assert(dll);
 const auto init=reinterpret_cast<BridgeInit>(GetProcAddress(dll,"NrFgInitialize"));const auto bridgeCreate=reinterpret_cast<BridgeNew>(GetProcAddress(dll,"NrFgCreate"));
 const auto configure=reinterpret_cast<BridgeConfigure>(GetProcAddress(dll,"NrFgConfigure"));const auto bridgePresent=reinterpret_cast<BridgePresent>(GetProcAddress(dll,"NrFgPresent"));
 const auto bridgeDestroy=reinterpret_cast<BridgeDestroy>(GetProcAddress(dll,"NrFgDestroy"));const auto bridgeImages=reinterpret_cast<BridgeImages>(GetProcAddress(dll,"NrFgGetImages"));
 queryDuringCopy=reinterpret_cast<BridgeQuery>(GetProcAddress(dll,"NrFgQueryProgress"));drainDuringCopy=reinterpret_cast<BridgeDrain>(GetProcAddress(dll,"NrFgDrain"));
 BridgeCallbacks callbacks{BridgeVersion,Enter,Leave,Get};assert(init&&init(&callbacks));ci.physical=H<VkPhysicalDevice>(3);ci.physicalMode=true;
 VkSwapchainKHR owned{};assert(bridgeCreate(&ci,&owned)==VK_SUCCESS&&owned==H<VkSwapchainKHR>(20));
 assert(configure(owned,&frame)&&bridgePresent(ci.queue,&pi)==VK_SUCCESS);
 BridgeProgress progress;assert(queryDuringCopy(owned,&progress)&&progress.generatedRetired==1&&progress.realRetired==1);
 auto recreation=ci;recreation.info.oldSwapchain=owned;createHandle=24;VkSwapchainKHR replacement{};
 assert(bridgeCreate(&recreation,&replacement)==VK_SUCCESS&&replacement==H<VkSwapchainKHR>(24)&&destroys==1);
 assert(queryDuringCopy(handle,&progress)&&progress.retired); // old owner was not destroyed by successful recreation
 unsigned imageCount=0;assert(bridgeImages(ci.device,handle,&imageCount,nullptr)==VK_SUCCESS&&imageCount==3);
 const auto oldCopies=copies;assert(configure(handle,&frame)&&bridgePresent(ci.queue,&pi)==VK_SUCCESS&&copies==oldCopies);
 recreation.info.oldSwapchain=replacement;createResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
 VkSwapchainKHR failed{};assert(bridgeCreate(&recreation,&failed)==VK_ERROR_OUT_OF_DEVICE_MEMORY);
 assert(queryDuringCopy(replacement,&progress)&&progress.retired);
 imageCount=0;assert(bridgeImages(ci.device,replacement,&imageCount,nullptr)==VK_SUCCESS&&imageCount==3);
 pi.pSwapchains=&replacement;assert(configure(replacement,&frame)&&bridgePresent(ci.queue,&pi)==VK_SUCCESS&&copies==oldCopies);
 assert(bridgeDestroy(ci.device,handle)&&bridgeDestroy(ci.device,replacement));
 createResult=VK_SUCCESS;createHandle=20;pi.pSwapchains=&handle;queryDuringCopy=nullptr;drainDuringCopy=nullptr;
 assert(bridgeCreate(&ci,&owned)==VK_SUCCESS);deferPresent=true;
 assert(configure(owned,&frame)&&bridgePresent(ci.queue,&pi)==VK_SUCCESS);
 recreation.info.oldSwapchain=owned;createHandle=24;
 assert(bridgeCreate(&recreation,&replacement)==VK_SUCCESS); // old WSI retirement must not block recreation
 assert(!bridgeDestroy(ci.device,owned)); // pending resources remain owned
 for(auto& [f,signalled]:fences){(void)f;signalled=true;}
 assert(bridgeDestroy(ci.device,owned)&&bridgeDestroy(ci.device,replacement));
 deferPresent=false;createHandle=20;
 const auto teardown=reinterpret_cast<BridgeTeardown>(GetProcAddress(dll,"NrFgTeardownDevice"));assert(teardown);
 const auto query=reinterpret_cast<BridgeQuery>(GetProcAddress(dll,"NrFgQueryProgress"));
 assert(bridgeCreate(&ci,&owned)==VK_SUCCESS);
 const auto beforeTeardown=destroys;
 idleResult=VK_TIMEOUT;assert(!teardown(ci.device)&&destroys==beforeTeardown);
 auto peer=ci;peer.device=H<VkDevice>(77);createHandle=24;
 assert(bridgeCreate(&peer,&replacement)==VK_SUCCESS);createHandle=20;
 assert(query(owned,&progress)&&progress.retired&&query(replacement,&progress)&&!progress.retired);
 idleResult=VK_ERROR_DEVICE_LOST;assert(teardown(ci.device)&&destroys==beforeTeardown+1);
 assert(!query(owned,&progress)&&query(replacement,&progress));
 idleResult=VK_SUCCESS;assert(teardown(peer.device)&&destroys==beforeTeardown+2&&!query(replacement,&progress));
 FreeLibrary(dll);
 const auto destroyedSafe=destroys;assert(destroyedSafe==7);
 PhysicalSwapchain unavailable;missing=true;assert(unavailable.Initialize(ci,Get)==VK_ERROR_EXTENSION_NOT_PRESENT);assert(unavailable.Destroy());missing=false;
 PhysicalSwapchain retained;assert(retained.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS);submitResult=VK_ERROR_DEVICE_LOST;
 assert(retained.Present(ci.queue,pi,frame)==VK_ERROR_DEVICE_LOST&&retained.Unsafe()&&!retained.Destroy()&&destroys==destroyedSafe);submitResult=VK_SUCCESS;
 PhysicalSwapchain oom;assert(oom.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS);presentResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;
 assert(oom.Present(ci.queue,pi,frame)==VK_ERROR_DEVICE_LOST&&oom.Unsafe()&&!oom.Destroy());presentResult=VK_SUCCESS;
 // A WSI fence can retire later, especially during fullscreen changes. The
 // native success/error must reach the game without a fabricated device loss.
 PhysicalSwapchain deferred;assert(deferred.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS);deferPresent=true;
 assert(deferred.Present(ci.queue,pi,frame)==VK_SUCCESS&&!deferred.Unsafe());
 assert(!deferred.Drain()&&deferred.generatedRetired==0&&deferred.realRetired==0);
 presentResult=VK_ERROR_FULL_SCREEN_EXCLUSIVE_MODE_LOST_EXT;
 assert(deferred.Present(ci.queue,pi,realOnly)==presentResult&&!deferred.Unsafe());
 for(auto& [f,signalled]:fences){(void)f;signalled=true;}
 assert(deferred.Drain()&&deferred.generatedRetired==1&&deferred.realRetired==1);
 assert(deferred.Destroy());deferPresent=false;presentResult=VK_SUCCESS;
 PhysicalSwapchain bounded;assert(bounded.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS);deferPresent=true;
 for(unsigned n=0;n<16;++n)assert(bounded.Present(ci.queue,pi,frame)==VK_SUCCESS);
 const auto boundedWaits=appWaits;const auto boundedSubmits=submits;
 assert(bounded.Present(ci.queue,pi,frame)==VK_ERROR_OUT_OF_HOST_MEMORY&&appWaits==boundedWaits&&submits==boundedSubmits&&!bounded.Unsafe());
 for(auto& [f,signalled]:fences){(void)f;signalled=true;}
 assert(bounded.Drain()&&bounded.generatedRetired==16&&bounded.realRetired==16);
 deferPresent=false;assert(bounded.Present(ci.queue,pi,frame)==VK_SUCCESS&&bounded.Destroy());
 PhysicalSwapchain stalled;assert(stalled.Initialize(ci,Get,Enter,Leave)==VK_SUCCESS);timeout=true;
 assert(stalled.Present(ci.queue,pi,frame)==VK_ERROR_DEVICE_LOST&&!stalled.Destroy()&&destroys==destroyedSafe+2);
 puts("PASS: physical identity/fullscreen, no private queues, exact waits, generated-real order, concurrent Acquire fallback, release/error retirement, actual DLL metadata/recreation, missing dispatch, unsafe retention");
}
