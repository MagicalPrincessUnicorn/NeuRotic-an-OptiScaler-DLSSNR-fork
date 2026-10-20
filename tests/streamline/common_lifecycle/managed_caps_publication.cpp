// Actual public IParameters virtual interface, authored publication/fields only.
// No private provider or claim that a parameter pointer owns arbitrary data.
#include <cstdint>
#include <string>
#include <vector>
#include <cstring>
#include <cstdio>
#include <optional>
#include <stdexcept>
#include <sl.param/parameters.h>
#include "../../../OptiScaler/hooks/StreamlineScopedCaps.h"
struct Caps
{
    struct Adapter {std::uint32_t architecture=0,vendor=0;} adapters[8]{};
    std::uint32_t gpuCount=1,driverVersionMajor=555;
    bool hwsSupported=false;
};
struct Params final:sl::param::IParameters
{
    enum class Mode {Present,Missing,Null,FalseWithPointer,Throw};
    void* publication=nullptr;Mode mode=Mode::Present;mutable unsigned reads=0;
#define VALUE(T) void set(const char*,T)override{} bool get(const char*,T*)const override{return false;}
    VALUE(bool) VALUE(unsigned long long) VALUE(float) VALUE(double) VALUE(unsigned int) VALUE(int)
#undef VALUE
    void set(const char* key,void* value)override
    {if(std::strcmp(key,sl::param::common::kSystemCaps)==0)publication=value;}
    bool get(const char* key,void** value)const override
    {
        ++reads;if(mode==Mode::Throw)throw std::runtime_error("actual parameter query exception");
        if(std::strcmp(key,sl::param::common::kSystemCaps)!=0||mode==Mode::Missing)return false;
        *value=mode==Mode::Null?nullptr:publication;return mode!=Mode::FalseWithPointer;
    }
    std::vector<std::string> enumerate()const override{return {sl::param::common::kSystemCaps};}
};
#ifdef NR_CACHED_CAPS_READER_CONTROL
#include "cached_caps_reader.inc"
#else
#include "../../../OptiScaler/hooks/StreamlineManagedCaps.h"
#endif
static unsigned checks=0;
static void Check(bool value,const char* message)
{++checks;if(!value)throw std::runtime_error(message);std::printf("PASS %s\n",message);}
int main()
{
    try
    {
        Params params;Caps a,b;a.adapters[0]={0x170,0x10de};b.adapters[0]={0x180,0x1002};
        for(Caps* published:{&a,&b})
        {
            params.set(sl::param::common::kSystemCaps,static_cast<void*>(published));
            const auto before=published->adapters[0];
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;
#ifdef NR_CACHED_CAPS_READER_CONTROL
            scope.emplace(ReadCachedCaps(&params),0xffffffffu,0x10deu,true);
#else
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,&params,sl::param::common::kSystemCaps,
                [](const Caps&){return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{{0xffffffffu,0x10deu,true}};});
            Check(status==Neurotic::Hooks::ManagedCapsStatus::Applied,"actual parameter publication acquired for this callback");
#endif
            Check(published->adapters[0].architecture==0xffffffffu&&published->adapters[0].vendor==0x10de,
                  "current freshly published object, including replacement B, receives scoped spoof");
            scope.reset();
            Check(published->adapters[0].architecture==before.architecture&&published->adapters[0].vendor==before.vendor&&
                  published->driverVersionMajor==555&&!published->hwsSupported,"fresh object exact restoration before original callback returns");
        }
#ifndef NR_CACHED_CAPS_READER_CONTROL
        Check(params.reads==2,"exactly one actual virtual pointer getter per callback, no retained publication");
        for(auto mode:{Params::Mode::Missing,Params::Mode::Null,Params::Mode::FalseWithPointer,Params::Mode::Throw})
        {
            params.mode=mode;std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;unsigned originalCalls=0;
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,&params,sl::param::common::kSystemCaps,
                [](const Caps&){return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{{0xffffffffu,0x10deu,true}};});
            ++originalCalls;Check(!scope&&status!=Neurotic::Hooks::ManagedCapsStatus::Applied&&originalCalls==1,
                  "missing/null/failed/throwing optional query leaves only write unapplied and original attempted");
        }
        params.mode=Params::Mode::Present;params.publication=&b;
        {
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,&params,sl::param::common::kSystemCaps,
                [](const Caps&){return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{};});
            Check(!scope&&status==Neurotic::Hooks::ManagedCapsStatus::Unchanged,"feature policy requiring no spoof performs no incidental restoration write");
        }
        b.gpuCount=9;
        {
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,&params,sl::param::common::kSystemCaps,
                [](const Caps&)->std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{throw std::runtime_error("resolver must not inspect invalid array count");});
            Check(!scope&&status==Neurotic::Hooks::ManagedCapsStatus::InvalidCount,"actual declared-array bound is checked before policy reads, no blanket loader refusal");
        }
        b.gpuCount=1;
        {
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,&params,sl::param::common::kSystemCaps,
                [](const Caps&)->std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{throw std::runtime_error("optional policy query exception");});
            Check(!scope&&status==Neurotic::Hooks::ManagedCapsStatus::PolicyException,"optional policy failure does not manufacture a write or block the original callback");
        }
        {
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> scope;
            const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(scope,static_cast<sl::param::IParameters*>(nullptr),sl::param::common::kSystemCaps,
                [](const Caps&){return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{};});
            Check(!scope&&status==Neurotic::Hooks::ManagedCapsStatus::MissingParameters,"null interface leaves only optional spoof unavailable");
        }
        Check(a.adapters[0].architecture==0x170&&b.adapters[0].architecture==0x180,"old and replacement publications retain distinct originals");
        {
            using Target=Neurotic::Hooks::StreamlineCapsTarget;
            Params gameParams,localParams;Caps gameCaps,localCaps;
            gameCaps.adapters[0]={0x170,0x10de};localCaps.adapters[0]={0x180,0x1002};
            gameParams.set(sl::param::common::kSystemCaps,static_cast<void*>(&gameCaps));
            localParams.set(sl::param::common::kSystemCaps,static_cast<void*>(&localCaps));
            std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> gameScope,localScope;
            auto request=[](Target target){
                const auto architecture=Neurotic::Hooks::StreamlineArchitectureForTarget(0u,target);
                if(!architecture)return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{};
                return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{{*architecture,0x10deu,false}};
            };
            const auto gameStatus=Neurotic::Hooks::BeginManagedCaps<Caps>(gameScope,&gameParams,sl::param::common::kSystemCaps,
                [&](const Caps&){return request(Target::Game);});
            Check(gameStatus==Neurotic::Hooks::ManagedCapsStatus::Applied&&gameCaps.adapters[0].architecture==0&&localCaps.adapters[0].architecture==0x180,
                  "unavailable replacement zero request targets only actual game publication");
            gameScope.reset();
            const auto localStatus=Neurotic::Hooks::BeginManagedCaps<Caps>(localScope,&localParams,sl::param::common::kSystemCaps,
                [&](const Caps&){return request(Target::Local);});
            unsigned originalLocalCalls=0;++originalLocalCalls;
            Check(localStatus==Neurotic::Hooks::ManagedCapsStatus::Unchanged&&!localScope&&originalLocalCalls==1&&
                  localCaps.adapters[0].architecture==0x180&&gameCaps.adapters[0].architecture==0x170,
                  "unavailable replacement leaves selected local original and separate game object unchanged");
            for(auto target:{Target::Game,Target::Local})
            {
                auto* selected=target==Target::Game?&gameCaps:&localCaps;
                auto* other=target==Target::Game?&localCaps:&gameCaps;
                auto* selectedParams=target==Target::Game?&gameParams:&localParams;
                const auto before=selected->adapters[0];const auto otherBefore=other->adapters[0];
                std::optional<Neurotic::Hooks::StreamlineScopedCaps<Caps>> selectedScope;
                const auto status=Neurotic::Hooks::BeginManagedCaps<Caps>(selectedScope,selectedParams,sl::param::common::kSystemCaps,
                    [&](const Caps&){return std::optional<Neurotic::Hooks::StreamlineCapsOverride<Caps>>{{
                        *Neurotic::Hooks::StreamlineArchitectureForTarget(0xffffffffu,target),0x10deu,true}};});
                Check(status==Neurotic::Hooks::ManagedCapsStatus::Applied&&selected->adapters[0].architecture==0xffffffffu&&
                      selected->adapters[0].vendor==0x10de&&selected->driverVersionMajor==999&&selected->hwsSupported&&
                      other->adapters[0].architecture==otherBefore.architecture&&other->adapters[0].vendor==otherBefore.vendor,
                      "available override changes only the explicitly selected game or local publication");
                selectedScope.reset();
                Check(selected->adapters[0].architecture==before.architecture&&selected->adapters[0].vendor==before.vendor&&
                      selected->driverVersionMajor==555&&!selected->hwsSupported,
                      "available game and local overrides restore each actual prior value");
            }
        }
#endif
        std::printf("MANAGED_CAPS_PUBLICATION checks=%u PASS actualIParameters=1 privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& error){std::fprintf(stderr,"MANAGED_CAPS_PUBLICATION FAIL: %s\n",error.what());return 1;}
}
