#include <iostream>
#if __has_include(<nr/lifecycle/Fsr3LoadedContextObservation.h>)
#include <nr/lifecycle/Fsr3LoadedContextObservation.h>
namespace L=Neurotic::Lifecycle;
static unsigned calls=0;
static ffxContext expected=nullptr;
static unsigned mode=0;
static ffxReturnCode_t Query(ffxContext* context,ffxQueryDescHeader* header)
{
    ++calls;
    if(!context||*context!=expected||header->type!=FFX_API_QUERY_DESC_TYPE_GET_PROVIDER_VERSION||header->pNext)
        return FFX_API_RETURN_ERROR;
    auto* result=reinterpret_cast<ffxQueryGetProviderVersion*>(header);
    result->versionId=0xF65CDD1201001003ull;
    result->versionName=mode==2?nullptr:"1.1.3";
    return mode==1?FFX_API_RETURN_ERROR:FFX_API_RETURN_OK;
}
int main()
{
    int checks=0,failures=0;
    auto check=[&](bool ok,const char* name){++checks;if(!ok){++failures;std::cerr<<"FAIL "<<name<<'\n';}};
    int marker=0;expected=&marker;
    auto absent=L::ObserveFsr3LoadedContext(nullptr,expected);
    check(!absent.Valid()&&calls==0,"missing export never called");
    absent=L::ObserveFsr3LoadedContext(Query,nullptr);
    check(!absent.Valid()&&calls==0,"null context never queried");
    auto observed=L::ObserveFsr3LoadedContext(Query,expected);
    check(observed.Valid()&&calls==1,"exact live context queried directly");
    check(observed.provider==0xF65CDD1201001003ull&&observed.version=="1.1.3","loaded provider identity captured");
    check(observed.DrainStatus()==L::Fsr3DrainCapability::UncheckedWaitResults,"known bundled wait cannot attest release");
    mode=1;auto failed=L::ObserveFsr3LoadedContext(Query,expected);
    check(!failed.Valid()&&failed.provider==0&&failed.version.empty(),"failed query cannot publish output identity");
    mode=2;auto malformed=L::ObserveFsr3LoadedContext(Query,expected);
    check(!malformed.Valid(),"missing provider name rejected");
    observed.provider=1;
    check(observed.DrainStatus()==L::Fsr3DrainCapability::UnknownProvider,"foreign ABI has no drain contract");
    observed.provider=0xF65CDD1201001003ull;observed.version="3.1.6";
    check(observed.DrainStatus()==L::Fsr3DrainCapability::UnknownProvider,"contradictory id and version not qualified");
    std::cout<<"FSR loaded context: "<<checks<<" checks, "<<failures<<" failures; CPU only\n";
    return failures?1:0;
}
#else
int main(){std::cerr<<"FAIL exact loaded FSR context observation missing\n";return 1;}
#endif
