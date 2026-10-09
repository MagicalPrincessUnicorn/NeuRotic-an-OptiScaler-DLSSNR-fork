#pragma once
#include "ExperimentalMfgPolicy.h"
#include <array>
#include <span>
namespace Neurotic::Mfg::Experimental {
// A provider already present when the host first sees the renderer has no
// proven pre-use history. Only a newly observed load with a retained Create
// observer can authorize the unsubmitted preparation transaction.
class EarlyCoverage {
    std::array<uintptr_t,1024> prior{};
    size_t count=0;
    uintptr_t covered=0;
    bool started=false,valid=false;
public:
    void Begin(std::span<const uintptr_t> modules) noexcept {
        if(valid)return;
        if(modules.size()>prior.size())return;
        count=modules.size();for(size_t i=0;i<count;++i)prior[i]=modules[i];valid=true;
    }
    bool Fresh(uintptr_t module) const noexcept {
        if(!valid || !module || started)return false;
        for(size_t i=0;i<count;++i)if(prior[i]==module)return false;
        return true;
    }
    void Covered(uintptr_t module,bool installed) noexcept {if(installed && Fresh(module) && !covered)covered=module;}
    bool CanPrepare(uintptr_t module) const noexcept {return valid && !started && covered==module && module;}
    void Create() noexcept {started=true;}
};
inline bool CanReport(Stage current,Stage next) noexcept {
    return current!=Stage::Poisoned || next==Stage::Poisoned;
}
inline bool MayPoisonDevice(Family family,bool backendLoaded) noexcept {
    return family!=Family::None && backendLoaded;
}
inline bool NewProviderNeedsValidation(uintptr_t module,uintptr_t provider,bool dlssgExports) noexcept {
    return module && module!=provider && dlssgExports;
}
inline bool IsAdmitted(Stage stage,bool prepared) noexcept {
    return prepared && (stage==Stage::Ready || stage==Stage::ActivityObserved);
}
// A prepared provider belongs only to synchronous game-owned Streamline FG.
// Nested SR/RR calls clear this scope; worker threads never inherit it.
class GameFgScope {
    inline static thread_local bool current=false;
    bool previous;
public:
    explicit GameFgScope(bool allowed) noexcept:previous(current){current=allowed;}
    ~GameFgScope(){current=previous;}
    static bool Current() noexcept{return current;}
    GameFgScope(const GameFgScope&)=delete;
    GameFgScope& operator=(const GameFgScope&)=delete;
};
inline bool MayEnterVendor(Family family,Stage stage,bool loaded,bool prepared,bool gameScope,bool off=false) noexcept {
    if(family==Family::None)return true;
    if(stage==Stage::Poisoned)return false;
    if(!loaded)return true; // Untouched native paths remain untouched.
    if(off)return true; // Safe ordinary disable remains available after refusal.
    return IsAdmitted(stage,prepared) && gameScope;
}
inline uint32_t SelectableCeiling(bool prepared,uint32_t observed) noexcept {
    return prepared && observed>=1 && observed<=5 ? observed : 0;
}

template<class Options> void RestoreBorrowedOptions(Options& target,const Options& source) noexcept {
    target.structVersion=source.structVersion;target.onErrorCallback=source.onErrorCallback;target.next=source.next;
}
class ThreadScope {
    inline static thread_local uintptr_t current=0;
    uintptr_t previous=0;
public:
    explicit ThreadScope(uintptr_t allowed) noexcept:previous(current){current=allowed;}
    ~ThreadScope(){current=previous;}
    static uintptr_t Current() noexcept{return current;}
    ThreadScope(const ThreadScope&)=delete;
    ThreadScope& operator=(const ThreadScope&)=delete;
};
}
