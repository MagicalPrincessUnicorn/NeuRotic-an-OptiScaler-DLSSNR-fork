#include <cstdint>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <json.hpp>
#ifndef NR_OLD_LEGACY_JSON_RETURN
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include "FixtureApi.h"
#include <array>
#include <sstream>
#include <string>
#include <thread>
using JsonOwner=Neurotic::Hooks::StreamlineOwnedJson;
struct FailedAllocation
{
    static inline unsigned calls=0;
    static void* Allocate(std::size_t) noexcept {++calls;return nullptr;}
};
static bool failCopy=false,failDiagnostic=false;
static const char* lastError=nullptr;
static unsigned errors=0;
static const char* FixtureTryCopy(std::string_view text) noexcept
{return failCopy?JsonOwner::TryCopyForProcess<FailedAllocation>(text):JsonOwner::TryCopyForProcess(text);}
#endif
using namespace nlohmann::literals;
enum class FGInput {NoFG,DLSSG};
struct State
{
    struct Version {std::uint32_t major=2;} streamlineVersion;
    FGInput activeFgInput=FGInput::NoFG;
    static State& Instance(){static State state;return state;}
};
using ConstGetter=const char*(*)();
static ConstGetter o_dlssg_slGetPluginJSONConfig_sl1=nullptr;
template<class... Args>static void FixtureLog(Args&&... args){((void)args,...);}
#ifndef NR_OLD_LEGACY_JSON_RETURN
template<class Format,class... Args>static void FixtureError(Format&& format,Args&&... args)
{
    FixtureLog(format,args...);lastError=format;++errors;
    if(failDiagnostic)throw std::runtime_error("injected diagnostic failure");
}
#define LOG_ERROR(...) FixtureError(__VA_ARGS__)
#else
#define LOG_ERROR(...) FixtureLog(__VA_ARGS__)
#endif
#define LOG_DEBUG(...) FixtureLog(__VA_ARGS__)
#ifdef NR_OLD_LEGACY_JSON_RETURN
#include "old_legacy_json_return.inc"
#else
#include "green_legacy_json_return.inc"
#endif
#undef LOG_DEBUG
#undef LOG_ERROR
static unsigned checks=0;
static std::atomic<unsigned> calls{0};
static const char* OriginalA(){++calls;return "{\"id\":\"A\"}";}
static const char* OriginalB(){++calls;return "{\"id\":\"B\"}";}
static void Check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
#ifndef NR_OLD_LEGACY_JSON_RETURN
static constexpr const char* malformed="not JSON";
static constexpr const char* policy=R"({ "hooks": ["original"], "exclusive_hooks": ["exclusive"], "external": {"feature": {"tags": ["tag"]}, "hws": {"required": true}}, "vsync": {"supported": false}})";
static const char* OriginalNull(){++calls;return nullptr;}
static const char* OriginalMalformed(){++calls;return malformed;}
static const char* OriginalPolicy(){++calls;return policy;}
static const char* OriginalThrow(){++calls;throw std::runtime_error("actual authored original failure");}
static thread_local std::string threadPayload;
static const char* OriginalThread(){++calls;return threadPayload.c_str();}
static FixtureLoad fixtureLoad=nullptr;
static const char* lastActualBorrow=nullptr;
static const char* OriginalImageBorrow()
{
    ++calls;const char* output=nullptr;
    if(!fixtureLoad(nullptr,"{}",&output))throw std::runtime_error("authored image original failed");
    lastActualBorrow=output;return output;
}
struct Library
{
    HMODULE image=nullptr;
    explicit Library(const std::wstring& path):image(LoadLibraryW(path.c_str()))
    {if(!image)throw std::runtime_error("owned authored image load failed");}
    ~Library(){if(image)FreeLibrary(image);}
    void Close(){auto old=image;image=nullptr;if(!FreeLibrary(old))throw std::runtime_error("owned image release failed");}
};
#endif
int wmain(int argc,wchar_t** argv)
{
    try
    {
        (void)argc;(void)argv;
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalA;const auto* first=LegacyGetter();
        Check(first&&std::strcmp(first,"{\"id\":\"A\"}")==0&&calls==1,
            "first legacy call returns its actual original JSON after one provider call");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalB;const auto* second=LegacyGetter();
        Check(second&&std::strcmp(second,"{\"id\":\"B\"}")==0&&calls==2,
            "second legacy call returns its actual original JSON after one provider call");
        Check(first!=second&&std::strcmp(first,"{\"id\":\"A\"}")==0,
            "later legacy JSON result cannot overwrite an earlier issued result");
#ifndef NR_OLD_LEGACY_JSON_RETURN
        Check(HeapValidate(GetProcessHeap(),0,const_cast<char*>(first))&&HeapValidate(GetProcessHeap(),0,const_cast<char*>(second)),
            "successful parsed results use the actual Windows process heap");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalNull;auto before=calls.load();
        Check(!LegacyGetter()&&calls==before+1,"original null result remains null after exactly one original call");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalMalformed;before=calls.load();
        Check(LegacyGetter()==malformed&&calls==before+1,"malformed-input fallback retains the exact original borrowed pointer");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalThrow;before=calls.load();bool propagated=false;
        try{LegacyGetter();}catch(const std::runtime_error& e){propagated=std::strcmp(e.what(),"actual authored original failure")==0;}
        Check(propagated&&calls==before+1,"genuine original exception behavior and call count are preserved");
        State::Instance().streamlineVersion.major=1;State::Instance().activeFgInput=FGInput::DLSSG;
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalPolicy;before=calls.load();const auto* edited=LegacyGetter();
        const auto patched=nlohmann::json::parse(edited);
        Check(calls==before+1&&edited!=policy&&patched["hooks"].empty()&&patched["exclusive_hooks"].empty()&&
              patched["external"]["feature"]["tags"].empty()&&patched["vsync"]["supported"]==true&&patched["external"]["hws"]["required"]==false,
            "existing five legacy FG edits remain in the successfully owned result");
        failCopy=true;before=calls.load();auto failedAllocations=FailedAllocation::calls;
        Check(LegacyGetter()==policy&&calls==before+1&&FailedAllocation::calls==failedAllocations+1&&
              lastError&&std::strstr(lastError,"UNOWNED, UNPATCHED"),
            "copy failure returns the same authentic unpatched original and labels it unowned");
        failDiagnostic=true;before=calls.load();const auto priorErrors=errors;
        Check(LegacyGetter()==policy&&calls==before+1&&errors==priorErrors+1,
            "failure diagnostic exceptions cannot introduce a new escaping error route");
        failDiagnostic=false;failCopy=false;State::Instance().activeFgInput=FGInput::NoFG;
        Check(std::strcmp(first,"{\"id\":\"A\"}")==0&&std::strcmp(second,"{\"id\":\"B\"}")==0,
            "later success and failure paths leave previously owned results intact");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalThread;
        std::array<const char*,64> results{};std::array<std::jthread,4> workers;
        for(std::size_t t=0;t<workers.size();++t)workers[t]=std::jthread([&,t]
        {for(std::size_t i=t*16;i<(t+1)*16;++i){threadPayload="{\"id\":"+std::to_string(i)+"}";results[i]=LegacyGetter();}});
        for(auto& worker:workers)worker.join();
        bool independent=true;
        for(std::size_t i=0;i<results.size();++i)
        {
            independent=independent&&results[i]&&nlohmann::json::parse(results[i])["id"]==i;
            for(std::size_t j=0;j<i;++j)independent=independent&&results[i]!=results[j];
        }
        Check(independent,"parallel successful calls retain all independent results after thread-local input destruction");
        if(argc!=2)throw std::runtime_error("supply existing unchanged M1 fixture directory");
        CommonFixtureLedger ledger;Library image(std::wstring(argv[1])+L"\\NrCommonFixtureA.dll");
        const auto configure=reinterpret_cast<FixtureConfigure>(GetProcAddress(image.image,"FixtureConfigureLedger"));
        const auto getter=reinterpret_cast<FixtureGetter>(GetProcAddress(image.image,"FixtureGetFunction"));
        Check(configure&&getter,"actual authored image exposes its original and detach ledger");configure(&ledger);
        fixtureLoad=reinterpret_cast<FixtureLoad>(getter("slOnPluginLoad"));
        Check(fixtureLoad!=nullptr,"actual authored image original callback is available");
        o_dlssg_slGetPluginJSONConfig_sl1=&OriginalImageBorrow;before=calls.load();const auto* imageOwned=LegacyGetter();
        Check(imageOwned&&imageOwned!=lastActualBorrow&&calls==before+1&&std::strcmp(imageOwned,"{\"fixture\":\"A\"}")==0,
            "a successful actual image result is copied independently of its string owner");
        failCopy=true;before=calls.load();const auto* borrowed=LegacyGetter();
        Check(borrowed==lastActualBorrow&&calls==before+1&&InterlockedCompareExchange(&ledger.detached,0,0)==0,
            "fallback retains same-invocation bytes while the caller genuinely owns the actual string image");
        std::istringstream stream(borrowed);const auto callerOwned=stream.str();const auto callerConfig=nlohmann::json::parse(stream);
        Check(callerOwned=="{\"fixture\":\"A\"}","normal caller copies borrowed bytes before releasing their actual owner");
        image.Close();failCopy=false;fixtureLoad=nullptr;
        Check(InterlockedCompareExchange(&ledger.detached,0,0)==1&&callerOwned=="{\"fixture\":\"A\"}"&&callerConfig["fixture"]=="A"&&
              std::strcmp(imageOwned,"{\"fixture\":\"A\"}")==0,
            "after real unload only caller-owned and process-owned copies are used");
        std::printf("LEGACY_FALLBACK owned=0 patched=0 sameInvocation=1 upstreamOwnerThroughCopy=1 retiredGetterQualified=0\n");
#endif
        std::printf("LEGACY_JSON_OWNERSHIP checks=%u PASS privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"LEGACY_JSON_OWNERSHIP FAIL: %s\n",e.what());return 1;}
}
