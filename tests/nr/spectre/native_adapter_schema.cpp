#define main InheritedNativeContextMain
#include "../vertical-slice/native_context_chain.cpp"
#undef main
#if __has_include(<nr/context/NativeNgxSchemaQualification.h>)
#include <nr/context/NativeNgxSchemaQualification.h>
#define NR_NATIVE_ADAPTER_SCHEMA
#endif
int main()
{
    return Guard([&]{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
#ifdef NR_NATIVE_ADAPTER_SCHEMA
    auto make=[](bool native=true,bool unknown=true){return std::make_unique<Fixture>(false,"evaluate",native,false,false,true,true,false,unknown);};
    const auto enable=[](Fixture& value){value.profile.sourceSchemas.Get(0)->domain=S::SourceSchemaDomain::NativeNgxAdapter;};
    auto native=make();enable(*native);const auto result=native->Run();
    check(result.context&&result.certificate&&result.certificate->eligibility==C::Eligibility::Eligible,
          "explicit Native adapter schema qualifies without inventing provider version");
    bool rawUnknown=true;for(const auto& c:native->port->Candidates())rawUnknown=rawUnknown&&!c.sourceVersion.IsKnown();
    check(rawUnknown&&native->port->Candidates().size()==28,"provider source version remains unknown in all original observations");
    auto standard=make();check(!standard->Run().context,"provider schema policy still refuses unknown provider version");
    auto old=make(true,false);const auto previous=old->Run();
    check(previous.context&&previous.certificate&&previous.certificate->eligibility==C::Eligibility::Eligible,
          "existing provider-version policy remains supported");
    auto realFrame=make(false);enable(*realFrame);
    check(!S::ValidProfile(realFrame->profile)&&!realFrame->Run().context,"adapter rule cannot bypass real-frame provider policy");
    auto future=make();enable(*future);future->profile.sourceSchemas.Get(0)->version={2,0};
    check(!future->Run().context,"future Native adapter version refuses");
    auto mixed=make();enable(*mixed);mixed->profile.sourceSchemas.Push({Sym("NGX"),{1,0}});
    check(!mixed->Run().context,"mixed provider and adapter rule is not an implicit fallback");
    auto foreign=make();enable(*foreign);foreign->profile.sourceSchemas.Get(0)->schema=Sym("FSR2");
    check(!foreign->Run().context,"Native adapter profile cannot authorize another source schema");
    auto duplicate=make();enable(*duplicate);
    check(!duplicate->Run(3).context,"duplicate adapter claim fails full Native context qualification");
    auto invalid=make();enable(*invalid);invalid->profile.sourceSchemas.Get(0)->domain=static_cast<S::SourceSchemaDomain>(99);
    check(!invalid->Run().context,"invalid schema-domain value refuses");
    const auto& original=native->port->Candidates().front();
    check(S::NativeNgxAdapterClaims(original,native->context),"exact original adapter claims control");
    const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
    if(!S::ResolveList(original.claims,native->context,claims)||!claims)return 1;
    auto changed=original;auto body=*claims;
    for(auto& claim:body)if(claim.field.View()=="adapter.version")claim.raw=Known(C::ScalarValue{std::uint64_t{2}});
    changed.claims.backing=native->context.Publish(body);
    check(!S::NativeNgxAdapterClaims(changed,native->context),"wrong raw adapter version refuses");
    body=*claims;
    for(auto& claim:body)if(claim.field.View()=="source.boundary")claim.raw=Known(C::ScalarValue{Sym("create")});
    changed.claims.backing=native->context.Publish(body);
    check(!S::NativeNgxAdapterClaims(changed,native->context),"creation callback cannot qualify as evaluation");
    body=*claims;
    for(auto& claim:body)if(claim.field.View()=="source.api")claim.raw=Known(C::ScalarValue{Sym("D3D11")});
    changed.claims.backing=native->context.Publish(body);
    check(!S::NativeNgxAdapterClaims(changed,native->context),"foreign graphics API refuses");
    body=*claims;
    for(auto& claim:body)if(claim.field.View()=="adapter.version")claim.effective=claim.raw;
    changed.claims.backing=native->context.Publish(body);
    check(!S::NativeNgxAdapterClaims(changed,native->context),"effective adapter substitution refuses");
    body=*claims;body.Push({Sym("adapter.version"),Known(C::ScalarValue{std::uint64_t{1}}),{}, {}});
    changed.claims.count=static_cast<std::uint32_t>(body.Size());changed.claims.backing=native->context.Publish(body);
    check(!S::NativeNgxAdapterClaims(changed,native->context),"duplicate adapter claims refuse");
    changed=original;changed.header.owner=C::OwnerDomain::Context;
    check(!S::NativeNgxAdapterClaims(changed,native->context),"non-provider observation owner refuses");
#else
    check(false,"explicit Native adapter schema qualifies without inventing provider version");
#endif
    std::cout<<"Native adapter schema: "<<count<<" checks, "<<failed<<" failures; synthetic source declarations only\n";
    return failed?1:0;});
}
