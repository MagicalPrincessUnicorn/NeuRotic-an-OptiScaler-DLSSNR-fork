// Conditional control: the authored host REALLY owns exclusion of every caller.
// This does not assert that production already-loaded SDK recovery has that gate.
#include "FixtureApi.h"
#include "ModuleEpochProbe.h"
#include <detours/detours.h>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <shared_mutex>
#include <stdexcept>
#include <string>
#include <thread>

using CommonModuleProbe::Epoch;
using CommonModuleProbe::ImageLease;
using CommonModuleProbe::Registry;
static FixtureGetter originalGetter=nullptr,cachedGetter=nullptr;
static FixtureCallback originalCallback=nullptr;
static Epoch* owner=nullptr;
static std::shared_mutex callerGate;
static std::atomic<bool> ready{false},stop{false};
static std::atomic<unsigned> beforeCalls{0},afterCalls{0},bad{0},forwarded{0},publicationFault{0};
static unsigned checks=0;
struct FactoryBarrier
{
    std::atomic<unsigned> arrived{0};
    static void Meet(void* context)
    {
        auto& self=*static_cast<FactoryBarrier*>(context);
        ++self.arrived;
        const auto start=GetTickCount64();
        while(self.arrived.load()<2&&GetTickCount64()-start<5000)std::this_thread::yield();
    }
};
static void Check(bool value,const char* text)
{
    if(!value)throw std::runtime_error(text);
    ++checks;std::printf("PASS %s\n",text);
}
static unsigned Callback(unsigned value)
{
    auto hold=ImageLease::Acquire(owner,reinterpret_cast<const void*>(originalCallback));
    return hold?originalCallback(value):0;
}
static void* Getter(const char* name)
{
    auto hold=ImageLease::Acquire(owner,reinterpret_cast<const void*>(cachedGetter));
    if(!hold)return nullptr;
    const auto original=originalGetter;
    if(!original||original==cachedGetter)
    {++publicationFault;return nullptr;}
    ++forwarded;
    const auto value=original(name); // exactly one original invocation in this control
    return name&&std::strcmp(name,"call")==0&&value==reinterpret_cast<void*>(originalCallback)
        ?reinterpret_cast<void*>(&Callback):value;
}
static void Worker()
{
    while(!stop.load())
    {
        std::shared_lock guard(callerGate);
        // ALL exported getter users in this authored process pass this real gate.
        const bool active=ready.load();
        const auto result=reinterpret_cast<FixtureCallback>(cachedGetter("call"));
        if(!result||result(3)!=1003||result!=(active?&Callback:originalCallback))++bad;
        if(active)++afterCalls;else ++beforeCalls;
    }
}
static LONG AbortFailedPreparation(LONG result)
{
    if(result==NO_ERROR)return result;
    const auto aborted=DetourTransactionAbort();
    return aborted==NO_ERROR?result:aborted;
}
static LONG Attach()
{
    auto result=DetourTransactionBegin();if(result!=NO_ERROR)return result;
    result=DetourUpdateThread(GetCurrentThread());
    if(result!=NO_ERROR)return AbortFailedPreparation(result);
    result=DetourAttach(reinterpret_cast<PVOID*>(&originalGetter),reinterpret_cast<PVOID>(&Getter));
    if(result!=NO_ERROR)return AbortFailedPreparation(result);
    return DetourTransactionCommit();
}
static LONG Detach()
{
    auto result=DetourTransactionBegin();if(result!=NO_ERROR)return result;
    result=DetourUpdateThread(GetCurrentThread());
    if(result!=NO_ERROR)return AbortFailedPreparation(result);
    result=DetourDetach(reinterpret_cast<PVOID*>(&originalGetter),reinterpret_cast<PVOID>(&Getter));
    if(result!=NO_ERROR)return AbortFailedPreparation(result);
    return DetourTransactionCommit();
}

