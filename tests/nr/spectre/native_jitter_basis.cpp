#define main InheritedNativeContextMain
#include "../vertical-slice/native_context_chain.cpp"
#undef main
int main()
{
    int count=0,failed=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<Fixture>(false,"evaluate",true,false,false,true,true,false,true,true);
    f->profile.sourceSchemas.Get(0)->domain=S::SourceSchemaDomain::NativeNgxAdapter;
    f->profile.requirements.Push({Sym("jitter"),true});
    const auto result=f->Run();
    check(result.context&&result.certificate&&result.certificate->eligibility==C::Eligibility::Eligible,
          "independent Native jitter qualifies through C02 and C04");
    if(!result.context)return 1;
    const auto& context=*result.context;const auto* raster=S::ResolveMetadata(context.renderRaster,f->context);
    const auto* motion=S::ResolveOptional(context.motion,f->context);
    const auto* frame=S::ResolveMetadata(context.frame,f->context);
    const auto rect=raster->active.KnownPart()->value;const C::Vec2 expected{2.0/rect.width,-1.0/rect.height};
    check(S::Established(context.jitter)&&context.jitter.KnownPart()->value==expected&&motion&&
          motion->currentRaster.active.KnownPart()->value!=rect,"jitter normalizes once against render basis independent of motion grid");
    P::BindingPlan bindings;const bool built=P::BuildFrameControls(context,Ev(1),f->context,bindings);
    const auto* x=P::FindBinding(bindings,"Jitter.Offset.X");const auto* y=P::FindBinding(bindings,"Jitter.Offset.Y");
    check(built&&x&&y&&P::Number(x->effective)&&P::Number(y->effective)&&*P::Number(x->effective)==2&&*P::Number(y->effective)==-1,
          "binding preserves original pixel jitter after qualified basis");
    auto wrong=context;wrong.jitter=Known(C::Vec2{99,99});P::BindingPlan invalid;
    check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"basis string cannot replace qualified jitter value");
    wrong=context;wrong.guides={};invalid={};
    check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"missing qualified jitter guide refuses bindings");
    wrong=context;auto foreign=*raster;auto foreignRect=rect;--foreignRect.width;foreign.active=Known(foreignRect);
    wrong.renderRaster=f->context.Publish(foreign);invalid={};
    check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"wrong render raster refuses independent jitter bindings");
    const C::AcquisitionCandidate* jitter=nullptr;
    for(const auto& candidate:f->port->Candidates())if(candidate.semantic==C::SemanticKind::Jitter)jitter=&candidate;
    check(jitter&&!jitter->motion.IsKnown(),"independent basis does not add a conflicting motion bundle");
    if(jitter)
    {
        auto poisoned=*jitter;auto ref=std::get<C::MetadataRef<C::ResourceView>>(poisoned.payload.KnownPart()->value);
        ref.owner=C::OwnerDomain::Provider;poisoned.payload=Known(C::CandidateValue{ref});
        check(!S::InterpretJitter(poisoned,*frame,*raster,f->context).IsKnown(),"non-Resource jitter basis refuses interpretation");
        poisoned=*jitter;auto noSample=*frame;noSample.nativeSample.reset();
        check(!S::InterpretJitter(poisoned,noSample,*raster,f->context).IsKnown(),"new payload basis requires authentic Native sample domain");
        check(!S::InterpretJitter(poisoned,*frame,foreign,f->context).IsKnown(),"payload raster must match requested render raster");
        poisoned=*jitter;poisoned.header.scope={Key("foreign.callback",1)};
        check(!S::InterpretJitter(poisoned,*frame,*raster,f->context).IsKnown(),"foreign callback scope cannot carry jitter basis");
        poisoned=*jitter;poisoned.motion=f->port->Source().seed.motion;
        check(!S::InterpretJitter(poisoned,*frame,*raster,f->context).IsKnown(),"payload basis cannot hide conflicting motion description");
        poisoned=*jitter;auto originalRef=std::get<C::MetadataRef<C::ResourceView>>(jitter->payload.KnownPart()->value);
        auto incomplete=*S::ResolveMetadata(originalRef,f->context);incomplete.identity.resourceViewIncarnation={};
        auto resource=f->arena->ForOwner(C::OwnerDomain::Resource);
        poisoned.payload=Known(C::CandidateValue{resource.Publish(incomplete)});
        check(!S::InterpretJitter(poisoned,*frame,*raster,f->context).IsKnown(),"incomplete Resource identity cannot qualify jitter basis");
        poisoned=*jitter;auto evidence=*S::ResolveMetadata(jitter->evidence,f->context);evidence.provenance=Known(C::SourceClass::Derived);
        auto provider=f->arena->ForOwner(C::OwnerDomain::Provider);poisoned.evidence=provider.Publish(evidence);
        check(!S::InterpretJitter(poisoned,*frame,*raster,f->context).IsKnown(),"derived source cannot claim original Native jitter basis");
        const C::BoundedList<C::GuideDescription,8>* guides=nullptr;S::ResolveList(context.guides,f->context,guides);
        auto changed=*guides;
        for(auto& guide:changed)if(guide.kind==C::SemanticKind::Jitter)
            guide.evidence.semanticCertainty=Known(C::SemanticCertainty::Claimed);
        wrong=context;wrong.guides.backing=f->context.Publish(changed);invalid={};
        check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"unqualified guide cannot authorize pixel jitter binding");
        changed=*guides;
        for(const auto& guide:*guides)if(guide.kind==C::SemanticKind::Jitter)changed.Push(guide);
        wrong=context;wrong.guides.backing=f->context.Publish(changed);wrong.guides.count=static_cast<std::uint32_t>(changed.Size());invalid={};
        check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"duplicate qualified guide cannot establish unique jitter basis");
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> retained;
        const C::BoundedList<C::ContractRef<C::ContractId::C01>,64>* coherent=nullptr;S::ResolveList(context.coherentCandidates,f->context,coherent);
        for(const auto& source:*coherent)if(source.record!=jitter->header.record)retained.Push(source);
        wrong=context;wrong.coherentCandidates.backing=f->context.Publish(retained);
        wrong.coherentCandidates.count=static_cast<std::uint32_t>(retained.Size());invalid={};
        check(!P::BuildFrameControls(wrong,Ev(1),f->context,invalid),"jitter guide must retain its exact coherent source candidate");
    }
    auto legacy=std::make_unique<Fixture>();auto old=legacy->Run();P::BindingPlan oldBindings;
    check(old.context&&P::BuildFrameControls(*old.context,Ev(1),legacy->context,oldBindings),"legacy motion raster basis stays supported");
    std::cout<<"Native jitter basis: "<<count<<" checks, "<<failed<<" failures; CPU synthetic owner declarations\n";
    return failed?1:0;
}
