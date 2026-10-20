#include <iostream>
#include <stdexcept>
#include <nr/protocol/NativeTemporalRuntime.h>
#if __has_include(<dlssnr/NativeParameterOverride.h>)
#include <dlssnr/NativeParameterOverride.h>
#define HAS_OVERRIDE 1
#endif
#if __has_include(<nr/protocol/NativeCommandStateScope.h>)
#include <nr/protocol/NativeCommandStateScope.h>
#define HAS_COMMAND_SCOPE 1
#endif
struct Parameters
{
    void* value=nullptr;int gets=0,sets=0;bool throwGet=false,throwSet=false,ignoreSet=false,failGet=false;
    int Get(const char*,void** out){++gets;if(throwGet)throw std::runtime_error("get");*out=value;return failGet?1:0;}
    void Set(const char*,void* next){++sets;if(!ignoreSet)value=next;if(throwSet)throw std::runtime_error("set");}
};
int main()
{
    unsigned checks=0,failed=0;const auto check=[&](bool ok,const char* name){++checks;if(!ok){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef HAS_OVERRIDE
    int original=0,scratch=0,other=0;
    Parameters p;p.value=&original;
    {
        DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);
        check(binding.Ready(),"original observed");check(binding.Bind(&scratch),"scratch bound");
        check(p.value==&scratch,"actual scratch");check(!binding.Bind(&other),"no rebinding");
        check(binding.Restore()&&p.value==&original,"restore verified");
        const auto count=p.sets;check(binding.Restore()&&p.sets==count,"restore idempotent");
        check(!binding.Bind(&scratch),"closed override refuses");
    }
    check(p.sets==2,"destructor no duplicate restoration");
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&other,0);check(!binding.Ready()&&!binding.Bind(&scratch),"wrong captured owner binding");}
    check(p.sets==0&&p.value==&original,"mismatch preserves original");
    for(unsigned mode=0;mode<2;++mode)
    {
        p={};p.value=&original;p.failGet=mode==0;p.throwGet=mode==1;
        {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);check(!binding.Ready()&&!binding.Bind(&scratch),"unavailable original refuses");}
        check(p.sets==0,"no write on unavailable original");
    }
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);p.ignoreSet=true;
        check(!binding.Bind(&scratch),"silent Set failure observed");check(binding.Restore(),"already original restoration observed");}
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);p.throwSet=true;
        check(!binding.Bind(&scratch)&&p.value==&scratch,"partial throwing Set remains effectful");p.throwSet=false;
        check(binding.Restore()&&p.value==&original,"partial override restored");}
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);check(binding.Bind(&scratch),"failure path bound");p.ignoreSet=true;
        check(!binding.Restore(),"silent restoration failure explicit");const auto count=p.sets;
        check(!binding.Restore()&&p.sets==count,"failed restoration sticky");}
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);check(binding.Bind(&scratch),"RAII bound");}
    check(p.value==&original&&p.sets==2,"RAII actual restoration");
    p={};p.value=&original;
    {DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);p.value=&other;
        check(!binding.Bind(&scratch)&&p.sets==0,"recheck original at mutation");}
    check(p.value==&other&&p.sets==0,"unused override does not overwrite newer owner");
    {
        Neurotic::Protocol::NativeExecutionObserver observer;
        observer.Fail("Native.EarlierFailure");
        observer.OutputRestoreFailed();
        check(observer.Facts().outputRestoreFailed,"restoration failure retained after earlier failure");
        check(observer.Facts().failure==Neurotic::Protocol::Symbol("Native.EarlierFailure"),"first diagnostic preserved");
        unsigned srCalls=0;
        if(Neurotic::Protocol::NativeMayContinueOuterEvaluation(observer.Facts()))++srCalls;
        check(srCalls==0,"uncertain Output blocks outer SR");
        Neurotic::Protocol::NativeExecutionObserver safe;
        safe.Fail("Native.ProviderUnavailable");
        if(Neurotic::Protocol::NativeMayContinueOuterEvaluation(safe.Facts()))++srCalls;
        check(srCalls==1,"ordinary refusal permits original outer SR");
        p={};p.value=&original;
        Neurotic::Protocol::NativeExecutionObserver partial;
        {
            DlssNr::NativeParameterOverride binding(&p,"Output",&original,0);
            check(binding.Bind(&scratch),"outer transaction binds scratch");
            p.ignoreSet=true;
            if(!binding.Restore())partial.OutputRestoreFailed();
        }
        if(Neurotic::Protocol::NativeMayContinueOuterEvaluation(partial.Facts()))++srCalls;
        check(p.value==&scratch&&srCalls==1,"actual failed Set cannot enter required SR");
    }
#else
    check(false,"restoration helper unavailable");
#endif
#ifndef HAS_COMMAND_SCOPE
    check(false,"Native executor requires exact command-state restoration scope");
#else
    namespace P=Neurotic::Protocol;
    unsigned restoreCalls=0;
    P::NativeExecutionObserver stateObserver;
    {
        P::NativeCommandStateScope scope([&]{++restoreCalls;return true;},[&]()noexcept{stateObserver.CommandStateRestoreFailed();});
        check(scope.Restore(),"command-state scope restores explicitly");
        check(scope.Restore()&&restoreCalls==1,"command-state restore exactly once");
    }
    check(restoreCalls==1&&P::NativeMayContinueOuterEvaluation(stateObserver.Facts()),"successful restore permits outer continuation");
    P::NativeExecutionObserver failedState;
    failedState.Fail("Native.EarlierFailure");
    {
        P::NativeCommandStateScope scope([&]{++restoreCalls;return false;},[&]()noexcept{failedState.CommandStateRestoreFailed();});
    }
    check(restoreCalls==2&&failedState.Facts().commandStateRestoreFailed,"early-exit restoration failure retained");
    check(failedState.Facts().failure==P::Symbol("Native.EarlierFailure"),"command failure keeps first diagnostic");
    check(!P::NativeMayContinueOuterEvaluation(failedState.Facts()),"failed state restoration blocks outer SR");
    P::NativeExecutionObserver throwingState;
    try
    {
        P::NativeCommandStateScope scope([&]() -> bool {++restoreCalls;throw std::runtime_error("setter");},[&]()noexcept{throwingState.CommandStateRestoreFailed();});
        throw std::runtime_error("renderer");
    }
    catch(const std::runtime_error&) {}
    check(restoreCalls==3&&throwingState.Facts().commandStateRestoreFailed,"exception unwinding retains failed original setter");
    check(!P::NativeMayContinueOuterEvaluation(throwingState.Facts()),"exceptional restoration cannot continue SR");
    P::NativeExecutionObserver onceState;
    {
        P::NativeCommandStateScope scope([&]{++restoreCalls;return false;},[&]()noexcept{onceState.CommandStateRestoreFailed();});
        check(!scope.Restore()&&!scope.Restore(),"failed restore cannot be retried as success");
    }
    check(restoreCalls==4,"failed restore one attempt including destruction");
#endif
    std::cout<<"checks="<<checks<<" failures="<<failed<<'\n';return failed?1:0;
}
