#include <cstdint>
#include <cstdio>
#include <stdexcept>
#include <string>
#include <string_view>
#include <json.hpp>
#include "../../../OptiScaler/hooks/StreamlineOnLoadInvocation.h"
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
using namespace nlohmann::literals;
enum class FGInput {NoFG,DLSSG};
struct feature_version
{
    std::uint32_t major=0,minor=0,patch=0;
    bool operator==(const feature_version&)const=default;
};
struct State
{
    feature_version streamlineVersion{};FGInput activeFgInput=FGInput::NoFG;
    static State& Instance(){static State state;return state;}
};
using Invocation=Neurotic::Hooks::StreamlineOnLoadInvocation<void>;
static bool NrOwnedOnLoadJson(bool result,const char** output,std::string_view rendered,const char*)
{return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,output,rendered);}
#define LOG_FUNC() ((void)0)
#define LOG_DEBUG(...) ((void)0)
#include "common_invocation_policy.inc"
#undef LOG_FUNC
#undef LOG_DEBUG
static unsigned checks=0,calls=0;
static std::string upstream;
static void* expectedParameters=nullptr;
static const char* expectedLoader=nullptr;
static const char** expectedOutput=nullptr;
static void Check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
static bool Original(void* params,const char* loader,const char** output)
{
    ++calls;
    if(params!=expectedParameters||loader!=expectedLoader||output!=expectedOutput)throw std::runtime_error("original argument changed");
    if(output)*output=upstream.c_str();return true;
}
static bool Failed(void*,const char*,const char** output)
{++calls;if(output)*output=reinterpret_cast<const char*>(static_cast<ULONG_PTR>(1));return false;}
static bool Null(void*,const char*,const char** output)
{++calls;if(output)*output=nullptr;return true;}
static nlohmann::json Config(unsigned major)
{
    return {{"version",{{"major",major},{"minor",1},{"build",1}}},{"api",{{"major",0}}},
            {"hooks",{"hook"}},{"exclusive_hooks",{"exclusive"}},
            {"external",{{"feature",{{"tags",{"tag"}}}},{"hws",{{"required",true}}}}},
            {"vsync",{{"supported",false}}}};
}
static bool Edited(const nlohmann::json& value)
{
    return value.at("hooks").empty()&&value.at("exclusive_hooks").empty()&&
           value.at("external").at("feature").at("tags").empty()&&
           value.at("vsync").at("supported")==true&&value.at("external").at("hws").at("required")==false;
}
int main()
{
    try
    {
        auto& state=State::Instance();
        state.activeFgInput=FGInput::DLSSG;
        state.streamlineVersion={2,7,1};auto explicitLegacy=Config(1);
        PatchSL1PluginJson(explicitLegacy,true);
        Check(Edited(explicitLegacy),"explicit Common policy applies five edits despite unrelated ambient version2");
        state.streamlineVersion={1,1,1};auto explicitModern=Config(2);const auto modernBefore=explicitModern;
        PatchSL1PluginJson(explicitModern,false);
        Check(explicitModern==modernBefore,"explicit Common policy preserves modern result despite ambient version1");
        auto compatibility=Config(1);PatchSL1PluginJson(compatibility);
        Check(Edited(compatibility),"unmigrated helper callers retain existing legacy compatibility policy");
        state.streamlineVersion={2,7,1};compatibility=Config(1);const auto compatibilityBefore=compatibility;
        PatchSL1PluginJson(compatibility);
        Check(compatibility==compatibilityBefore,"unmigrated modern helper callers retain existing compatibility behavior");
        int parameter=3;const char loader[]="authored loader";expectedParameters=&parameter;expectedLoader=loader;
        const char* output=nullptr;expectedOutput=&output;
        const Invocation invocation{&Original,&parameter,nullptr,nullptr};
        for(unsigned major:{1u,2u})
        {
            state.streamlineVersion={major==1?2u:1u,7,1};state.activeFgInput=FGInput::DLSSG;
            upstream=Config(major).dump();const unsigned before=calls;
            Check(CommonTransform(invocation,loader,&output)&&output&&calls==before+1,"actual Common body calls exact original once and publishes owned result");
            const auto result=nlohmann::json::parse(output);
            Check(major==1?Edited(result):result==Config(major),"actual Common result uses its own release for legacy JSON policy");
            Check(state.streamlineVersion==feature_version{major,1,1},"remaining compatibility write is preserved and global-overwrite obligation stays OPEN");
        }
        state.activeFgInput=FGInput::NoFG;upstream=Config(1).dump();
        Check(CommonTransform(invocation,loader,&output)&&nlohmann::json::parse(output)==Config(1),"Common legacy JSON remains unchanged without requested external FG");
        upstream=R"({"version":{"major":3}})";state.streamlineVersion={1,4,9};bool threw=false;
        try{CommonTransform(invocation,loader,&output);}catch(const nlohmann::json::out_of_range&){threw=true;}
        Check(threw&&state.streamlineVersion==feature_version{3,4,9},"malformed version preserves prior exception and partial-global-update behavior");
        const Invocation failed{&Failed,nullptr,nullptr,nullptr};
        Check(!CommonTransform(failed,nullptr,&output)&&output==reinterpret_cast<const char*>(static_cast<ULONG_PTR>(1)),"original false bypasses JSON and does not read poisoned output");
        const Invocation null{&Null,nullptr,nullptr,nullptr};
        Check(CommonTransform(null,nullptr,&output)&&!output,"original successful null retains existing no-parse behavior");
        std::printf("COMMON_INVOCATION_POLICY checks=%u PASS fullVersionIsolation=OPEN privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"COMMON_INVOCATION_POLICY FAIL: %s\n",e.what());return 1;}
}
