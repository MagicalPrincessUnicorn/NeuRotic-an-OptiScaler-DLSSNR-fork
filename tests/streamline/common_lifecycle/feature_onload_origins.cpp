#include "FixtureApi.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef NR_FEATURE_ONLOAD_ORIGINS_GREEN
#include "../../../OptiScaler/hooks/StreamlineOnLoadBindings.h"
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <array>
using Invocation=Neurotic::Hooks::StreamlineOnLoadInvocation<void>;
struct InvocationReceipt {FixtureLoad original=nullptr;void* params=nullptr;const void* binding=nullptr;const Neurotic::Hooks::StreamlineModuleEpochs::Epoch* codeEpoch=nullptr;};
static thread_local InvocationReceipt lastInvocation;
static void RecordInvocation(const Invocation& value){lastInvocation={value.original,value.parameters,value.binding,value.codeEpoch};}
static void FixtureWarning(const char*,const char* feature,const char* reason){std::printf("BINDING_WARNING %s %s\n",feature,reason);}
#define LOG_WARN FixtureWarning
#endif
#include "feature_onload_origins.inc"
#ifdef NR_FEATURE_ONLOAD_ORIGINS_GREEN
#undef LOG_WARN
static std::array<FixtureLoad,3> cachedEntries{},liveEntries{};
static unsigned routeIndex=0;
static bool BudgetA(void*,const char*,const char** output){if(output)*output="A";return true;}
static bool BudgetB(void*,const char*,const char** output){if(output)*output="B";return true;}
static bool BudgetC(void*,const char*,const char** output){if(output)*output="C";return true;}
static bool BudgetTransform(const Invocation& value,const char* loader,const char** output){return value.original(value.parameters,loader,output);}
static void* MissingGetter(const char*){return nullptr;}
#endif
static unsigned checks=0,failures=0;
static void Require(bool condition,const char* message)
{if(!condition)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
struct Library
{
    HMODULE image=nullptr;
    explicit Library(const std::wstring& path):image(LoadLibraryW(path.c_str()))
    {if(!image)throw std::runtime_error("authored fixture load failed");}
    ~Library(){if(image)FreeLibrary(image);}
    void Close(){if(image){const auto handle=image;image=nullptr;if(!FreeLibrary(handle))throw std::runtime_error("fixture release failed");}}
    FixtureGetter Getter()const
    {
        auto get=reinterpret_cast<FixtureGetter>(GetProcAddress(image,"FixtureGetFunction"));
        if(!get)throw std::runtime_error("authored fixture getter missing");return get;
    }
};
template<class Projection>
static void Route(const char* name,FixtureGetter a,FixtureGetter b)
{
    Projection::Select(a);const auto issuedA=reinterpret_cast<FixtureLoad>(Projection::Get("slOnPluginLoad"));
    const char* first=nullptr;
    Require(issuedA&&issuedA(nullptr,"{}",&first)&&first&&std::strcmp(first,"{\"fixture\":\"A\"}")==0,"first A callback executes actual authored A");
#ifdef NR_FEATURE_ONLOAD_ORIGINS_GREEN
    const auto contextA=lastInvocation;
    Require(contextA.binding&&contextA.codeEpoch&&contextA.original,"feature transform receives its actual immutable owner context");
#endif
    Projection::Select(b);const auto issuedB=reinterpret_cast<FixtureLoad>(Projection::Get("slOnPluginLoad"));
    const char* second=nullptr;
    Require(issuedB&&issuedB(nullptr,"{}",&second)&&second&&std::strcmp(second,"{\"fixture\":\"B\"}")==0,"new B callback executes actual authored B");
    const char* cached=nullptr;const bool result=issuedA(nullptr,"{}",&cached);
    std::printf("FEATURE_ORIGIN route=%s cachedA_success=%u actual=%s\n",name,static_cast<unsigned>(result),cached?cached:"<null>");
    if(!result||!cached||std::strcmp(cached,"{\"fixture\":\"A\"}")!=0)
    {++failures;std::printf("FAIL cached %s A callback retargeted after B publication\n",name);}
    else{++checks;std::printf("PASS cached %s A retains its actual original\n",name);}
#ifdef NR_FEATURE_ONLOAD_ORIGINS_GREEN
    Require(lastInvocation.binding==contextA.binding&&lastInvocation.codeEpoch==contextA.codeEpoch&&lastInvocation.original==contextA.original,"cached feature A retains its context after B publication");
    int params=7;const char* current=nullptr;
    Require(issuedA(&params,"current params",&current)&&lastInvocation.params==&params,"feature context carries this invocation's exact current parameters");
    cachedEntries[routeIndex]=issuedA;liveEntries[routeIndex]=issuedB;++routeIndex;
    Projection::Select(MissingGetter);
    Require(!Projection::Get("slOnPluginLoad"),"missing actual callback remains null without manufactured success");
    Projection::Select(b);
#endif
}
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("supply existing unchanged M1 fixture directory");
        const std::wstring root=argv[1];Library a(root+L"\\NrCommonFixtureA.dll"),b(root+L"\\NrCommonFixtureB.dll");
        Route<DlssProjection>("DLSS",a.Getter(),b.Getter());
        Route<ReflexProjection>("Reflex",a.Getter(),b.Getter());
        Route<PclProjection>("PCL",a.Getter(),b.Getter());
#ifdef NR_FEATURE_ONLOAD_ORIGINS_GREEN
        Require(cachedEntries[0]!=cachedEntries[1]&&cachedEntries[0]!=cachedEntries[2]&&cachedEntries[1]!=cachedEntries[2],"three actual feature families use independent compiled binding domains");
        using SmallDlss=Neurotic::Hooks::StreamlineOnLoadBindings<void,2,DlssOnLoadDomain>;
        using SmallReflex=Neurotic::Hooks::StreamlineOnLoadBindings<void,2,ReflexOnLoadDomain>;
        using DefaultCommon=Neurotic::Hooks::StreamlineOnLoadBindings<void,2>;
        const auto one=SmallDlss::Bind(&BudgetA,&BudgetTransform),two=SmallDlss::Bind(&BudgetB,&BudgetTransform),full=SmallDlss::Bind(&BudgetC,&BudgetTransform);
        Require(one.status==SmallDlss::Status::Wrapped&&two.status==SmallDlss::Status::Wrapped&&full.status==SmallDlss::Status::CapacityExhausted&&full.function==&BudgetC,"one exhausted domain preserves the exact original without recycling entries");
        Require(SmallReflex::Bind(&BudgetC,&BudgetTransform).status==SmallReflex::Status::Wrapped&&DefaultCommon::Bind(&BudgetC,&BudgetTransform).status==DefaultCommon::Status::Wrapped,"feature-domain exhaustion cannot consume another family or default Common's independent budget");
        a.Close();
        for(std::size_t i=0;i<cachedEntries.size();++i)
        {
            const char* output="caller sentinel";
            Require(!cachedEntries[i](nullptr,"{}",&output)&&std::strcmp(output,"caller sentinel")==0,"actual A unload retires only its issued feature wrapper without changing caller output");
            Require(liveEntries[i](nullptr,"{}",&output)&&output&&std::strcmp(output,"{\"fixture\":\"B\"}")==0,"actual independently live B remains callable after A unload");
        }
        Neurotic::Runtime::RequestProcessExit();
        for(const auto entry:liveEntries){const char* output="caller sentinel";Require(!entry(nullptr,"{}",&output)&&std::strcmp(output,"caller sentinel")==0,"process closing prevents new work through issued feature wrappers");}
#endif
        std::printf("FEATURE_ONLOAD_ORIGINS checks=%u failures=%u physicalEngine=0 privateProvider=0 GPU=0\n",checks,failures);
        return failures?1:0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"FEATURE_ONLOAD_ORIGINS ERROR: %s\n",e.what());return 2;}
}
