// Actual authored callbacks and production text owner; no product entry engine.
#include "FixtureApi.h"
#include "ModuleEpochProbe.h"
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <array>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <thread>

using CommonModuleProbe::Epoch;
using CommonModuleProbe::ImageLease;
using CommonModuleProbe::Registry;
static Neurotic::Hooks::StreamlineOwnedJson text;
struct Route { Epoch* owner=nullptr;FixtureLoad original=nullptr; };
static std::array<Route,4> routes;
static unsigned used=0,checks=0;
static void Check(bool value,const char* message)
{if(!value)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
template<size_t I>static bool Invoke(void* params,const char* loader,const char** output)
{
    const auto& route=routes[I];
    auto hold=ImageLease::Acquire(route.owner,reinterpret_cast<const void*>(route.original));
    if(!hold)return false;
    // Preserve every actual input and the original success/failure/null output.
    const bool result=route.original(params,loader,output);
    if(!result||!output||!*output)return result;
    // Contain only the ownership allocation failure. Never report an owned
    // success while letting the provider's borrowed pointer escape this lease.
    // Provider exceptions retain their original behavior and unwind the hold.
    try{*output=text.Publish(std::string(*output));}catch(...){*output=nullptr;return false;}
    return result;
}
static const std::array<FixtureLoad,4> wrappers={&Invoke<0>,&Invoke<1>,&Invoke<2>,&Invoke<3>};
static FixtureLoad Bind(Registry& registry,FixtureLoad original)
{
    if(!original)return nullptr;
    auto* owner=registry.Observe(reinterpret_cast<const void*>(original));
    Check(owner!=nullptr,"actual returned callback code owner observed, no facade inferred");
    // Serial fixture election only. Production concurrent callback publication
    // remains separate; the module factory has its own real concurrency control.
    for(unsigned i=0;i<used;++i)
        if(routes[i].owner==owner&&routes[i].original==original)return wrappers[i];
    if(used==routes.size())throw std::runtime_error("authored fixture's four route examples exhausted");
    const auto index=used++;routes[index]={owner,original};return wrappers[index];
}
struct Library
{
    HMODULE value=nullptr;
    explicit Library(const std::wstring& path):value(LoadLibraryW(path.c_str()))
    {if(!value)throw std::runtime_error("owned fixture load failed");}
    ~Library(){if(value)FreeLibrary(value);}
    void Close(){if(value){Check(FreeLibrary(value)!=FALSE,"actual authored image host reference released");value=nullptr;}}
    FixtureGetter Getter(const char* name="FixtureGetFunction")const
    {
        const auto result=reinterpret_cast<FixtureGetter>(GetProcAddress(value,name));
        if(!result)throw std::runtime_error("owned fixture getter missing");return result;
    }
};
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("supply unchanged owned M1 fixture output directory");
        const std::wstring root=argv[1],pathA=root+L"\\NrCommonFixtureA.dll",pathB=root+L"\\NrCommonFixtureB.dll";
        Registry registry;Check(registry.Start(),"actual loader epoch observer active");
        Library a(pathA),b(pathB);
        auto getA=a.Getter(),getB=b.Getter(),forwarded=a.Getter("FixtureForwardedGetter");
        Check(forwarded==getB,"genuine facade A forwarding resolves the SAME physical getter B");
        auto rawA=reinterpret_cast<FixtureLoad>(getA("slOnPluginLoad"));
        auto rawB=reinterpret_cast<FixtureLoad>(getB("slOnPluginLoad"));
        const auto wrapperA=Bind(registry,rawA),wrapperB=Bind(registry,rawB);
        const auto facadeB=Bind(registry,reinterpret_cast<FixtureLoad>(forwarded("slOnPluginLoad")));
        Check(wrapperB==facadeB,"same actual callback/epoch deduplicates independent of facade provenance");
        Check(Bind(registry,nullptr)==nullptr,"actual absent callback remains null");
        const char *rawJsonA=nullptr,*rawJsonB=nullptr,*firstA=nullptr,*firstB=nullptr;
        Check(rawA(nullptr,"{}",&rawJsonA)&&rawB(nullptr,"{}",&rawJsonB),"actual authored original callbacks supply reference JSON");
        Check(wrapperA(nullptr,"{}",&firstA)&&wrapperB(nullptr,"{}",&firstB),"both independently bound actual callbacks execute");
        Check(firstA!=rawJsonA&&firstB!=rawJsonB&&std::string(firstA)==rawJsonA&&std::string(firstB)==rawJsonB,
              "actual returned texts copied exactly into independent owned allocations");
        const char* secondA=nullptr;Check(wrapperA(nullptr,"{}",&secondA)&&secondA!=firstA,
              "repeated result never overwrites an earlier issued buffer");
        Check(std::string(firstA)=="{\"fixture\":\"A\"}"&&std::string(firstB)=="{\"fixture\":\"B\"}",
              "later same/other-origin calls preserve both original issued values");
        Check(!wrapperA(nullptr,"{}",nullptr),"original false/null-output failure is preserved without fabricated text");
        std::array<const char*,2> parallel{};
        std::array<bool,2> success{};
        std::jthread first([&]{success[0]=wrapperA(nullptr,"{}",&parallel[0]);});
        std::jthread second([&]{success[1]=wrapperB(nullptr,"{}",&parallel[1]);});
        first.join();second.join();
        Check(success[0]&&success[1]&&parallel[0]!=parallel[1]&&std::string(parallel[0])==rawJsonA&&std::string(parallel[1])==rawJsonB,
              "simultaneous authored origins publish stable owned text without shared mutable buffer");
        a.Close();
        const char* untouched="caller sentinel";
        Check(!wrapperA(nullptr,"{}",&untouched)&&std::string(untouched)=="caller sentinel",
              "retired callback refuses before original invocation and preserves caller output");
        const char* afterFacade=nullptr;
        Check(facadeB(nullptr,"{}",&afterFacade)&&std::string(afterFacade)=="{\"fixture\":\"B\"}",
              "facade A unload does NOT veto independently live actual callback B");
        Check(std::string(firstA)=="{\"fixture\":\"A\"}","owned old A text remains valid after its provider unload");
        const auto previousB=b.value;b.Close();
        Check(std::string(firstB)=="{\"fixture\":\"B\"}","owned old B text remains valid after its provider unload");
        Library reloaded(pathB);
        const auto fresh=Bind(registry,reinterpret_cast<FixtureLoad>(reloaded.Getter()("slOnPluginLoad")));
        std::printf("JSON_ROUTE_RELOAD oldB=%p newB=%p\n",static_cast<void*>(previousB),static_cast<void*>(reloaded.value));
        Check(fresh!=wrapperB,"new B image epoch gets a fresh immutable wrapper identity");
        const char* stale="old output sentinel";const char* current=nullptr;
        Check(!wrapperB(nullptr,"{}",&stale)&&std::string(stale)=="old output sentinel"&&fresh(nullptr,"{}",&current)&&
              std::string(current)=="{\"fixture\":\"B\"}","old callback never rebinds while new callback remains usable");
        reloaded.Close();Check(registry.Stop(),"observer stopped after actual owned calls/images drained");
        std::printf("OWNED_CALLBACK_JSON checks=%u PASS productionTextOwner=1 productEntryEngine=0 concurrentCallbackFactory=0 privateProvider=0 GPU=0\n",checks);
        return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"OWNED_CALLBACK_JSON FAIL: %s\n",error.what());return 1;}
}
