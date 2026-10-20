#include "FixtureApi.h"
#include <atomic>
#include <cstring>

#ifndef COMMON_FIXTURE_TAG
#error Compile an explicit authored fixture tag.
#endif
static CommonFixtureLedger* ledger = nullptr;
static std::atomic<unsigned> state{COMMON_FIXTURE_TAG};
static std::atomic<bool> active{true};
static void* startupParameters=nullptr;

extern "C" __declspec(dllexport) void FixtureConfigureLedger(CommonFixtureLedger* value) { ledger = value; }
extern "C" __declspec(dllexport) unsigned FixtureReadState() { return state.load(); }
extern "C" __declspec(dllexport) void FixtureSetState(unsigned value) { state.store(value); }
extern "C" __declspec(dllexport) unsigned FixtureCall(unsigned argument)
{
    const bool enteredActive=active.load();
    if(ledger)
    {
        InterlockedIncrement(&ledger->entered);
        if(InterlockedCompareExchange(&ledger->block,0,0))
        {
            SetEvent(ledger->enteredEvent);
            if(WaitForSingleObject(ledger->releaseEvent,10000)!=WAIT_OBJECT_0)return 0;
        }
    }
    return enteredActive&&active.load()?COMMON_FIXTURE_TAG*1000u+argument:0;
}
extern "C" __declspec(dllexport) bool FixtureOnLoad(void*,const char*,const char** json)
{
    if(!json)return false;
#if COMMON_FIXTURE_TAG == 1
    *json="{\"fixture\":\"A\"}";
#else
    *json="{\"fixture\":\"B\"}";
#endif
    return true;
}
#if COMMON_FIXTURE_TAG == 1
extern "C" __declspec(dllexport) bool FixtureOnStartup(const char*,void*) { active.store(true);return true; }
#else
extern "C" __declspec(dllexport) bool FixtureOnStartup(const char*,void*,void* parameters)
{startupParameters=parameters;active.store(true);return true;}
#endif
extern "C" __declspec(dllexport) void* FixtureReadStartupParameters() { return startupParameters; }
extern "C" __declspec(dllexport) void FixtureOnShutdown() { active.store(false); }
extern "C" __declspec(dllexport) void* FixtureGetFunction(const char* name)
{
    if(!name)return nullptr;
    if(std::strcmp(name,"call")==0)return reinterpret_cast<void*>(&FixtureCall);
    if(std::strcmp(name,"slOnPluginLoad")==0)return reinterpret_cast<void*>(&FixtureOnLoad);
    if(std::strcmp(name,"slOnPluginStartup")==0)return reinterpret_cast<void*>(&FixtureOnStartup);
    if(std::strcmp(name,"slOnPluginShutdown")==0)return reinterpret_cast<void*>(&FixtureOnShutdown);
    return nullptr;
}
BOOL APIENTRY DllMain(HMODULE,DWORD reason,LPVOID)
{
    if(reason==DLL_PROCESS_ATTACH){ledger=nullptr;startupParameters=nullptr;state.store(COMMON_FIXTURE_TAG);active.store(true);}
    if(reason==DLL_PROCESS_DETACH&&ledger)InterlockedIncrement(&ledger->detached);
    return TRUE;
}