int wmain(int argc,wchar_t** argv)
{
    HMODULE library=nullptr;
    std::array<std::thread,2> workers;
    bool attached=false;
    Registry registry;
    try
    {
        if(argc!=2)throw std::runtime_error("supply the owned existing M1 fixture directory");
        const std::wstring path=std::wstring(argv[1])+L"\\NrCommonFixtureA.dll";
        library=LoadLibraryW(path.c_str());Check(library!=nullptr,"existing authored M1 DLL loaded; no producer rebuild");
        // This getter is cached BEFORE interceptor/epoch initialization. No
        // subsequent GetProcAddress is permitted to replace this host variable.
        cachedGetter=reinterpret_cast<FixtureGetter>(GetProcAddress(library,"FixtureGetFunction"));
        Check(cachedGetter!=nullptr,"host cached real getter before interceptor initialization");
        originalCallback=reinterpret_cast<FixtureCallback>(cachedGetter("call"));
        Check(originalCallback&&originalCallback(1)==1001,"pre-initialization cached getter returns exact upstream callback");
        const auto rawStartup=cachedGetter("slOnPluginStartup");
        const auto rawShutdown=cachedGetter("slOnPluginShutdown");
        Check(registry.Start(),"epoch observer initializes after host cached getter");
        std::array<Epoch*,2> elected{};
        FactoryBarrier factory;
        std::thread firstFactory([&]{elected[0]=registry.Observe(reinterpret_cast<const void*>(cachedGetter),&FactoryBarrier::Meet,&factory);});
        std::thread secondFactory([&]{elected[1]=registry.Observe(reinterpret_cast<const void*>(cachedGetter),&FactoryBarrier::Meet,&factory);});
        firstFactory.join();secondFactory.join();
        owner=elected[0];
        Check(factory.arrived.load()==2&&owner&&owner==elected[1],
              "two real pinned factories race publication and elect ONE actual live image epoch");
        Check(registry.Observe(reinterpret_cast<const void*>(cachedGetter))==owner,
              "later genuine lookup reuses that canonical live epoch without rebinding");
        originalGetter=cachedGetter;
        std::array<unsigned char,16> preimage{};
        std::memcpy(preimage.data(),reinterpret_cast<const void*>(cachedGetter),preimage.size());
        Check(DetourTransactionBegin()==NO_ERROR,"actual checked transaction begins for failure control");
        Check(DetourUpdateThread(GetCurrentThread())==NO_ERROR,"actual current-thread enlistment in owned empty-caller control");
        auto untouched=originalGetter;
        Check(DetourAttach(reinterpret_cast<PVOID*>(&untouched),nullptr)!=NO_ERROR,"actual invalid attach fails before publication");
        Check(DetourTransactionAbort()==NO_ERROR&&untouched==cachedGetter&&
              std::memcmp(preimage.data(),reinterpret_cast<const void*>(cachedGetter),preimage.size())==0,
              "failed preparation abort retains exact original pointer and entry bytes");
        workers[0]=std::thread(&Worker);workers[1]=std::thread(&Worker);
        const auto start=GetTickCount64();
        while(beforeCalls.load()<2&&GetTickCount64()-start<5000)Sleep(1);
        if(beforeCalls.load()<2)throw std::runtime_error("owned cached getter callers did not run before activation");
        Check(true,"real workers call only cached getter before activation");
        {
            // An actual caller-owned rendezvous, outside DllMain/notification.
            // No suspended thread or frozen allocator owner is manufactured.
            std::unique_lock exclusion(callerGate);
            Check(Attach()==NO_ERROR,"checked stock Detours activation under genuine all-caller exclusion");
            attached=true;
            Check(originalGetter!=cachedGetter,"actual original trampoline published before callers resume");
            ready.store(true);
        }
        const auto activated=GetTickCount64();
        while(afterCalls.load()<100&&GetTickCount64()-activated<5000)Sleep(1);
        stop.store(true);for(auto& thread:workers)if(thread.joinable())thread.join();
        Check(afterCalls.load()>=100&&bad.load()==0&&publicationFault.load()==0,
              "future calls through PRE-cached raw getter see complete typed interception after entry activation");
        {
            std::shared_lock caller(callerGate);
            const auto count=forwarded.load();
            const auto wrapped=cachedGetter("call");
            Check(wrapped==reinterpret_cast<void*>(&Callback)&&forwarded.load()==count+1&&Callback(5)==1005,
                  "one cached getter invocation forwards once and returns the intended callable wrapper");
            Check(cachedGetter(nullptr)==nullptr&&cachedGetter("unrelated")==nullptr&&
                  cachedGetter("slOnPluginStartup")==rawStartup&&cachedGetter("slOnPluginShutdown")==rawShutdown,
                  "null/unrelated and untouched raw lifecycle pointers are preserved");
        }
        {
            std::unique_lock exclusion(callerGate);
            Check(Detach()==NO_ERROR,"physical detach uses the SAME genuine caller exclusion, not a zero counter guess");
            attached=false;ready.store(false);
            Check(originalGetter==cachedGetter&&cachedGetter("call")==reinterpret_cast<void*>(originalCallback),
                  "actual detached pointer and cached entry return to their upstream identity");
        }
        Check(Callback(6)==1006,"issued callback does not depend on retired getter trampoline allocation");
        Check(FreeLibrary(library)!=FALSE,"owned host releases actual provider image");library=nullptr;
        Check(owner->retired.load()&&Callback(7)==0,"our issued callback tombstone refuses after actual unload");
        Check(registry.Stop(),"observer stops after owned threads/calls/provider drained");
        std::printf("CACHED_GETTER_CONTROL checks=%u PASS before=%u after=%u originalForwards=%u actualHostRendezvous=1 productionRendezvousEstablished=0 suspendAll=0 privateProvider=0\n",
                    checks,beforeCalls.load(),afterCalls.load(),forwarded.load());
        return 0;
    }
    catch(const std::exception& error)
    {
        stop.store(true);for(auto& thread:workers)if(thread.joinable())thread.join();
        if(attached){std::unique_lock exclusion(callerGate);Detach();}
        if(library)FreeLibrary(library);
        std::fprintf(stderr,"CACHED_GETTER_CONTROL FAIL: %s\n",error.what());return 1;
    }
}
