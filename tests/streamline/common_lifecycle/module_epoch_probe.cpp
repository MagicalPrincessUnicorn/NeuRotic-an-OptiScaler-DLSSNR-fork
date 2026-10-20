#include "FixtureApi.h"
#include "ModuleEpochProbe.h"
#include <array>
#include <cstdio>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

using CommonModuleProbe::Epoch;
using CommonModuleProbe::ImageLease;
using CommonModuleProbe::Registry;
static unsigned checks=0;
static void Check(bool value,const char* text)
{
    if(!value)throw std::runtime_error(text);
    ++checks;std::printf("PASS %s\n",text);
}
struct Library
{
    HMODULE value=nullptr;
    explicit Library(const std::wstring& path):value(LoadLibraryW(path.c_str()))
    {if(!value)throw std::runtime_error("owned fixture LoadLibrary failed");}
    Library(const Library&)=delete;
    ~Library(){if(auto prior=std::exchange(value,nullptr))FreeLibrary(prior);}
    void Close(){if(auto prior=std::exchange(value,nullptr))Check(FreeLibrary(prior)!=FALSE,"actual host FreeLibrary succeeded");}
    template<class T>T Export(const char* name)const
    {
        const auto result=reinterpret_cast<T>(GetProcAddress(value,name));
        if(!result)throw std::runtime_error("required owned fixture export missing");return result;
    }
};
struct BoundRoute
{
    Epoch* source=nullptr;
    Epoch* target=nullptr;
    FixtureCallback original=nullptr;
    unsigned Invoke(unsigned argument)const
    {
        auto sourceHold=ImageLease::Acquire(source,source?source->image:nullptr);
        if(!sourceHold)return 0;
        auto targetHold=ImageLease::Acquire(target,reinterpret_cast<const void*>(original));
        if(!targetHold)return 0;
        return original(argument);
    }
};
static std::array<const BoundRoute*,4> issued{};
template<size_t I>static unsigned Wrapper(unsigned argument)
{return issued[I]?issued[I]->Invoke(argument):0;}

static BoundRoute Bind(Registry& registry,HMODULE source,FixtureGetter getter)
{
    auto* sourceEpoch=registry.Observe(reinterpret_cast<const void*>(source));
    auto* getterEpoch=registry.Observe(reinterpret_cast<const void*>(getter));
    auto sourceHold=ImageLease::Acquire(sourceEpoch,source);
    auto getterHold=ImageLease::Acquire(getterEpoch,reinterpret_cast<const void*>(getter));
    Check(bool(sourceHold)&&bool(getterHold),"actual source/getter owners held before foreign getter");
    Check(getter(nullptr)==nullptr&&getter("unrelated")==nullptr,"upstream null callbacks remain null");
    auto callback=reinterpret_cast<FixtureCallback>(getter("call"));
    auto* targetEpoch=registry.Observe(reinterpret_cast<const void*>(callback));
    Check(callback&&targetEpoch,"actual returned callback target has an observed owner");
    return {sourceEpoch,targetEpoch,callback};
}
struct Events
{
    CommonFixtureLedger ledger;
    Events()
    {
        ledger.enteredEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        ledger.releaseEvent=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        if(!ledger.enteredEvent||!ledger.releaseEvent)throw std::runtime_error("owned event creation failed");
    }
    ~Events(){if(ledger.enteredEvent)CloseHandle(ledger.enteredEvent);if(ledger.releaseEvent)CloseHandle(ledger.releaseEvent);}
    void Arm(){ResetEvent(ledger.enteredEvent);ResetEvent(ledger.releaseEvent);InterlockedExchange(&ledger.block,1);}
    void Release(){SetEvent(ledger.releaseEvent);InterlockedExchange(&ledger.block,0);}
};
struct ReloadInterleave
{
    Library* previous=nullptr;
    std::wstring path;
    CommonFixtureLedger* ledger=nullptr;
    std::unique_ptr<Library> replacement;
    static void Run(void* context)
    {
        auto& self=*static_cast<ReloadInterleave*>(context);
        self.previous->Close();
        self.replacement=std::make_unique<Library>(self.path);
        self.replacement->Export<FixtureConfigure>("FixtureConfigureLedger")(self.ledger);
    }
};

