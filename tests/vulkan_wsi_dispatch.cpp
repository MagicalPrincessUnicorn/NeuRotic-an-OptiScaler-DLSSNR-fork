#include <dlssnr/VulkanWsiDispatch.h>
#include <cstdio>
#include <cstdlib>
#include <thread>
using namespace DlssNr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
static unsigned a=0,b=0,observed=0;
static VkResult VKAPI_CALL First(VkQueue,const VkPresentInfoKHR*){++a;return VK_SUBOPTIMAL_KHR;}
static VkResult VKAPI_CALL Second(VkQueue,const VkPresentInfoKHR*){++b;return VK_ERROR_OUT_OF_DATE_KHR;}
struct PresentTag { static VkResult Invoke(PFN_vkQueuePresentKHR target,VkQueue q,const VkPresentInfoKHR* p){++observed;return target(q,p);} };
static unsigned hookCalls=0,driverCalls=0;
static PFN_vkQueuePresentKHR nestedEntry=nullptr;
static bool nest=false;
static VkResult VKAPI_CALL Driver(VkQueue queue,const VkPresentInfoKHR*)
{
    ++driverCalls;
    if(nest){nest=false;VkPresentInfoKHR independent{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
        Check(nestedEntry(queue,&independent)==VK_SUCCESS,"independent nested original returns");}
    return VK_SUCCESS;
}
static VkResult VKAPI_CALL Hook(VkQueue queue,const VkPresentInfoKHR* info)
{
    auto target=VkWsiTargetScope<PFN_vkQueuePresentKHR>::Get(Driver);
    if(VkWsiForwardScope::Matches(VkWsiOperation::Present,(uintptr_t)queue,info))return target(queue,info);
    ++hookCalls;auto local=*info;
    VkWsiForwardScope forward(VkWsiOperation::Present,(uintptr_t)queue,&local);
    VkWsiTargetScope<PFN_vkQueuePresentKHR> clear(nullptr);
    return target(queue,&local);
}
int main(){
    using Slots=VkWsiFunctionSlots<PresentTag,PFN_vkQueuePresentKHR,2>;
    auto first=Slots::Wrap(First),second=Slots::Wrap(Second);
    Check(first&&second&&first!=second,"distinct downstream functions have distinct typed entries");
    Check(Slots::Wrap(First)==first&&Slots::Wrap(first)==first,"repeat resolver and already wrapped pointer are stable");
    Check(first(VK_NULL_HANDLE,nullptr)==VK_SUBOPTIMAL_KHR&&second(VK_NULL_HANDLE,nullptr)==VK_ERROR_OUT_OF_DATE_KHR,"original results preserved");
    Check(a==1&&b==1&&observed==2,"each actual pointer invoked exactly once");
    auto third=+[](VkQueue,const VkPresentInfoKHR*)->VkResult{return VK_TIMEOUT;};
    Check(Slots::Wrap(third)==third,"exhaustion preserves callable original without false coverage");
    Check(!Slots::Wrap(nullptr),"null unsupported entry stays null");
    VkPresentInfoKHR p{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR},nested{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
    {VkWsiForwardScope s(VkWsiOperation::Present,1,&p);Check(VkWsiForwardScope::Matches(VkWsiOperation::Present,1,&p),"forwarded tuple suppressed");
     Check(!VkWsiForwardScope::Matches(VkWsiOperation::Present,1,&nested),"independent nested same queue remains observed");
     Check(!VkWsiForwardScope::Matches(VkWsiOperation::Present,2,&p),"different queue remains observed");}
    Check(!VkWsiForwardScope::Matches(VkWsiOperation::Present,1,&p),"scope unwinds");
    using HookSlots=VkWsiFunctionSlots<VkWsiHookTag<Hook>,PFN_vkQueuePresentKHR>;
    // Emulates GDPA returning a detoured exported entry rather than the driver
    // address. The forwarded export must use its own original trampoline.
    const auto cachedExport=HookSlots::Wrap(Hook);
    Check(cachedExport((VkQueue)4,&p)==VK_SUCCESS&&hookCalls==1&&driverCalls==1,
        "resolver to exported hook observes exactly once without recursive original");
    nestedEntry=cachedExport;nest=true;
    cachedExport((VkQueue)4,&p);
    Check(hookCalls==3&&driverCalls==3,"independent nested Present on same queue is retained");
    Check(!VkWsiTargetScope<PFN_vkQueuePresentKHR>::current,"original target restored after nested calls");
    VkWsiDevices devices;const auto d=(VkDevice)1;const auto q=(VkQueue)3;
    auto gen=devices.Created(d,(VkPhysicalDevice)2,(VkInstance)4,{VK_QUEUE_GRAPHICS_BIT});
    devices.Queue(d,q,0,0);Check(devices.ForQueue(q).generation==gen,"queue exact device generation");
    devices.Destroyed(d);Check(!devices.ForQueue(q).generation,"destroy invalidates queue mapping");
    auto gen2=devices.Created(d,(VkPhysicalDevice)2,(VkInstance)4,{VK_QUEUE_GRAPHICS_BIT});
    Check(gen2>gen&&!devices.ForQueue(q).generation,"raw device handle reuse grants no old queue");
    devices.Queue(d,q,0,VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT);Check(!devices.ForQueue(q).generation,"protected queue cannot grant overlay rights");
    devices.Disable();Check(!devices.Created(d,(VkPhysicalDevice)2,(VkInstance)4,{VK_QUEUE_GRAPHICS_BIT}),"coverage exhaustion remains closed across recreation");
    Check(!VkWsiNeedsOverlayOnly(true,true,true,false),"observed public unload Off supersedes stale last On");
    Check(VkWsiNeedsOverlayOnly(true,false,false,false),"unknown observed provider cannot admit semantic NR");
    Check(VkWsiNeedsOverlayOnly(true,true,false,true),"independent FG provider still bypasses semantic NR");
    VkPresentInfoKHR uncalled{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};uncalled.swapchainCount=1;
    uncalled.pResults=reinterpret_cast<VkResult*>(1);
    Check(VkWsiSelectedPresentResult(false,VK_ERROR_DEVICE_LOST,uncalled)==VK_ERROR_DEVICE_LOST,
        "uncertain overlay never reads original Present output that was not written");
    Check(VkWsiSelectedPresentResult(true,VK_ERROR_DEVICE_LOST,uncalled)==VK_ERROR_DEVICE_LOST,
        "failed original result takes precedence without assuming output validity");
    VkResult perImage=VK_SUBOPTIMAL_KHR;uncalled.pResults=&perImage;
    Check(VkWsiSelectedPresentResult(true,VK_SUCCESS,uncalled)==VK_SUBOPTIMAL_KHR,"successful original honors per-image result");
    VkWsiDevices aliases;aliases.Created(d,(VkPhysicalDevice)2,(VkInstance)4,{VK_QUEUE_GRAPHICS_BIT});
    aliases.Created((VkDevice)9,(VkPhysicalDevice)2,(VkInstance)4,{VK_QUEUE_GRAPHICS_BIT});
    aliases.Swapchain(d,(VkSwapchainKHR)8);aliases.Swapchain((VkDevice)9,(VkSwapchainKHR)8);
    Check(!aliases.Device(d).generation&&!aliases.Device((VkDevice)9).generation,"ambiguous swapchain numbers across devices never mix identities");
    std::puts("PASS loader WSI exact dispatch, exhaustion, nested forwarding and device generation");
}
