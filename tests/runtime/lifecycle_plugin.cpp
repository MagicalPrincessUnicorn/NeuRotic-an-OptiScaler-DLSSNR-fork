#include "runtime/RuntimeLifetime.h"
static LONG initialized=0;
static DWORD WINAPI Child(void*){InterlockedIncrement(&initialized);return 0;}
static DWORD WINAPI Bootstrap(void*){
    auto child=CreateThread(nullptr,0,Child,nullptr,0,nullptr);
    if(!child)return 1;
    const auto result=WaitForSingleObject(child,3000);CloseHandle(child);return result==WAIT_OBJECT_0?0:1;
}
extern "C" __declspec(dllexport) LONG Ready(){return InterlockedCompareExchange(&initialized,0,0);}
BOOL WINAPI DllMain(HMODULE module,DWORD reason,void*){
    if(reason==DLL_PROCESS_ATTACH){
        if(!Neurotic::Runtime::PinRuntime(module))return FALSE;
        auto worker=CreateThread(nullptr,0,Bootstrap,nullptr,0,nullptr);if(!worker)return FALSE;CloseHandle(worker);
    }
    if(reason==DLL_PROCESS_DETACH)Neurotic::Runtime::RequestProcessExit();
    return TRUE;
}
