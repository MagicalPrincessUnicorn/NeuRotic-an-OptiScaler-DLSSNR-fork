"""Compile the product shutdown dispatcher with typed lifecycle boundary doubles.

The real generation/recording and executor suites separately exercise GPU rights.
This fixture reproduces a failed shutdown followed by actual recording release.
"""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp').read_text(encoding='utf-8')
start=source.find('static void ShutdownVkSessionsLocked(')
if start < 0: start=source.index('void ShutdownVk(bool deviceAlive)')
body=source[start:source.index('void NotifyDeviceInitVk()',start)]
helper_start=source.find('static bool PrepareVkPassDeviceLocked(')
if helper_start<0: helper='static bool PrepareVkPassDeviceLocked(VkDevice){return true;}\n'
else:
    pos=source.index('{',helper_start);depth=1;end=pos+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    helper=source[helper_start:end]+'\n'
prefix=r'''
#include <mutex>
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstdint>
#include <initializer_list>
using VkDevice=void*;
#define VK_NULL_HANDLE nullptr
#define LOG_INFO(...) ((void)0)
enum class VkNrRoute { Present, Native };
struct State { VkDevice device=nullptr; std::unique_lock<std::mutex>* controlLock=nullptr; void* borrowedPass=nullptr; bool blocked=false; int cleanups=0; };
std::array<State,10> present,native;
State& g_nativeVk=native[0];
struct Runtime {bool initialized=false;VkDevice device=nullptr;bool Initialized(){return initialized;}VkDevice Device(){return device;}} g_vkRuntime;
enum class VkRouteDeviceUse {FirstUse,Same,Conflict};
VkRouteDeviceUse ClassifyVkRouteDevice(VkDevice owned,VkDevice provider,VkDevice requested){return (owned&&owned!=requested)||(provider&&provider!=requested)?VkRouteDeviceUse::Conflict:owned?VkRouteDeviceUse::Same:VkRouteDeviceUse::FirstUse;}
State* active=&native[0];
State& PassStateVk(VkNrRoute r,uint32_t p){return (r==VkNrRoute::Present?present:native)[p];}
State& ActiveVk(){return *active;}
struct ScopedVkSession {State* old;ScopedVkSession(VkNrRoute r,uint32_t p=0):old(active){active=&PassStateVk(r,p);}~ScopedVkSession(){active=old;}};
std::mutex g_nativeRecordMutex,g_vkMutex;
bool g_vkSessionClosed=false,g_vkShutdownFailed=false,g_vkCleanupActive=false;
struct Executor {void Shutdown(bool){} void DeviceDestroyed(VkDevice){} } executor;
Executor& GetVulkanPresentExecutor(){return executor;}
uint64_t GenerationBytesVk(){return 0;}
size_t GenerationCountVk(){return 0;}
void ShutdownVkLocked(bool alive){
 auto& s=ActiveVk();if(!s.device)return;
 if(alive&&s.blocked){g_vkShutdownFailed=true;return;}
 ++s.cleanups;s.device=nullptr;if(&s==&g_nativeVk)g_vkRuntime.initialized=false;
}
void Check(bool ok,const char* why){if(!ok){std::fprintf(stderr,"FAIL: %s\n",why);std::exit(1);}}
'''
suffix=r'''
int main(){
 auto d=reinterpret_cast<VkDevice>(1);
 g_vkSessionClosed=true;
 Check(!PrepareVkPassDeviceLocked(d)&&!native[0].device,"closed chain cannot allocate its first pass");
 g_vkSessionClosed=false;
 Check(PrepareVkPassDeviceLocked(d)&&native[0].device==d,"partial first pass has visible device ownership before allocation");
 present[0].device=d;present[0].blocked=true;native[0].device=d;
 ShutdownVk(true);
 Check(g_vkSessionClosed&&present[0].device==d,"failed release retains closed session");
 present[0].blocked=false; // game freed its recording; vendor release now succeeds
 ShutdownVk(true);
 Check(!present[0].device&&!native[0].device,"second shutdown must retry retained model cleanup");
 ShutdownVk(true);Check(present[0].cleanups==1&&native[0].cleanups==1,"successful cleanup is idempotent");
 g_vkRuntime.initialized=true;g_vkRuntime.device=d; // Present-only shared provider
 ShutdownVk(true);Check(!g_vkRuntime.initialized,"Present-only provider still receives native shutdown ownership");
 // Root composition owns the pass used by its child recordings.
 native[0].device=d;native[1].device=d;native[1].blocked=true;
 ShutdownVk(true);Check(native[0].device==d,"root survives unresolved child replay rights");
 native[1].blocked=false;ShutdownVk(true);Check(!native[0].device&&!native[1].device,"retry retires child before root");
#ifdef NR_SHUTDOWN_RETRY
 auto other=reinterpret_cast<VkDevice>(2);
 native[0].device=other;present[0].device=d;
 RetryShutdownVk(d);Check(!present[0].device&&native[0].device==other,"recording release retries only matching device");
 present[0].device=d;{std::lock_guard lock(g_nativeRecordMutex);RetryShutdownVk(d);}
 Check(present[0].device==d,"reentrant hook cleanup is nonblocking");
 DeviceDestroyedVk(d);Check(!present[0].device&&native[0].device==other,"dead device clears only its own bookkeeping");
 DeviceDestroyedVk(d);Check(native[0].device==other,"duplicate device cleanup is harmless");
#endif
 std::puts("PASS product shutdown retry, child ownership, idempotence and device scoping");
}
'''
out=root/'builds/vulkan-experiment/tests/vulkan_shutdown_dispatch.cpp'
out.write_text(('#define NR_SHUTDOWN_RETRY\n' if 'void RetryShutdownVk(' in body else '')+prefix+helper+body+suffix,encoding='utf-8')
