// Actual extracted Prepare initialization/availability boundary; external CPU
// availability/lifecycle/Initialize services are declared stubs, not GPU proof.
#include <cstdint>
#include <iostream>
#include <stdexcept>
#include <string>
#include <memory>
#include <mutex>
#include <thread>
struct ID3D12Device {};
static std::recursive_mutex g_lifecycleMutex;
static std::uint64_t g_lifecycleGeneration=1;
static bool g_sessionClosed=false,g_shutdownFailed=false;
struct ModelOwner {
    std::shared_ptr<int> providerUses;
    std::unique_ptr<int> nativeCreation;
    int capabilityDestruction=0;
} g_nr;
struct Retirement {
    bool permit=true;
    std::unique_ptr<int> NextModelCreation(const std::shared_ptr<int>&) {
        return permit?std::make_unique<int>(1):nullptr;
    }
};
static std::shared_ptr<Retirement> g_rendererRetirement;
static int g_rendererShutdownObservation=0;
// Actual owner accessors and NotifyDeviceInit control flow. Retirement factory
// is explicitly a CPU external stub; no completed GPU retirement is claimed.
#include "availability_lifecycle_under_test.inc"
static void CloseSessionForFixture() {
#include "availability_close_under_test.inc"
}
struct Runtime {
    std::mutex mutex;
    struct Device {void* Get(){return this;}} device12;
    bool nrChecked=false,nrAvailable=false,failed=false,deviceChanged=false;
    bool initReady=true;
    std::uint64_t nrResumeGeneration=0,nrLifecycleGeneration=0;
    bool nrSessionUnavailable=false;
    void* nrAvailabilityDevice=nullptr;
    std::string nrUnavailableReason;
    bool Initialize(void*,std::string& reason) {
        if(failed) {reason="completion lost; restart required";return false;}
        if(deviceChanged) {reason="device changed; restart required";return false;}
        if(!initReady) {reason="initialization unavailable";return false;}
        return true;
    }
};
struct State {
    bool nativePostSr=true,nativePreSr=false,rejected=false;
    std::string reason;
    void Reject(const std::string& text){rejected=true;reason=text;}
};
struct Settings {std::uint64_t resumeGeneration=1;};
static unsigned probes=0,checks=0,failures=0;
static bool available=false,throwProbe=false;
static auto& lifecycle=g_lifecycleGeneration;
static bool DirectD3D12Available(void*,const char** reason=nullptr){
    ++probes;
    const auto reject=[&](const char* text){if(reason)*reason=text;return false;};
#include "availability_session_refusal_under_test.inc"
    if(throwProbe) {throwProbe=false;throw std::runtime_error("probe exception");}
    if(reason)*reason=available?nullptr:"chosen DLL is missing its required export";
    return available;
}
static void PrepareBranch(Runtime& runtime,Settings runtimeSettings,State& s) {
    void* context=nullptr;
#include "availability_under_test.inc"
}
static State Prepare(Runtime& runtime,Settings settings) {State s;PrepareBranch(runtime,settings,s);return s;}
static void Check(bool value,const char* text){++checks;if(!value){++failures;std::cout<<"FAIL "<<text<<'\n';}else std::cout<<"PASS "<<text<<'\n';}
namespace Dx11Transport {
struct ContextLock {
    explicit ContextLock(void*) {
        bool ownerWasFree=false;
        std::thread competitor([&] {
            ownerWasFree=g_lifecycleMutex.try_lock();
            if(ownerWasFree)g_lifecycleMutex.unlock();
        });
        competitor.join();
        if(ownerWasFree)throw std::runtime_error("context entered before lifecycle owner");
    }
};
}
static Runtime* activeRuntime=nullptr;
static Runtime& Transport(){return *activeRuntime;}
static bool PrepareLockOrder(){
    Runtime input;activeRuntime=&input;void* context=nullptr;
#include "availability_prepare_locks_under_test.inc"
    return true;
}
static bool CompleteLockOrder(){
    Runtime input;activeRuntime=&input;void* context=nullptr;
#include "availability_complete_locks_under_test.inc"
    return true;
}
static void Reset(){probes=0;available=false;throwProbe=false;lifecycle=1;g_sessionClosed=false;g_shutdownFailed=false;g_rendererRetirement.reset();g_nr={};}
int main(){
    Reset();Runtime runtime;Settings settings;
    Check(Prepare(runtime,settings).rejected&&probes==1,"initial unavailable is cached");
    available=true;
    Check(Prepare(runtime,settings).rejected&&probes==1,"unchanged facts avoid per-frame probe");
    ++settings.resumeGeneration;
    Check(!Prepare(runtime,settings).rejected&&probes==2,"resume change retries negative");
    Check(!Prepare(runtime,settings).rejected&&probes==2,"positive is reused within epoch");
    available=false;++settings.resumeGeneration;
    Check(Prepare(runtime,settings).rejected&&probes==3,"next resume observes removal");

    Reset();Runtime changed;Settings sameResume;Prepare(changed,sameResume);available=true;++lifecycle;
    Check(!Prepare(changed,sameResume).rejected&&probes==2,"provider lifecycle change retries without resume change");

    Reset();Runtime specific;
    Check(Prepare(specific,{}).reason=="chosen DLL is missing its required export","actual capability failure is propagated");

    Reset();Runtime interrupted;throwProbe=true;
    try {Prepare(interrupted,{});}catch(const std::runtime_error&){}
    available=true;
    Check(!Prepare(interrupted,{}).rejected&&probes==2,"exception does not commit incomplete probe result");

    Reset();Runtime quarantined;quarantined.failed=true;Settings resumed;resumed.resumeGeneration=9;lifecycle=9;
    Check(Prepare(quarantined,resumed).rejected&&probes==0&&quarantined.failed,
        "initialization quarantine refusal stays before availability on toggle");
    Reset();Runtime moved;moved.deviceChanged=true;
    Check(Prepare(moved,{}).rejected&&probes==0,"device ownership refusal stays before availability");
    Reset();Runtime present;State presentOnly;presentOnly.nativePostSr=false;
    PrepareBranch(present,{},presentOnly);
    Check(!presentOnly.rejected&&probes==0,"Present-only boundary does not invoke Native availability");
    Reset();Runtime retry;retry.initReady=false;
    const auto first=Prepare(retry,{});retry.initReady=true;available=true;
    Check(first.rejected&&!Prepare(retry,{}).rejected&&probes==1,"unsubmitted preparation can retry without resetting quarantine");
    Reset();Runtime reopened;available=true;ID3D12Device device;
    Check(!Prepare(reopened,{}).rejected&&probes==1,"session begins open");
    CloseSessionForFixture();
    Check(Prepare(reopened,{}).reason=="NR session is closed; restart required"&&probes==2,
        "actual shutdown transition caches closed refusal");
    g_rendererRetirement=std::make_shared<Retirement>();NotifyDeviceInit(&device);
    Check(!g_sessionClosed&&lifecycle==2&&!Prepare(reopened,{}).rejected&&probes==3,
        "actual successful reopen invalidates closed refusal at unchanged generation and resume");
    Reset();Runtime missingRetirement;available=true;CloseSessionForFixture();Prepare(missingRetirement,{});
    NotifyDeviceInit(&device);
    Check(g_sessionClosed&&Prepare(missingRetirement,{}).rejected&&probes==1,"missing retirement cannot reopen or trigger probes");
    g_rendererRetirement=std::make_shared<Retirement>();g_rendererRetirement->permit=false;NotifyDeviceInit(&device);
    Check(g_sessionClosed&&Prepare(missingRetirement,{}).rejected&&probes==1,"denied next creation cannot reopen or trigger probes");
    Reset();Runtime shutdownFailure;available=true;Prepare(shutdownFailure,{});g_shutdownFailed=true;
    NotifyDeviceInit(&device);
    Check(g_shutdownFailed&&Prepare(shutdownFailure,{}).rejected&&probes==2,
        "shutdown failure cannot reuse cached availability or reopen");
    Reset();Runtime stillOpen;available=true;Prepare(stillOpen,{});NotifyDeviceInit(&device);
    Check(!g_sessionClosed&&!Prepare(stillOpen,{}).rejected&&probes==1,"ordinary device notification does not invalidate unchanged facts");
    try {Check(PrepareLockOrder(),"actual Prepare acquires lifecycle before context and transport");}
    catch(const std::exception&) {Check(false,"actual Prepare acquires lifecycle before context and transport");}
    try {Check(CompleteLockOrder(),"actual Complete acquires lifecycle before context and transport");}
    catch(const std::exception&) {Check(false,"actual Complete acquires lifecycle before context and transport");}
    std::cout<<"checks="<<checks<<" failures="<<failures<<'\n';return failures?1:0;
}
