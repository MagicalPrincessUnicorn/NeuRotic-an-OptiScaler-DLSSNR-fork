#include "FixtureApi.h"
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
enum class FGNvngxReplacement {None,Other};
struct State
{
    FGNvngxReplacement activeFgNvngx=FGNvngxReplacement::Other;
    static State& Instance(){static State state;return state;}
};
static unsigned bindCalls=0;
static const void* boundOriginal=nullptr;
static std::atomic<unsigned> prepareCalls{0};
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
static HANDLE preparationEntered=nullptr,preparationRelease=nullptr;
static std::atomic<bool> blockPreparation{false};
static std::atomic<unsigned> projectedOriginals{0};
#endif
static void bindNativeMfgWrapper(const void* original){++bindCalls;boundOriginal=original;}
static void prepareNativeMfgCapabilities()
{
    prepareCalls.fetch_add(1);
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
    if(blockPreparation.load())
    {
        if(!SetEvent(preparationEntered)||WaitForSingleObject(preparationRelease,10000)!=WAIT_OBJECT_0)
            throw std::runtime_error("owned preparation coordination failed");
    }
#endif
}
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
#include "../../../OptiScaler/hooks/StreamlineOnLoadBindings.h"
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <array>
#include <thread>
using Invocation=Neurotic::Hooks::StreamlineOnLoadInvocation<void>;
struct InvocationReceipt {FixtureLoad original=nullptr;void* params=nullptr;const void* binding=nullptr;const Neurotic::Hooks::StreamlineModuleEpochs::Epoch* codeEpoch=nullptr;};
static thread_local InvocationReceipt lastInvocation;
static void RecordInvocation(const Invocation& value){lastInvocation={value.original,value.parameters,value.binding,value.codeEpoch};}
static void FixtureWarning(const char*,const char* feature,const char* reason){std::printf("BINDING_WARNING %s %s\n",feature,reason);}
#define LOG_WARN FixtureWarning
#endif
#include "dlssg_onload_origins.inc"
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
#undef LOG_WARN
static std::array<FixtureLoad,2> cachedEntries{},liveEntries{};
static unsigned routeIndex=0;
static void* MissingGetter(const char*){return nullptr;}
#endif
static unsigned checks=0,failures=0;
static void Require(bool ok,const char* message){if(!ok)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
struct Library
{
    HMODULE image=nullptr;
    explicit Library(const std::wstring& path):image(LoadLibraryW(path.c_str())){if(!image)throw std::runtime_error("authored fixture load failed");}
    ~Library(){if(image)FreeLibrary(image);}
    void Close(){if(image){const auto value=image;image=nullptr;if(!FreeLibrary(value))throw std::runtime_error("fixture release failed");}}
    FixtureGetter Getter()const
    {
        const auto value=reinterpret_cast<FixtureGetter>(GetProcAddress(image,"FixtureGetFunction"));
        if(!value)throw std::runtime_error("authored fixture getter missing");return value;
    }
};
template<class Projection>
static void Route(const char* name,bool game,FixtureGetter a,FixtureGetter b)
{
    const unsigned initialBinds=bindCalls,initialPrepare=prepareCalls.load();
    Projection::Select(a);const auto issuedA=reinterpret_cast<FixtureLoad>(Projection::Get("slOnPluginLoad"));
    if(game)Require(bindCalls==initialBinds+1&&boundOriginal==a("slOnPluginLoad"),"game getter binds the exact raw A original before exposing callback");
    const char* first=nullptr;Require(issuedA&&issuedA(nullptr,"{}",&first)&&first&&std::strcmp(first,"{\"fixture\":\"A\"}")==0,"first DLSSG A callback invokes actual authored A");
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
    const auto contextA=lastInvocation;
    Require(contextA.original&&contextA.binding&&contextA.codeEpoch,"DLSSG callback receives its actual immutable owner context");
#endif
    Projection::Select(b);const auto issuedB=reinterpret_cast<FixtureLoad>(Projection::Get("slOnPluginLoad"));
    if(game)Require(bindCalls==initialBinds+2&&boundOriginal==b("slOnPluginLoad"),"game getter binds the exact raw B original before exposing callback");
    const char* second=nullptr;Require(issuedB&&issuedB(nullptr,"{}",&second)&&second&&std::strcmp(second,"{\"fixture\":\"B\"}")==0,"new DLSSG B callback invokes actual authored B");
    const char* cached=nullptr;const bool result=issuedA(nullptr,"{}",&cached);
    std::printf("DLSSG_ORIGIN route=%s cachedA_success=%u actual=%s\n",name,static_cast<unsigned>(result),cached?cached:"<null>");
    if(!result||!cached||std::strcmp(cached,"{\"fixture\":\"A\"}")!=0){++failures;std::printf("FAIL cached %s A callback retargeted after B publication\n",name);}
    else{++checks;std::printf("PASS cached %s A retains actual original\n",name);}
    if(game)Require(prepareCalls.load()==initialPrepare+3,"game preparation call count is preserved for all three original invocations");
    else
    {
        Require(bindCalls==initialBinds&&prepareCalls.load()==initialPrepare,"local callback does not acquire game MFG binding or preparation side effects");
        State::Instance().activeFgNvngx=FGNvngxReplacement::None;Projection::Select(a);
        Require(Projection::Get("slOnPluginLoad")==a("slOnPluginLoad"),"existing local inactive rule returns the exact upstream callback");
        State::Instance().activeFgNvngx=FGNvngxReplacement::Other;
    }
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
    Require(lastInvocation.binding==contextA.binding&&lastInvocation.codeEpoch==contextA.codeEpoch&&lastInvocation.original==contextA.original,"cached DLSSG A retains its immutable context after B publication");
    int params=9;const char* current=nullptr;
    Require(issuedA(&params,"current params",&current)&&lastInvocation.params==&params,"DLSSG invocation carries the actual current borrowed parameters");
    if(game)Require(prepareCalls.load()==initialPrepare+4,"game preparation count remains one per additional bound original invocation");
    cachedEntries[routeIndex]=issuedA;liveEntries[routeIndex]=issuedB;++routeIndex;
    Projection::Select(MissingGetter);Require(!Projection::Get("slOnPluginLoad"),"missing DLSSG callback remains null without synthetic success");Projection::Select(b);
#endif
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("supply unchanged M1 fixture directory");const std::wstring root=argv[1];
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
        CommonFixtureLedger ledger;
#endif
        Library a(root+L"\\NrCommonFixtureA.dll"),b(root+L"\\NrCommonFixtureB.dll");
        Route<GameProjection>("game",true,a.Getter(),b.Getter());Route<LocalProjection>("local",false,a.Getter(),b.Getter());
#ifdef NR_DLSSG_ONLOAD_ORIGINS_GREEN
        Require(cachedEntries[0]!=cachedEntries[1],"game and local DLSSG use independent compiled binding domains");
        const auto configure=reinterpret_cast<FixtureConfigure>(GetProcAddress(a.image,"FixtureConfigureLedger"));
        Require(configure!=nullptr,"actual authored provider detach ledger available");configure(&ledger);
        preparationEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);preparationRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Require(preparationEntered&&preparationRelease,"owned pre-original preparation coordination events created");
        const auto beforeOriginal=projectedOriginals.load();blockPreparation.store(true);
        bool blockedResult=false;const char* blockedOutput=nullptr;
        std::jthread worker([&]{try{blockedResult=cachedEntries[0](nullptr,"{}",&blockedOutput);}catch(...){blockedResult=false;}});
        const bool entered=WaitForSingleObject(preparationEntered,10000)==WAIT_OBJECT_0;
        const bool notYetOriginal=projectedOriginals.load()==beforeOriginal;
        a.Close();
        const bool held=InterlockedCompareExchange(&ledger.detached,0,0)==0;
        SetEvent(preparationRelease);worker.join();blockPreparation.store(false);
        CloseHandle(preparationEntered);CloseHandle(preparationRelease);preparationEntered=nullptr;preparationRelease=nullptr;
        Require(entered&&notYetOriginal,"recorded preparation blocks before the actual original call site");
        Require(held,"temporary code lease holds actual A while pre-original preparation is blocked");
        Require(blockedResult&&blockedOutput&&std::strcmp(blockedOutput,"{\"fixture\":\"A\"}")==0&&
                projectedOriginals.load()==beforeOriginal+1&&InterlockedCompareExchange(&ledger.detached,0,0)==1,
                "actual original and owned output finish before final provider detach without permanent pin");
        for(std::size_t i=0;i<cachedEntries.size();++i)
        {
            const auto before=prepareCalls.load();const char* output="caller sentinel";
            Require(!cachedEntries[i](nullptr,"{}",&output)&&std::strcmp(output,"caller sentinel")==0&&prepareCalls.load()==before,"retired A callback performs no original or MFG preparation work");
            Require(liveEntries[i](nullptr,"{}",&output)&&output&&std::strcmp(output,"{\"fixture\":\"B\"}")==0,"independently live DLSSG B remains callable after A unload");
        }
        Neurotic::Runtime::RequestProcessExit();
        for(const auto entry:liveEntries){const auto before=prepareCalls.load();const char* output="caller sentinel";Require(!entry(nullptr,"{}",&output)&&std::strcmp(output,"caller sentinel")==0&&prepareCalls.load()==before,"process close performs no issued DLSSG original or preparation work");}
#endif
        std::printf("DLSSG_ONLOAD_ORIGINS checks=%u failures=%u mfgTransactionQualified=0 physicalEngine=0 privateProvider=0 GPU=0\n",checks,failures);return failures?1:0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"DLSSG_ONLOAD_ORIGINS ERROR: %s\n",e.what());return 2;}
}
