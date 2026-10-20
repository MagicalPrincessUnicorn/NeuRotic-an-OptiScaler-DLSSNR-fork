// Product PCH may include core Vulkan before platform-selection macros.
#include <vulkan/vulkan.h>
#include "../OptiScaler/dlssnr/NativeFgVulkan.h"
#include "../OptiScaler/dlssnr/NativeFgLayouts.h"
#include <cassert>
#include <cstdio>
#include <future>
#include <chrono>
using namespace DlssNr::NativeFg;
static BridgeCallbacks callbacks;static unsigned creates=0,presents=0,drains=0,destroys=0;static BridgeFrame configuredFrame;
static bool Init(const BridgeCallbacks* c){assert(c->version==BridgeVersion);callbacks=*c;return true;}
static bool lastPhysical=false,oldRetired=false;static VkResult createResult=VK_SUCCESS;
static VkResult CreateFixture(const BridgeCreate* c,VkSwapchainKHR* out){assert(c->version==BridgeVersion&&c->queue);lastPhysical=c->physicalMode;++creates;if(createResult!=VK_SUCCESS)return createResult;*out=reinterpret_cast<VkSwapchainKHR>(10);return VK_SUCCESS;}
static bool QueryFixture(VkSwapchainKHR,BridgeProgress* p){p->retired=oldRetired;return true;}
static void UnlockedMetadata(VkSwapchainKHR s){auto query=std::async(std::launch::async,[s]{return Frame(s);});assert(query.wait_for(std::chrono::seconds(1))==std::future_status::ready);query.get();}
static VkResult ImagesFixture(VkDevice,VkSwapchainKHR s,unsigned* count,VkImage* images){UnlockedMetadata(s);if(images)for(unsigned i=0;i<3;++i)images[i]=reinterpret_cast<VkImage>(uintptr_t(20+i));*count=3;return VK_SUCCESS;}
static VkResult AcquireFixture(VkDevice,VkSwapchainKHR s,std::uint64_t,VkSemaphore,VkFence,unsigned* index){UnlockedMetadata(s);*index=1;return VK_SUCCESS;}
static bool ConfigFixture(VkSwapchainKHR,const BridgeFrame* frame){configuredFrame=*frame;return true;}
static VkResult presentResult=VK_SUCCESS;
static VkResult PresentFixture(VkQueue,const VkPresentInfoKHR*){++presents;return presentResult;}
static bool drainOkay=true,destroyOkay=true;
static unsigned binds=0;static bool bindOkay=true;static VkQueue boundQueue{};
static bool BindFixture(VkDevice,VkSwapchainKHR,VkQueue q,unsigned family){assert(family==0);++binds;boundQueue=q;return bindOkay;}
static bool DrainFixture(VkSwapchainKHR){++drains;return drainOkay;}
static bool DestroyFixture(VkDevice,VkSwapchainKHR){if(!destroyOkay)return false;++destroys;return true;}
static bool TeardownFixture(VkDevice){if(!drainOkay||!destroyOkay)return false;++destroys;return true;}
static void VKAPI_CALL QueueFixture(VkDevice,unsigned,unsigned index,VkQueue* queue){*queue=reinterpret_cast<VkQueue>(static_cast<uintptr_t>(100+index));}
static VkResult Copy(void*,VkCommandBuffer,VkImage,unsigned,unsigned,std::uint64_t){return VK_SUCCESS;}
int main(){InjectBridge({Init,CreateFixture,ImagesFixture,AcquireFixture,ConfigFixture,PresentFixture,DrainFixture,DestroyFixture,BindFixture,QueryFixture,TeardownFixture});
    VkSwapchainCreateInfoKHR ci{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};ci.imageArrayLayers=1;ci.minImageCount=2;ci.imageExtent={128,128};ci.imageFormat=VK_FORMAT_B8G8R8A8_UNORM;
    ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;ci.compositeAlpha=VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;ci.imageUsage=VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    auto device=reinterpret_cast<VkDevice>(1);auto queue=reinterpret_cast<VkQueue>(2);auto physical=reinterpret_cast<VkPhysicalDevice>(3);VkSwapchainKHR swap{};
    assert(!Create(device,physical,queue,0,&ci,nullptr,&swap,false));
    DeviceCreated(device,0,1,QueueFixture);
    VkSurfaceFullScreenExclusiveWin32InfoEXT monitor{VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_WIN32_INFO_EXT};monitor.hmonitor=reinterpret_cast<HMONITOR>(1);
    VkSurfaceFullScreenExclusiveInfoEXT fullscreen{VK_STRUCTURE_TYPE_SURFACE_FULL_SCREEN_EXCLUSIVE_INFO_EXT};fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;fullscreen.pNext=&monitor;ci.pNext=&fullscreen;
    assert(Create(device,physical,queue,0,&ci,nullptr,&swap,true)==VK_SUCCESS&&Owns(swap)&&creates==1);
    assert(SupportedSwapchainChain(nullptr));
    for(auto mode:{VK_FULL_SCREEN_EXCLUSIVE_ALLOWED_EXT,VK_FULL_SCREEN_EXCLUSIVE_DISALLOWED_EXT}){fullscreen.fullScreenExclusive=mode;assert(SupportedSwapchainChain(&fullscreen));}
    fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;assert(!SupportedSwapchainChain(&fullscreen));fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
    monitor.pNext=&fullscreen;assert(!SupportedSwapchainChain(&fullscreen));monitor.pNext=nullptr;
    VkBaseInStructure unknown{VK_STRUCTURE_TYPE_IMAGE_FORMAT_LIST_CREATE_INFO,nullptr};assert(!SupportedSwapchainChain(&unknown));
    monitor.hmonitor=nullptr;assert(!SupportedSwapchainChain(&fullscreen));monitor.hmonitor=reinterpret_cast<HMONITOR>(1);
    ci.flags=VK_SWAPCHAIN_CREATE_PROTECTED_BIT_KHR;VkSwapchainKHR refused{};assert(!Create(device,physical,queue,0,&ci,nullptr,&refused,true));
    assert(Status().find("swapchain creation flags")!=std::string::npos);ci.flags=0;
    assert(!Create(device,physical,queue,0,&ci,nullptr,&refused,false,"test queue registration failure"));
    assert(Status().find("test queue registration failure")!=std::string::npos&&creates==1);
    fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
    assert(!Create(device,physical,queue,0,&ci,nullptr,&refused,true));
    assert(Status().find("application-controlled exclusive fullscreen")!=std::string::npos&&creates==1);
    fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
    ci.imageSharingMode=VK_SHARING_MODE_CONCURRENT;
    assert(!Create(device,physical,queue,0,&ci,nullptr,&refused,true));
    assert(Status().find("concurrent swapchain queue sharing")!=std::string::npos);ci.imageSharingMode=VK_SHARING_MODE_EXCLUSIVE;
    ci.imageColorSpace=VK_COLOR_SPACE_HDR10_ST2084_EXT;
    assert(!Create(device,physical,queue,0,&ci,nullptr,&refused,true));
    assert(Status().find("SDR required")!=std::string::npos);ci.imageColorSpace=VK_COLOR_SPACE_SRGB_NONLINEAR_KHR;
    VkAllocationCallbacks allocator{};
    assert(!Create(device,physical,queue,0,&ci,&allocator,&refused,true));
    assert(Status().find("custom swapchain allocation")!=std::string::npos&&creates==1);
    unsigned count=0,index=0;assert(Images(device,swap,&count,nullptr)==VK_SUCCESS&&count==3);
    assert(Acquire(device,swap,0,reinterpret_cast<VkSemaphore>(4),nullptr,&index)==VK_SUCCESS&&index==1);
    assert(!Begin(swap));
    auto actual=reinterpret_cast<VkQueue>(5);
    assert(!BindPresentQueue(device,swap,actual,1)&&!BindPresentQueue(reinterpret_cast<VkDevice>(6),swap,actual,0));
    assert(!BindPresentQueue(device,swap,reinterpret_cast<VkQueue>(101),0)&&binds==0);
    assert(BindPresentQueue(device,swap,actual,0)&&boundQueue==actual&&binds==1);
    assert(BindPresentQueue(device,swap,actual,0)&&binds==1);
    assert(Begin(swap)&&Finish(swap)&&!configuredFrame.generated&&configuredFrame.frame==1);
    assert(Begin(swap)&&Publish(swap,Copy,nullptr)&&Finish(swap)&&configuredFrame.generated&&configuredFrame.frame==2);
    VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};p.swapchainCount=1;p.pSwapchains=&swap;p.pImageIndices=&index;
    auto present=ResolvePresent(&p,nullptr);assert(present&&present(queue,&p)==VK_ERROR_INITIALIZATION_FAILED&&presents==0);
    assert(present(actual,&p)==VK_SUCCESS&&presents==1);
    assert(BindPresentQueue(device,swap,queue,0)&&binds==2);
    presentResult=VK_ERROR_OUT_OF_DATE_KHR;
    assert(Begin(swap)&&Finish(swap)&&!configuredFrame.generated);
    assert(present(queue,&p)==VK_ERROR_OUT_OF_DATE_KHR&&DrainDevice(device));
    callbacks.enter(true);assert(Internal());callbacks.enter(false);callbacks.leave(false);assert(Internal());callbacks.leave(true);assert(!Internal());
    assert(DrainDevice(device));drainOkay=false;
    assert(!DrainDevice(device,false)&&!DrainQueue(queue,false)); // async workers must complete before native idle
    assert(!DestroyDevice(device)&&!Owns(swap)&&Images(device,swap,&count,nullptr).has_value()&&destroys==0);
    drainOkay=true;destroyOkay=false;
    assert(!DestroyDevice(device)&&!Owns(swap)&&Images(device,swap,&count,nullptr).has_value()&&destroys==0);
    destroyOkay=true;
    assert(DestroyDevice(device)&&!Owns(swap)&&destroys==1&&drains>=1);
    assert(!Images(device,swap,&count,nullptr));
    // Physical presentation uses actual images and the actual Present queue,
    // even when the game requested every queue and application fullscreen.
    DeviceMaintenance(device,true);fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_APPLICATION_CONTROLLED_EXT;
    assert(Create(device,physical,queue,0,&ci,nullptr,&swap,true)==VK_SUCCESS&&lastPhysical);
    assert(BindPresentQueue(device,swap,actual,0));
    drainOkay=false;assert(DrainDevice(device,false)&&DrainQueue(actual,false)&&!DrainDevice(device));drainOkay=true;
    VkImage realImages[3]{};count=3;assert(Images(device,swap,&count,realImages)==VK_SUCCESS);
    assert(Layouts::Translate(device,realImages[0],VK_IMAGE_LAYOUT_PRESENT_SRC_KHR)==VK_IMAGE_LAYOUT_PRESENT_SRC_KHR);
    assert(Begin(swap)&&Finish(swap));
    assert(Composition(swap,Copy,reinterpret_cast<void*>(42))&&configuredFrame.compose==Copy&&configuredFrame.composeContext==reinterpret_cast<void*>(42));
    // A failed native recreation still retires the old physical swapchain.
    auto recreate=ci;recreate.oldSwapchain=swap;createResult=VK_ERROR_OUT_OF_DEVICE_MEMORY;oldRetired=true;
    assert(Create(device,physical,queue,0,&recreate,nullptr,&refused,true)==createResult);
    assert(Begin(swap)&&!Publish(swap,Copy,nullptr)&&Finish(swap)&&!configuredFrame.generated);
    count=3;assert(Images(device,swap,&count,realImages)==VK_SUCCESS);
    destroyOkay=false;
    assert(Destroy(device,swap)==true&&!Owns(swap)&&!DestroyDevice(device));
    destroyOkay=true;assert(!Begin(swap)&&DestroyDevice(device)); // deferred owner collected, never reused
    createResult=VK_SUCCESS;oldRetired=false;
    fullscreen.fullScreenExclusive=VK_FULL_SCREEN_EXCLUSIVE_DEFAULT_EXT;
    DeviceCreated(device,0,1,QueueFixture);assert(Create(device,physical,queue,0,&ci,nullptr,&swap,true)==VK_SUCCESS);
    assert(BindPresentQueue(device,swap,queue,0));bindOkay=false;
    assert(!BindPresentQueue(device,swap,actual,0));const auto failedBindCount=binds;
    assert(!BindPresentQueue(device,swap,queue,0)&&binds==failedBindCount&&DestroyDevice(device));
    std::puts("PASS: physical no-spare/fullscreen routing, failed recreation retirement, unlocked Acquire/Images, observed queue binding, failure retention and virtual WSI routing (bridge fixture)");
}
