#define main InheritedNativeContextMain
#include "../vertical-slice/native_context_chain.cpp"
#undef main
#include <nr/context/NativeSampleQualification.h>
int main()
{
    int count=0,failed=0;
    const auto check=[&](bool value,const char* name){++count;if(!value){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto native=std::make_unique<Fixture>(false,"evaluate",true);
    const auto result=native->Run();
    if(!result.context)std::cerr<<"Native refusal: "<<result.reason.View()<<'\n';
    check(result.context&&result.certificate&&result.certificate->eligibility==C::Eligibility::Eligible,
          "N10 NGX retained observations qualify C02 and C04 without a base frame");
    bool originalsFresh=native->port->Candidates().size()==28;
    const auto* frame=S::ResolveMetadata(native->request.frame,native->context);
    for(const auto& candidate:native->port->Candidates())
    {
        const auto* evidence=S::ResolveMetadata(candidate.evidence,native->context);
        originalsFresh=originalsFresh&&frame&&evidence&&S::NativeEvidenceFresh(*evidence,*frame,native->context);
    }
    check(originalsFresh,"N10 FEED publishes explicit sample freshness on original C01 evidence");
    const auto freshness=[]<class T>(const T& certificate){
        if constexpr(requires{certificate.nativeFreshness;})return certificate.nativeFreshness.has_value()&&!certificate.maximumAge.IsKnown();
        else return false;};
    check(result.certificate&&freshness(*result.certificate),"N10 C04 records sample freshness without invented real-frame age");
    P::BindingPlan bindings;
    check(result.context&&P::BuildFrameControls(*result.context,Ev(1),native->context,bindings),
          "N10 retained Native controls remain usable by binding builder");
    auto wrong=std::make_unique<Fixture>(false,"evaluate",true,true);
    check(!wrong->Run().context,"N11 sample must identify the actual source callback");
    auto motionWrong=std::make_unique<Fixture>(false,"evaluate",true,false,true);
    const auto refusedMotion=motionWrong->Run();
    check(refusedMotion.certificate&&refusedMotion.certificate->eligibility==C::Eligibility::Ineligible,
          "N11 raw Native motion must identify the qualified sample");
    auto legacy=std::make_unique<Fixture>();const auto old=legacy->Run();
    check(old.context&&old.certificate&&old.certificate->eligibility==C::Eligibility::Eligible,
          "N09 established real-frame qualification stays valid");
    auto mixed=std::make_unique<Fixture>(false,"evaluate",true);mixed->profile.maximumAge=Known(std::uint64_t{0});
    check(!mixed->Run().context,"N09 sample policy refuses mixed age domain");
    auto future=std::make_unique<Fixture>(false,"evaluate",true);
    []<class T>(T& value){if constexpr(requires{value.nativeSampleDomainVersion;})value.nativeSampleDomainVersion=2;}(future->profile);
    check(!future->Run().context,"R07 unknown Native qualification version refuses");
    auto scoped=std::make_unique<Fixture>(false,"evaluate",true,false,false,true);
    const auto within=scoped->Run();
    check(within.context&&within.certificate&&within.certificate->eligibility==C::Eligibility::Eligible&&
          within.context->header.scope==scoped->request.header.scope,
          "N03 stable enrolled scope permits fresh Native callback identity");
    auto foreignScope=std::make_unique<Fixture>(false,"evaluate",true,false,false,true);
    foreignScope->request.header.scope={Key("native.foreign.scope",1)};
    foreignScope->certificate.scope=foreignScope->request.header.scope;
    check(!foreignScope->Run().context,"N03 foreign enrolled context scope refuses");
    auto foreignCallback=std::make_unique<Fixture>(false,"evaluate",true,true,false,true);
    check(!foreignCallback->Run().context,"N11 enrolled scope cannot replace exact callback freshness");
    auto realDomain=std::make_unique<Fixture>(false,"evaluate",false,false,false,true);
    check(!realDomain->Run().context,"N09 Native enrollment never weakens real-frame scope");
    std::cout<<"Sample context: "<<count<<" checks, "<<failed<<" failures; CPU only\n";
    return failed?1:0;
}
