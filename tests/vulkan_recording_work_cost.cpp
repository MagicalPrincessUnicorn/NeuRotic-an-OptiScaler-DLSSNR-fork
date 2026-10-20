#include <vulkan/vulkan.h>
#include <array>
#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <future>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <unordered_map>
#include <vector>
// Test-only visibility to pause the actual owner lock. No runtime replacement.
#define private public
#include "../OptiScaler/dlssnr/VulkanNrRecording.h"
#undef private
using namespace DlssNr;
template<class T> static T H(uintptr_t n){return reinterpret_cast<T>(n);}
static void Check(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL: %s\n",message);std::exit(1);}}
static bool WorkBypassesOwner(VkNrRecordingOwner& owner,VkCommandBuffer cb){
 std::unique_lock pause(owner.mutex_);
 std::promise<void> started;auto ready=started.get_future();
 auto task=std::async(std::launch::async,[&]{started.set_value();owner.OnWork(cb);});ready.wait();
 bool bypass=task.wait_for(std::chrono::milliseconds(100))==std::future_status::ready;
 pause.unlock();task.get();return bypass;
}
int main(){
 VkNrRecordingOwner owner;auto device=H<VkDevice>(1);auto pool=H<VkCommandPool>(2);auto a=H<VkCommandBuffer>(3);auto b=H<VkCommandBuffer>(4);
 auto begin=[&](VkCommandBuffer cb){owner.OnAllocate(device,pool,cb,0);owner.OnBegin(cb,0,VK_SUCCESS);};begin(a);begin(b);
 auto start=std::chrono::steady_clock::now();for(unsigned n=0;n<1000000;++n)owner.OnWork(a);
 std::printf("Unobserved work calls=1000000 host_ms=%.3f (synthetic; no game timing)\n",std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count());
 Check(WorkBypassesOwner(owner,a),"unobserved command work must bypass the shared submission/recording lock");
 auto overlay=owner.ReserveSubmissionDependency(device,1,0);Check(bool(overlay),"overlay dependency reserved");
 Check(WorkBypassesOwner(owner,a),"submission-only overlay dependency must not arm work tracking");
 auto first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)!=first,"work after a sampled token invalidates it");
 first=owner.CommandSerial(a);std::thread other([&]{for(int i=0;i<1000;++i)owner.OnWork(b);});
 for(int i=0;i<1000;++i)owner.OnWork(a);other.join();Check(owner.CommandSerial(a)==first+1000,"concurrent unrelated work does not change a watched recording's token");
 owner.OnEnd(a,VK_ERROR_OUT_OF_HOST_MEMORY);first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"failed end retains the watcher");
 owner.OnBegin(a,0,VK_ERROR_OUT_OF_HOST_MEMORY);first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"failed begin retains the watcher");
 owner.OnReset(a,VK_ERROR_OUT_OF_HOST_MEMORY);first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"failed reset retains the watcher");
 owner.OnEnd(a,VK_SUCCESS);Check(owner.CommandSerial(a)==UINT64_MAX,"ended recordings have no usable token");Check(WorkBypassesOwner(owner,b),"last successful end disarms work tracking");
 owner.OnBegin(a,0,VK_SUCCESS);first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"fresh incarnation arms independently");
 owner.OnReset(a,VK_SUCCESS);Check(WorkBypassesOwner(owner,b),"successful reset disarms the last watcher");
 owner.OnBegin(a,0,VK_SUCCESS);(void)owner.CommandSerial(a);(void)owner.CommandSerial(b);
 owner.OnFree(a);first=owner.CommandSerial(b);owner.OnWork(b);Check(owner.CommandSerial(b)==first+1,"free of one watcher preserves another");
 owner.OnResetPool(device,pool,VK_SUCCESS);Check(WorkBypassesOwner(owner,b),"pool reset disarms remaining watchers");
 begin(a);auto use=owner.Reserve(a,1);Check(bool(use),"model reservation accepted");first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"reserved output still detects later work");
 owner.OnFree(a);Check(WorkBypassesOwner(owner,b),"free disarms last watcher");
 begin(a);(void)owner.CommandSerial(a);owner.OnDestroyPool(device,pool);Check(WorkBypassesOwner(owner,a),"pool destruction disarms watchers");
 begin(a);(void)owner.CommandSerial(a);owner.OnDeviceDestroyed(device);Check(WorkBypassesOwner(owner,a),"device destruction disarms watchers");
 Check(owner.CommandSerial(a)==UINT64_MAX,"destroyed handle cannot yield a token");begin(a);
 Check(WorkBypassesOwner(owner,a),"reused handle begins without a stale watcher");first=owner.CommandSerial(a);owner.OnWork(a);Check(owner.CommandSerial(a)==first+1,"reused handle remains observable");
 owner.OnReset(a,VK_SUCCESS);owner.OnReset(a,VK_SUCCESS);owner.OnFree(a);owner.OnDestroyPool(device,pool);owner.OnDeviceDestroyed(device);
 Check(WorkBypassesOwner(owner,a),"duplicate close paths never underflow active watcher count");
 std::puts("PASS actual recording work: cold lock bypass, candidate invalidation, concurrent buffers, failed/successful lifecycle and reuse");
}
