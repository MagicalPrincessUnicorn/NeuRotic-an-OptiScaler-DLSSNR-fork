#define main InheritedNativeContextMain
#include "../vertical-slice/native_context_chain.cpp"
#undef main
int main()
{
    return Guard([&]{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto make=[](bool unknown,bool invalid=false){return std::make_unique<Fixture>(false,"evaluate",true,false,false,true,unknown,invalid);};
    auto known=make(false);const auto control=known->Run();
    check(control.context&&control.certificate&&control.certificate->eligibility==C::Eligibility::Eligible,"known-content Native context control");
    auto unknown=make(true);const auto planned=unknown->Run();
    check(planned.context&&planned.certificate&&planned.certificate->eligibility==C::Eligibility::Eligible,"unknown content permits structural Native context");
    bool noRevision=true;unsigned resources=0;
    for(const auto& candidate:unknown->port->Candidates())if(candidate.payload.IsKnown())
        if(const auto* ref=std::get_if<C::MetadataRef<C::ResourceView>>(&candidate.payload.KnownPart()->value))
        {
            const auto* view=S::ResolveMetadata(*ref,unknown->context);++resources;
            noRevision=noRevision&&view&&!view->identity.contentRevision.IsKnown()&&!candidate.contentRevision.IsKnown();
        }
    check(resources==6&&noRevision,"structural qualification invents no input revision");
    auto strict=make(true);strict->profile.requireExactContent=true;const auto refused=strict->Run();
    check(!refused.certificate||refused.certificate->eligibility!=C::Eligibility::Eligible,"exact-content semantic profile refuses unknown contents");
    auto invalid=make(true,true);check(!invalid->Run().context,"unknown contents cannot hide an incomplete resource descriptor");
    auto stale=std::make_unique<Fixture>(false,"evaluate",true,true,false,true,true);
    check(!stale->Run().context,"structural context still binds the authentic callback");
    std::cout<<"Structural Native context: "<<count<<" checks, "<<failed<<" failures; CPU synthetic source only\n";
    return failed?1:0;});
}
