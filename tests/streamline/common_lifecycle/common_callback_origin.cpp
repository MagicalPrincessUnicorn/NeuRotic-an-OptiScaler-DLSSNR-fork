#include "FixtureApi.h"
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#ifdef NR_OLD_COMMON_CALLBACK_ORIGIN
#include "old_common_callback_origin.inc"
#else
#include "../../../OptiScaler/hooks/StreamlineOnLoadBindings.h"
#include "../../../OptiScaler/hooks/StreamlineOwnedJson.h"
#include <array>
#include <atomic>
#include <thread>
static void FixtureWarning(const char*,const char* reason){std::printf("BINDING_WARNING %s\n",reason);}
using Invocation=Neurotic::Hooks::StreamlineOnLoadInvocation<void>;
struct InvocationReceipt
{
    FixtureLoad original=nullptr;
    void* parameters=nullptr;
    const void* binding=nullptr;
    const Neurotic::Hooks::StreamlineModuleEpochs::Epoch* codeEpoch=nullptr;
};
static thread_local InvocationReceipt invocationReceipt;
static thread_local const Invocation* activeInvocation=nullptr;
static void RecordInvocation(const Invocation& invocation)
{invocationReceipt={invocation.original,invocation.parameters,invocation.binding,invocation.codeEpoch};}
struct CaptureInvocation
{
    const Invocation& invocation;
    const Invocation* previous;
    explicit CaptureInvocation(const Invocation& value):invocation(value),previous(activeInvocation)
    {activeInvocation=&value;RecordInvocation(value);}
    ~CaptureInvocation(){RecordInvocation(invocation);activeInvocation=previous;}
};
#define LOG_WARN FixtureWarning
#include "green_common_callback_origin.inc"
#undef LOG_WARN
using Bindings=Neurotic::Hooks::StreamlineOnLoadBindings<void>;
static HANDLE transformEntered=nullptr,transformRelease=nullptr;
static bool BlockingTransform(const Invocation& invocation,const char* loader,const char** output)
{
    CaptureInvocation capture(invocation);
    const bool result=invocation.original(invocation.parameters,loader,output);
    SetEvent(transformEntered);
    if(WaitForSingleObject(transformRelease,10000)!=WAIT_OBJECT_0)return false;
    if(!result||!output||!*output)return result;
    return Neurotic::Hooks::StreamlineOwnedJson::PublishSuccessfulForProcess(result,output,*output);
}
static bool FailedOriginal(void*,const char*,const char** output)
{if(output)*output=reinterpret_cast<const char*>(static_cast<ULONG_PTR>(1));return false;}
static bool ThrowingOriginal(void*,const char*,const char**){throw std::runtime_error("authored original exception");}
static bool ExtraOriginal(void*,const char*,const char**){return false;}
static bool SuccessfulNullOriginal(void*,const char*,const char** output){if(output)*output=nullptr;return true;}
struct ArgumentReceipt {void* params=nullptr;const char* loader=nullptr;const char** output=nullptr;unsigned calls=0;bool exact=false;};
static ArgumentReceipt arguments;
static bool RecordingOriginal(void* params,const char* loader,const char** output)
{
    ++arguments.calls;arguments.exact=params==arguments.params&&loader==arguments.loader&&output==arguments.output;
    if(output)*output="{}";return true;
}
static FixtureLoad reentryTarget=nullptr;
static bool ReentrantOriginal(void* params,const char* loader,const char** output)
{
    const auto inner=Bindings::Bind(reentryTarget,&OriginProjection::hkcommon_slOnPluginLoad);
    return inner.status==Bindings::Status::Wrapped&&inner.function(params,loader,output);
}
static FixtureLoad sameBinding=nullptr;
static int outerParameters=1,innerParameters=2;
static bool nestedContextStable=false;
static unsigned nestedCalls=0;
static bool NestedContextOriginal(void* params,const char* loader,const char** output)
{
    ++nestedCalls;const auto* outer=activeInvocation;
    if(!outer||outer->parameters!=params)return false;
    if(params==&outerParameters)
    {
        const auto original=outer->original;const auto binding=outer->binding;const auto epoch=outer->codeEpoch;
        const bool result=sameBinding(&innerParameters,loader,output);
        nestedContextStable=activeInvocation==outer&&outer->parameters==&outerParameters&&
            outer->original==original&&outer->binding==binding&&outer->codeEpoch==epoch;
        return result&&nestedContextStable;
    }
    if(output)*output="{}";return params==&innerParameters;
}
static HANDLE concurrentReady=nullptr;
static std::atomic<unsigned> concurrentEntered{0};
static bool ConcurrentContextOriginal(void* params,const char*,const char** output)
{
    const auto* view=activeInvocation;
    if(concurrentEntered.fetch_add(1)==1)SetEvent(concurrentReady);
    const bool ready=WaitForSingleObject(concurrentReady,10000)==WAIT_OBJECT_0;
    if(output)*output="{}";
    return ready&&view&&view->parameters==params&&view->original==&ConcurrentContextOriginal;
}
#endif
static unsigned checks=0;
static void Check(bool ok,const char* message)
{if(!ok)throw std::runtime_error(message);++checks;std::printf("PASS %s\n",message);}
struct Library
{
    HMODULE image=nullptr;
    explicit Library(const std::wstring& path):image(LoadLibraryW(path.c_str()))
    {if(!image)throw std::runtime_error("authored fixture load failed");}
    ~Library(){if(image)FreeLibrary(image);}
    void Close(){if(image){const auto value=image;image=nullptr;if(!FreeLibrary(value))throw std::runtime_error("fixture release failed");}}
    FixtureGetter Getter()const
    {
        auto get=reinterpret_cast<FixtureGetter>(GetProcAddress(image,"FixtureGetFunction"));
        if(!get)throw std::runtime_error("authored fixture getter missing");return get;
    }
};
int wmain(int argc,wchar_t** argv)
{
    try
    {
        if(argc!=2)throw std::runtime_error("supply existing unchanged M1 fixture directory");
        const std::wstring root=argv[1];Library a(root+L"\\NrCommonFixtureA.dll"),b(root+L"\\NrCommonFixtureB.dll");
#ifndef NR_OLD_COMMON_CALLBACK_ORIGIN
        Check(Bindings::Prepare(),"production unload observer prepared before getter publication");
#endif
        OriginProjection::o_common_slGetPluginFunction=a.Getter();
        const auto issuedA=reinterpret_cast<FixtureLoad>(OriginProjection::hkcommon_slGetPluginFunction("slOnPluginLoad"));
        const char* first=nullptr;
        Check(issuedA&&issuedA(nullptr,"{}",&first)&&first&&std::strcmp(first,"{\"fixture\":\"A\"}")==0,
            "A getter initially issues a callback that executes actual authored A");
#ifndef NR_OLD_COMMON_CALLBACK_ORIGIN
        const auto contextA=invocationReceipt;
        Check(contextA.binding&&contextA.codeEpoch&&contextA.original,
            "transform receives actual immutable binding and code epoch for A");
#endif
        OriginProjection::o_common_slGetPluginFunction=b.Getter();
        const auto issuedB=reinterpret_cast<FixtureLoad>(OriginProjection::hkcommon_slGetPluginFunction("slOnPluginLoad"));
        const char* second=nullptr;
        Check(issuedB&&issuedB(nullptr,"{}",&second)&&second&&std::strcmp(second,"{\"fixture\":\"B\"}")==0,
            "B getter issues a callback that executes actual authored B");
#ifndef NR_OLD_COMMON_CALLBACK_ORIGIN
        const auto contextB=invocationReceipt;
        Check(contextB.binding&&contextB.codeEpoch&&contextB.binding!=contextA.binding&&contextB.codeEpoch!=contextA.codeEpoch,
            "A and B transformations receive distinct actual owner contexts");
#endif
        const char* cached=nullptr;const bool result=issuedA(nullptr,"{}",&cached);
        std::printf("CACHED_A_AFTER_B success=%u actual=%s\n",static_cast<unsigned>(result),cached?cached:"<null>");
        Check(result&&cached&&std::strcmp(cached,"{\"fixture\":\"A\"}")==0,
            "callback cached from A must still invoke A after B issues its callback");
#ifndef NR_OLD_COMMON_CALLBACK_ORIGIN
        Check(invocationReceipt.binding==contextA.binding&&invocationReceipt.codeEpoch==contextA.codeEpoch&&
              invocationReceipt.original==contextA.original,
            "cached A retains its immutable context after B publication");
#endif
#ifndef NR_OLD_COMMON_CALLBACK_ORIGIN
        const auto rawA=reinterpret_cast<FixtureLoad>(a.Getter()("slOnPluginLoad"));
        const auto rawB=reinterpret_cast<FixtureLoad>(b.Getter()("slOnPluginLoad"));
        Check(issuedA!=issuedB&&first!=cached&&std::strcmp(first,"{\"fixture\":\"A\"}")==0,
            "issued compiled entries and earlier owned JSON retain their independent identities");
        const auto forwardGet=reinterpret_cast<FixtureGetter>(GetProcAddress(a.image,"FixtureForwardedGetter"));
        Check(forwardGet==b.Getter(),"actual facade A export forwards to actual physical getter B");
        OriginProjection::o_common_slGetPluginFunction=forwardGet;
        const auto forwarded=reinterpret_cast<FixtureLoad>(OriginProjection::hkcommon_slGetPluginFunction("slOnPluginLoad"));
        Check(forwarded==issuedB,"forwarded returned callback deduplicates by actual B code epoch");
        Check(!OriginProjection::hkcommon_slGetPluginFunction("absent"),"absent original callback remains absent");
        Check(OriginProjection::hkcommon_slGetPluginFunction("slOnPluginStartup")==b.Getter()("slOnPluginStartup")&&
              OriginProjection::hkcommon_slGetPluginFunction("slOnPluginShutdown")==b.Getter()("slOnPluginShutdown"),
            "untouched historical startup and shutdown pointers remain exact raw originals");
        std::array<Bindings::Result,16> concurrent{};std::array<std::jthread,16> threads;
        for(std::size_t i=0;i<threads.size();++i)threads[i]=std::jthread([&,i]
            {concurrent[i]=Bindings::Bind(i%2?rawB:rawA,&OriginProjection::hkcommon_slOnPluginLoad);});
        for(auto& thread:threads)thread.join();
        bool deduplicated=true;
        for(std::size_t i=0;i<concurrent.size();++i)
            deduplicated=deduplicated&&concurrent[i].status==Bindings::Status::Wrapped&&concurrent[i].function==(i%2?issuedB:issuedA);
        Check(deduplicated,"simultaneous bindings retain exact immutable A and B routes without duplicate issuance");
        const auto missing=Bindings::Bind(nullptr,&OriginProjection::hkcommon_slOnPluginLoad);
        Check(!missing.function&&missing.status==Bindings::Status::OriginalNull,
            "null original remains null without manufacturing a wrapper");
        const auto noOwner=reinterpret_cast<FixtureLoad>(static_cast<ULONG_PTR>(1));
        const auto unavailable=Bindings::Bind(noOwner,&OriginProjection::hkcommon_slOnPluginLoad);
        Check(unavailable.status==Bindings::Status::OwnerUnavailable&&unavailable.function==noOwner,
            "actual loader ownership failure preserves the exact unwrapped value without calling it");
        int parameter=7;const char loader[]="authored loader";const char* recorded=nullptr;
        arguments={&parameter,loader,&recorded,0,false};
        const auto recording=Bindings::Bind(&RecordingOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        Check(recording.function(&parameter,loader,&recorded)&&arguments.calls==1&&arguments.exact&&recorded&&std::strcmp(recorded,"{}")==0,
            "bound original is called once with the exact three caller arguments");
        const auto recordingContext=invocationReceipt;
        int secondParameter=8;arguments={&secondParameter,loader,&recorded,0,false};
        Check(recording.function(&secondParameter,loader,&recorded)&&arguments.exact&&arguments.calls==1&&
              invocationReceipt.parameters==&secondParameter&&invocationReceipt.binding==recordingContext.binding&&
              invocationReceipt.codeEpoch==recordingContext.codeEpoch,
            "same binding receives current P then Q parameters without storing either in the shared record");
        Check(Bindings::Bind(&RecordingOriginal,&OriginProjection::hkcommon_slOnPluginLoad).function==recording.function,
            "repeated original and transform binding retains the same immutable entry");
        const auto successNull=Bindings::Bind(&SuccessfulNullOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        const char* emptyResult="caller original";
        const bool successNullResult=successNull.function(nullptr,"{}",&emptyResult);
        const auto nullContext=invocationReceipt;
        Check(successNullResult&&!emptyResult&&!issuedA(nullptr,"{}",nullptr),
            "original successful null text and original null-output failure are preserved");
        Check(nullContext.binding!=recordingContext.binding&&nullContext.codeEpoch==recordingContext.codeEpoch,
            "different originals in the same actual module have distinct bindings and the same code epoch");
        const auto nested=Bindings::Bind(&NestedContextOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        sameBinding=nested.function;const char* nestedOutput=nullptr;
        Check(sameBinding(&outerParameters,loader,&nestedOutput)&&nestedContextStable&&nestedCalls==2&&
              invocationReceipt.parameters==&outerParameters,
            "nested same binding keeps the outer invocation and parameters immutable across the inner call");
        concurrentReady=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Check(concurrentReady!=nullptr,"owned concurrent invocation event created");
        const auto parallel=Bindings::Bind(&ConcurrentContextOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        int parallelParams[2]={11,12};bool parallelResults[2]={false,false};
        std::jthread parallelA([&]{const char* value=nullptr;parallelResults[0]=parallel.function(&parallelParams[0],loader,&value)&&invocationReceipt.parameters==&parallelParams[0];});
        std::jthread parallelB([&]{const char* value=nullptr;parallelResults[1]=parallel.function(&parallelParams[1],loader,&value)&&invocationReceipt.parameters==&parallelParams[1];});
        parallelA.join();parallelB.join();CloseHandle(concurrentReady);concurrentReady=nullptr;
        Check(parallelResults[0]&&parallelResults[1]&&concurrentEntered.load()==2,
            "two overlapping calls of one binding retain their own actual parameters");
        reentryTarget=rawA;const auto reentrant=Bindings::Bind(&ReentrantOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        const char* reentrantOutput=nullptr;
        Check(reentrant.function(nullptr,"{}",&reentrantOutput)&&reentrantOutput&&std::strcmp(reentrantOutput,"{\"fixture\":\"A\"}")==0,
            "original callback can reenter binding and invocation without a publication lock spanning it");
        const auto failed=Bindings::Bind(&FailedOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        const char* failedOutput=nullptr;
        Check(failed.status==Bindings::Status::Wrapped&&!failed.function(nullptr,"{}",&failedOutput)&&
              failedOutput==reinterpret_cast<const char*>(static_cast<ULONG_PTR>(1)),
            "original false result and unreadable output survive the bound callback without a JSON read");
        const auto throwing=Bindings::Bind(&ThrowingOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        bool exceptionPreserved=false;
        try{throwing.function(nullptr,"{}",nullptr);}catch(const std::runtime_error& e)
        {exceptionPreserved=std::strcmp(e.what(),"authored original exception")==0;}
        Check(exceptionPreserved,"original exception propagates through the actual temporary code lease");
        CommonFixtureLedger ledger;
        const auto configure=reinterpret_cast<FixtureConfigure>(GetProcAddress(a.image,"FixtureConfigureLedger"));
        Check(configure!=nullptr,"actual authored provider detach ledger available");configure(&ledger);
        transformEntered=CreateEventW(nullptr,TRUE,FALSE,nullptr);transformRelease=CreateEventW(nullptr,TRUE,FALSE,nullptr);
        Check(transformEntered&&transformRelease,"owned transformation coordination events created");
        const auto blocking=Bindings::Bind(rawA,&BlockingTransform);
        bool blockedResult=false;const char* blockedOutput=nullptr;
        std::jthread worker([&]{blockedResult=blocking.function(nullptr,"{}",&blockedOutput);});
        Check(WaitForSingleObject(transformEntered,10000)==WAIT_OBJECT_0,"actual original returned while its owning callback transformation remains active");
        const auto oldA=a.image;a.Close();
        Check(InterlockedCompareExchange(&ledger.detached,0,0)==0,
            "host final release cannot unload provider before bound callback finishes copying its output");
        Check(SetEvent(transformRelease)!=FALSE,"owned transformation released");worker.join();
        Check(blockedResult&&blockedOutput&&std::strcmp(blockedOutput,"{\"fixture\":\"A\"}")==0&&
              InterlockedCompareExchange(&ledger.detached,0,0)==1,
            "actual last temporary lease releases provider after successful owned publication");
        CloseHandle(transformEntered);CloseHandle(transformRelease);transformEntered=nullptr;transformRelease=nullptr;
        const char* retired="caller sentinel";
        Check(!issuedA(nullptr,"{}",&retired)&&std::strcmp(retired,"caller sentinel")==0,
            "actual unload tombstones old A wrapper without changing caller output");
        const char* afterFacade=nullptr;
        Check(forwarded(nullptr,"{}",&afterFacade)&&std::strcmp(afterFacade,"{\"fixture\":\"B\"}")==0,
            "unloading facade A does not invalidate independently live callback B");
        Check(std::strcmp(first,"{\"fixture\":\"A\"}")==0&&std::strcmp(blockedOutput,"{\"fixture\":\"A\"}")==0,
            "all issued process-owned A text remains valid after actual provider detach");
        Library fresh(root+L"\\NrCommonFixtureA.dll");OriginProjection::o_common_slGetPluginFunction=fresh.Getter();
        const auto renewed=reinterpret_cast<FixtureLoad>(OriginProjection::hkcommon_slGetPluginFunction("slOnPluginLoad"));
        const char* newOutput=nullptr;
        std::printf("COMMON_ROUTE_RELOAD oldA=%p freshA=%p sameAddress=%u\n",static_cast<void*>(oldA),static_cast<void*>(fresh.image),static_cast<unsigned>(oldA==fresh.image));
        Check(renewed!=issuedA&&renewed(nullptr,"{}",&newOutput)&&!issuedA(nullptr,"{}",&retired),
            "fresh image epoch gets a new entry while old wrapper remains permanently retired");
        Check(invocationReceipt.binding!=contextA.binding&&invocationReceipt.codeEpoch!=contextA.codeEpoch,
            "replacement module incarnation cannot reuse the retired binding or code epoch identity");
        using Small=Neurotic::Hooks::StreamlineOnLoadBindings<void,4>;
        Check(Small::Prepare(),"same production factory supports a bounded capacity control");
        const auto smallA=Small::Bind(reinterpret_cast<FixtureLoad>(fresh.Getter()("slOnPluginLoad")),&OriginProjection::hkcommon_slOnPluginLoad);
        const auto smallB=Small::Bind(rawB,&OriginProjection::hkcommon_slOnPluginLoad);
        const auto smallFalse=Small::Bind(&FailedOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        const auto smallThrow=Small::Bind(&ThrowingOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        const auto exhausted=Small::Bind(&ExtraOriginal,&OriginProjection::hkcommon_slOnPluginLoad);
        Check(smallA.status==Small::Status::Wrapped&&smallB.status==Small::Status::Wrapped&&
              smallFalse.status==Small::Status::Wrapped&&smallThrow.status==Small::Status::Wrapped&&
              exhausted.status==Small::Status::CapacityExhausted&&exhausted.function==&ExtraOriginal,
            "capacity exhaustion preserves the exact original instead of recycling an escaped wrapper");
        const auto priorSmall=smallA.function;fresh.Close();Library freshAgain(root+L"\\NrCommonFixtureA.dll");
        const auto newRaw=reinterpret_cast<FixtureLoad>(freshAgain.Getter()("slOnPluginLoad"));
        const auto afterExhaustion=Small::Bind(newRaw,&OriginProjection::hkcommon_slOnPluginLoad);
        Check(afterExhaustion.status==Small::Status::CapacityExhausted&&afterExhaustion.function==newRaw&&
              !priorSmall(nullptr,"{}",&retired),"unload does not make an escaped slot reusable for a replacement generation");
        freshAgain.Close();b.Close();
        Neurotic::Runtime::RequestProcessExit();
        Check(!issuedB(nullptr,"{}",&retired)&&!throwing.function(nullptr,"{}",nullptr)&&Bindings::Bind(&ExtraOriginal,&OriginProjection::hkcommon_slOnPluginLoad).status==Bindings::Status::ProcessClosing&&
              !OriginProjection::hkcommon_slGetPluginFunction("slOnPluginLoad"),
            "existing process-close flag prevents new Root provider work and binding publication");
        std::printf("KNOWN_OPEN_GLOBAL_GETTER_ORIGIN=1 physicalActivationQualified=0\n");
#endif
        std::printf("COMMON_CALLBACK_ORIGIN checks=%u PASS productPhysicalEngine=0 privateProvider=0 GPU=0\n",checks);return 0;
    }
    catch(const std::exception& e){std::fprintf(stderr,"COMMON_CALLBACK_ORIGIN FAIL: %s\n",e.what());return 1;}
}