int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("supply the owned fixture output directory");
        const std::wstring directory=argv[1];
        const auto pathA=directory+L"\\NrCommonFixtureA.dll";
        const auto pathB=directory+L"\\NrCommonFixtureB.dll";
        Registry failure;
        Check(!failure.Start(0xffffffffu),"actual invalid notification flags fail registration");
        Check(failure.Observe(reinterpret_cast<const void*>(&Wrapper<0>))==nullptr,"unregistered observer publishes no authority");
        Registry registry;
        Check(registry.Start(),"actual public loader notifications registered");
        Events eventsA,eventsB;
        // Ledgers outlive every configured fixture module, including exception cleanup.
        Library a(pathA),b(pathB);
        a.Export<FixtureConfigure>("FixtureConfigureLedger")(&eventsA.ledger);
        b.Export<FixtureConfigure>("FixtureConfigureLedger")(&eventsB.ledger);
        const auto getterA=a.Export<FixtureGetter>("FixtureGetFunction");
        const auto getterB=b.Export<FixtureGetter>("FixtureGetFunction");
        const auto rawStartupA=getterA("slOnPluginStartup");
        const auto rawStartupB=getterB("slOnPluginStartup");
        const auto rawShutdownA=getterA("slOnPluginShutdown");
        Check(rawStartupA==reinterpret_cast<void*>(a.Export<FixtureStartup>("FixtureOnStartup"))&&
              rawStartupB==reinterpret_cast<void*>(b.Export<FixtureStartupHistorical>("FixtureOnStartup"))&&
              rawShutdownA==reinterpret_cast<void*>(a.Export<FixtureShutdown>("FixtureOnShutdown")),
              "untouched lifecycle getter values preserve exact modern/historical typed pointer identities");
        int historicalParameters=42;
        Check(reinterpret_cast<FixtureStartup>(rawStartupA)("{}",nullptr)&&
              reinterpret_cast<FixtureStartupHistorical>(rawStartupB)("{}",nullptr,&historicalParameters)&&
              b.Export<FixtureStartupParameters>("FixtureReadStartupParameters")()==&historicalParameters,
              "host invokes each raw Startup with its actual authored two/three-argument ABI");
        const char* rawJsonA=nullptr;const char* rawJsonB=nullptr;
        Check(reinterpret_cast<FixtureLoad>(getterA("slOnPluginLoad"))(nullptr,"{}",&rawJsonA)&&
              reinterpret_cast<FixtureLoad>(getterB("slOnPluginLoad"))(nullptr,"{}",&rawJsonB),
              "actual authored OnLoad callbacks return distinct upstream JSON");
        const std::string ownedJsonA=rawJsonA,ownedJsonB=rawJsonB;
        Check(ownedJsonA=="{\"fixture\":\"A\"}"&&ownedJsonB=="{\"fixture\":\"B\"}",
              "host copies both actual JSON results while their owned images remain live");
        const auto baseA=a.value;
        auto routeA=Bind(registry,a.value,a.Export<FixtureGetter>("FixtureGetFunction"));
        auto routeB=Bind(registry,b.value,b.Export<FixtureGetter>("FixtureGetFunction"));
        issued[0]=&routeA;issued[1]=&routeB;
        const FixtureCallback escapedA=&Wrapper<0>,escapedB=&Wrapper<1>;
        Check(escapedA(7)==1007&&escapedB(7)==2007&&escapedA(8)==1008,
              "issued old/new wrappers keep exact original targets and arguments");
        auto forwarded=Bind(registry,a.value,a.Export<FixtureGetter>("FixtureForwardedGetter"));
        issued[3]=&forwarded;
        Check(forwarded.source->image==a.value&&forwarded.target->image==b.value&&Wrapper<3>(9)==2009,
              "forwarded export retains distinct source and actual callback code owner");
        {
            Library duplicate(pathA);
            Check(duplicate.value==a.value,"duplicate host load references the same live image");
            duplicate.Close();
            Check(!routeA.source->retired.load()&&eventsA.ledger.detached==0&&escapedA(10)==1010,
                  "successful nonfinal FreeLibrary is not mistaken for physical unload");
        }
        a.Export<FixtureMutate>("FixtureSetState")(99);
        Check(a.Export<FixtureState>("FixtureReadState")()==99,"actual fixture mutable state changed before unload");
        a.Close();
        Check(routeA.source->retired.load()&&eventsA.ledger.detached==1&&GetModuleHandleW(pathA.c_str())==nullptr,
              "final host release actually detaches image and permanently revokes its epoch");
        Check(escapedA(11)==0&&Wrapper<3>(11)==0&&escapedB(11)==2011,
              "retired source wrappers refuse while unrelated actual target image remains live");
        Check(ownedJsonA=="{\"fixture\":\"A\"}"&&ownedJsonB=="{\"fixture\":\"B\"}",
              "owned returned JSON copies survive original image unload without reading expired raw pointers");
        Library reloaded(pathA);
        reloaded.Export<FixtureConfigure>("FixtureConfigureLedger")(&eventsA.ledger);
        std::printf("EPOCH_RELOAD first=%p reloaded=%p oldEpoch=%llu\n",static_cast<void*>(baseA),
                    static_cast<void*>(reloaded.value),static_cast<unsigned long long>(routeA.source->serial));
        Check(reloaded.value==baseA,"actual same-path reload reused the previous base address (ABA exercised)");
        Check(reloaded.Export<FixtureState>("FixtureReadState")()==1,"legitimate reload reinitializes provider globals without permanent image pin");
        auto routeReloaded=Bind(registry,reloaded.value,reloaded.Export<FixtureGetter>("FixtureGetFunction"));
        issued[2]=&routeReloaded;
        Check(routeReloaded.source!=routeA.source&&routeReloaded.source->serial!=routeA.source->serial,
              "same-address reload receives a distinct epoch instead of reviving old metadata");
        Check(escapedA(12)==0&&Wrapper<2>(12)==1012,"old escaped callback never redirects into a same-address replacement");

        // Mapping lifetime cannot establish logical-state lifetime. This authored
        // negative witness is not a claim that arbitrary concurrent Shutdown is
        // supported by a real plugin or by the prototype.
        eventsA.Arm();
        unsigned logicalResult=999;
        std::thread logical([&]{logicalResult=Wrapper<2>(13);});
        if(WaitForSingleObject(eventsA.ledger.enteredEvent,5000)!=WAIT_OBJECT_0)
        {eventsA.Release();logical.join();throw std::runtime_error("logical witness callback did not enter");}
        reloaded.Export<FixtureShutdown>("FixtureOnShutdown")();
        eventsA.Release();logical.join();
        Check(logicalResult==0&&!routeReloaded.source->retired.load(),
              "negative witness: live image reference does not prevent logical Shutdown state invalidation");
        Check(reloaded.Export<FixtureStartup>("FixtureOnStartup")("{}",nullptr),"actual fixture Startup restores authored logical state");

        eventsA.Arm();
        unsigned result=0;
        std::thread worker([&]{result=Wrapper<2>(14);});
        if(WaitForSingleObject(eventsA.ledger.enteredEvent,5000)!=WAIT_OBJECT_0)
        {eventsA.Release();worker.join();throw std::runtime_error("owned callback did not enter");}
        reloaded.Close();
        const bool held=GetModuleHandleW(pathA.c_str())!=nullptr&&!routeReloaded.source->retired.load()&&eventsA.ledger.detached==1;
        eventsA.Release();worker.join();
        Check(held&&result==1014,"temporary invocation references protect real in-flight callback code across host FreeLibrary");
        Check(eventsA.ledger.detached==2&&routeReloaded.source->retired.load()&&GetModuleHandleW(pathA.c_str())==nullptr,
              "last invocation release permits actual detach immediately after callback completion");
        Check(Wrapper<2>(15)==0,"completed retired callback cannot be acquired again");

        // Bootstrap an observer after a provider is already loaded; a temporary
        // reference protects publication without inventing a prior load event.
        Library preloaded(pathA);
        preloaded.Export<FixtureConfigure>("FixtureConfigureLedger")(&eventsA.ledger);
        Registry late;
        Check(late.Start(),"late observer registration succeeds for an already loaded image");
        auto* bootstrap=late.Observe(reinterpret_cast<const void*>(preloaded.value));
        Check(bootstrap&&!bootstrap->retired.load(),"held-reference bootstrap publishes an authentic live epoch");
        preloaded.Close();
        Check(bootstrap->retired.load()&&eventsA.ledger.detached==3,"bootstrapped epoch sees actual final unload without load-event backfill");
        Check(late.Stop(),"late observer unregistered after owned loader threads joined");

        // Force the actual TOCTOU boundary, rather than only calling a wrapper
        // after retirement has already completed. No private provider is used.
        Library changing(pathA);
        changing.Export<FixtureConfigure>("FixtureConfigureLedger")(&eventsA.ledger);
        auto changingRoute=Bind(registry,changing.value,changing.Export<FixtureGetter>("FixtureGetFunction"));
        const auto changingBase=changing.value;
        const auto callsBefore=eventsA.ledger.entered;
        ReloadInterleave interleave{&changing,pathA,&eventsA.ledger,{}};
        ImageLease::AcquireObservation observation;
        auto rejected=ImageLease::Acquire(changingRoute.source,reinterpret_cast<const void*>(changingRoute.original),
                                          &ReloadInterleave::Run,&interleave,&observation);
        std::printf("EPOCH_ACQUIRE_INTERLEAVE first=%p replacement=%p preLive=%u acquired=%u ownerMatches=%u postKnown=%u retired=%u\n",
                    static_cast<void*>(changingBase),static_cast<void*>(interleave.replacement->value),
                    unsigned(observation.initiallyLive),unsigned(observation.ownerAcquired),unsigned(observation.ownerMatches),
                    unsigned(observation.postRetirementKnown),unsigned(observation.postRetired));
        Check(interleave.replacement->value==changingBase,"controlled acquire interleave actually reuses the old image base");
        Check(!rejected&&observation.initiallyLive&&observation.ownerAcquired&&observation.ownerMatches&&
              observation.postRetirementKnown&&observation.postRetired&&eventsA.ledger.entered==callsBefore,
              "actual same-address new-image reference is rejected by permanent old-epoch POSTcheck without invoking it");
        auto freshAfterRace=Bind(registry,interleave.replacement->value,
                                interleave.replacement->Export<FixtureGetter>("FixtureGetFunction"));
        Check(freshAfterRace.source!=changingRoute.source&&freshAfterRace.Invoke(16)==1016&&changingRoute.Invoke(16)==0,
              "new epoch remains usable after rejected stale acquisition; old route never revives");
        interleave.replacement->Close();
        Check(eventsA.ledger.detached==5,"controlled interleave releases both real image incarnations without permanent holds");
        b.Close();
        Check(eventsB.ledger.detached==1&&routeB.source->retired.load(),"second independent image unload retires only its own routes");
        Check(registry.Stop(),"observer unregistered after all owned calls and modules drained");
        std::printf("COMMON_MODULE_EPOCH checks=%u PASS publicFixtures=2 sameAddressReuse=1 privateProvider=0 game=0 productRegistry=0 logicalShutdownGuarantee=0\n",checks);
        return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"COMMON_MODULE_EPOCH FAIL: %s\n",error.what());return 1;}
}
