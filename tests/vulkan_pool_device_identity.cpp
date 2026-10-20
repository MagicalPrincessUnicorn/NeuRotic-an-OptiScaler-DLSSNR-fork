#include <cassert>
#include "../OptiScaler/dlssnr/VulkanNrRecording.h"
#include <cstdio>
#include <cstdlib>
using namespace DlssNr;
static void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
int main(){
 {using namespace DlssNr;
  const auto a=reinterpret_cast<VkDevice>(0x10),b=reinterpret_cast<VkDevice>(0x20);
  VkNrObservationScope first(VkNrObservation::ResetPool,0x30,a);assert(first.Observe());
  VkNrObservationScope alias(VkNrObservation::ResetPool,0x30,a);assert(!alias.Observe());
  VkNrObservationScope peer(VkNrObservation::ResetPool,0x30,b);assert(peer.Observe());
 }

 const auto a=reinterpret_cast<VkDevice>(1),b=reinterpret_cast<VkDevice>(2);
 const auto pool=reinterpret_cast<VkCommandPool>(3);
 const auto cb=reinterpret_cast<VkCommandBuffer>(4);
 VkNrRecordingOwner owner;VkCommandPoolCreateInfo ci{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
 owner.OnCreatePool(a,pool,ci,VK_SUCCESS);owner.OnAllocate(a,pool,cb,0);
 owner.OnBegin(cb,0,VK_SUCCESS);const auto use=*owner.ReserveCopy(cb,1);owner.OnEnd(cb,VK_SUCCESS);
 const auto incarnation=owner.Incarnation(cb);
 owner.OnCreatePool(b,pool,ci,VK_SUCCESS);
 owner.OnResetPool(b,pool,VK_SUCCESS);
 Check(owner.Incarnation(cb)==incarnation&&!owner.RecordingReleased(use),"foreign device pool reset preserves executable owner and private use");
 owner.OnDestroyPool(b,pool);
 Check(owner.Incarnation(cb)==incarnation&&!owner.Reusable(use),"foreign device pool destroy preserves executable owner");
 std::puts("PASS device-qualified command pool ownership");
}
