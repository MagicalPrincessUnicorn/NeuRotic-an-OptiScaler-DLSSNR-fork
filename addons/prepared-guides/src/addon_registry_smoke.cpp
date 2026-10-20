// Drives the actual addon registry callbacks through a minimal ReShade host.
// No graphics device, game, proprietary model or Vulkan registration is used.
#include <windows.h>
#include <reshade_events.hpp>
#include <dlssnr/PreparedGuideStatus.h>
#include <array>
#include <cstdio>
#include <thread>

static std::array<void*, 2048> callbacks{};
static unsigned registrations=0;
extern "C" __declspec(dllexport) bool ReShadeRegisterAddon(void*, uint32_t version){return version==20;}
extern "C" __declspec(dllexport) void ReShadeUnregisterAddon(void*){}
extern "C" __declspec(dllexport) void ReShadeRegisterEvent(reshade::addon_event event,void* callback){callbacks.at(static_cast<unsigned>(event))=callback;++registrations;}
extern "C" __declspec(dllexport) void ReShadeUnregisterEvent(reshade::addon_event event,void*){callbacks.at(static_cast<unsigned>(event))=nullptr;--registrations;}
extern "C" __declspec(dllexport) void ReShadeLogMessage(void*,int,const char*){}

int wmain(int argc,wchar_t** argv){
    if(argc!=2)return 2;
    const auto addon=LoadLibraryW(argv[1]);
    if(!addon){std::printf("LoadLibrary failed: %lu\n",GetLastError());return 3;}
    namespace P=DlssNr::PreparedGuides;
    auto query=reinterpret_cast<P::QueryFn>(GetProcAddress(addon,P::QueryExport));
    if(!query){std::puts("FAIL: loaded addon has no prepared input status query");return 6;}
    P::Status status;
    if(query(nullptr))return 7;
    status.version=99;if(query(&status))return 8;
    status=P::Status{};if(query(&status)!=1||status.active||status.inputFrames||status.modelCompletions||status.copybackCompletions)return 9;
    using Callback=void(*)(reshade::api::swapchain*,bool);
    const auto init=reinterpret_cast<Callback>(callbacks[static_cast<unsigned>(reshade::addon_event::init_swapchain)]);
    const auto destroy=reinterpret_cast<Callback>(callbacks[static_cast<unsigned>(reshade::addon_event::destroy_swapchain)]);
    if(!init||!destroy||registrations!=9)return 4;
    auto exercise=[&](uintptr_t identity){auto* chain=reinterpret_cast<reshade::api::swapchain*>(identity);for(int i=0;i<1000;++i){init(chain,false);destroy(chain,false);}};
    std::thread first(exercise,uintptr_t{1}),second(exercise,uintptr_t{2});first.join();second.join();
    FreeLibrary(addon);if(registrations)return 5;
    std::puts("PASS: actual addon status ABI rejection/initial state, 2000 concurrent registry pairs, and callback unregistration");
    return 0;
}
