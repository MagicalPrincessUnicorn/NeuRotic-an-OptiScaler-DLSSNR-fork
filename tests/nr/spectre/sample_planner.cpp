#define main InheritedRepresentationMain
#include "../representations/planner_tests.cpp"
#undef main
int main()
{
    return Guard([&]{
    int failed=0,count=0;const auto check=[&](bool pass,const char* name){++count;if(!pass){++failed;std::cerr<<"FAIL "<<name<<'\n';}};
    auto f=std::make_unique<Fixture>();auto store=std::make_unique<Reader>();
    C::NativeSampleIdentityV1 sample;sample.session=f->frame.sessionId.KnownPart()->value;
    sample.stream=f->frame.renderStreamId.KnownPart()->value;sample.view=f->frame.viewId.KnownPart()->value;
    sample.feature=Id<C::ObjectIncarnation>("feature",1);sample.ingress=Id<C::ObjectIncarnation>("ingress",1);
    sample.producerOrdinal=1;sample.callback=Ev(90).record;
    f->frame.nativeSample=store->Publish(sample,C::OwnerDomain::Provider);f->frame.baseRealFrameId={};
    S::ContextRequest request;request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
    request.frame=store->Publish(f->frame,C::OwnerDomain::Provider);request.boundary=f->boundary;
    request.generations=store->Publish(C::GenerationVector{});request.renderRaster=store->Publish(f->source.raster);
    request.outputRaster=request.renderRaster;
    C::EvidenceVector ev;ev.provenance=Known(C::SourceClass::Native);ev.semanticCertainty=Known(C::SemanticCertainty::Qualified);
    ev.nativeFreshness=S::NativeFreshness(*f->frame.nativeSample,sample);ev.coverage=Known(C::CoverageKind::Complete);
    request.continuity=store->Publish(ev,C::OwnerDomain::Context);
    C::AcquisitionCandidate candidate;candidate.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
    candidate.provider=Key("provider");candidate.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
    candidate.sourceSchema=Sym("NGX");candidate.sourceVersion=Known(C::VersionNumber{1,0});
    candidate.semantic=C::SemanticKind::Color;candidate.frame=request.frame;candidate.boundary=f->boundary;
    candidate.evidence=store->Publish(ev,C::OwnerDomain::Provider);candidate.masks=store->Publish(C::MaskSet{},C::OwnerDomain::Provider);
    candidate.descriptiveLifetime.callbackScope=Known(sample.callback);
    C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);candidate.color=Known(store->Publish(color,C::OwnerDomain::Provider));
    candidate.payload=Known(C::CandidateValue{store->Publish(f->source,C::OwnerDomain::Resource)});candidate.contentRevision=f->source.identity.contentRevision;
    C::ObservationSet observations;observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);
    observations.boundary=f->boundary;C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;
    refs.Push({Sym(C::AcquisitionCandidate::WireName),candidate.header.record,1});observations.candidates=NrContextTests::PublishList(*store,refs);
    S::SemanticProfile profile;profile.key=f->profile;profile.purpose=Sym("RENDER.NativeSemantics");profile.rule=Sym("CTX.Semantics");
    profile.publication=Key("native.policy",1);profile.nativeSampleDomainVersion=1;profile.boundary=C::BoundaryKind::BeforeUpscale;
    profile.sourceSchemas.Push({Sym("NGX"),{1,0}});profile.acceptedEvidence.Push(C::SourceClass::Native);
    profile.requirements.Push({Sym("color.scene-linear"),true});profile.requireExactContent=true;
    const std::span<const C::AcquisitionCandidate> sources{&candidate,1};
    const auto built=S::BuildContextForPurpose(observations,sources,request,profile,*store);
    check(built.context.has_value(),"N10 Native C02 constructed for explicit purpose");if(!built.context)return 1;
    const auto& context=built.context->Value();
    const auto qualified=S::EvaluateSemanticCapability(context,sources,profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,4),*store);
    check(qualified.certificate&&qualified.certificate->Value().eligibility==C::Eligibility::Eligible,"N10 exact-content C04 admitted");
    if(!qualified.certificate)return 1;
    store->candidate=&candidate;S::RepresentationRequirement req;req.requested=true;req.required=true;
    req.kind=C::SemanticKind::Color;req.field=Sym("color.scene-linear");req.selected=*refs.Get(0);
    req.mapping=f->mapping;req.output=f->target.descriptor;req.precision=Sym("fp16");req.capabilities=f->caps;
    S::SourceRepresentationFacts facts;facts.source=f->source.identity;facts.precision=Known(Sym("fp16"));facts.capabilities=Known(f->caps);
    const auto derive=[&]{return S::DeriveRepresentationPlan(context,qualified.certificate->Value(),profile,req,facts,{},*store);};
    check(derive().path==S::PreparationPath::DirectUse,"N10 Native C04 reaches actual representation planner");
    const auto originalCandidate=candidate;
    auto structuralSource=f->source;structuralSource.identity.contentRevision={};
    candidate.payload=Known(C::CandidateValue{store->Publish(structuralSource,C::OwnerDomain::Resource)});candidate.contentRevision={};
    auto structuralProfile=profile;structuralProfile.requireExactContent=false;structuralProfile.publication=Key("structural.policy",1);
    const auto structuralContext=S::BuildContextForPurpose(observations,sources,request,structuralProfile,*store);
    check(structuralContext.context.has_value(),"structural C02 accepts unknown content");
    if(!structuralContext.context)return 1;
    const auto structuralCertificate=S::EvaluateSemanticCapability(structuralContext.context->Value(),sources,structuralProfile,
        NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,5),*store);
    check(structuralCertificate.certificate&&structuralCertificate.certificate->Value().eligibility==C::Eligibility::Eligible,
          "structural C04 accepts unknown content");
    if(!structuralCertificate.certificate)return 1;
    auto structuralFacts=facts;structuralFacts.source=structuralSource.identity;
    const auto planStructural=[&]{return S::DeriveStructuralRepresentationPlan(structuralContext.context->Value(),
        structuralCertificate.certificate->Value(),structuralProfile,req,structuralFacts,*store);};
    const auto structural=planStructural();
    check(structural.path==S::PreparationPath::StructuralOnly&&structural.plannedPath==S::PreparationPath::DirectUse&&structural.key&&
        !structural.prepared&&structural.evidence.preparedLookups==0,"unknown content yields a plan-only result without prepared-content reuse");
    const auto published=S::PublishRepresentationPlan(structural,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,6),Ev(),*store);
    check(published&&!published->Value().source.contentRevision.IsKnown()&&!published->Value().keys.content.contentRevision.IsKnown(),
          "structural C12 preserves unknown content");
    check(S::DeriveRepresentationPlan(structuralContext.context->Value(),structuralCertificate.certificate->Value(),
        structuralProfile,req,structuralFacts,{},*store).path==S::PreparationPath::Unavailable,"ordinary current-content planning remains strict");
    candidate=originalCandidate;
    check(planStructural().path==S::PreparationPath::Unavailable,"structural plan rejects different source identity facts");
    auto conversionReq=req;conversionReq.allocationClass=Sym("scratch");
    C::TransformStep step;step.kind=C::TransformKind::ExposureUndo;step.operation=Sym("fixture.scale");step.version=1;
    step.sourceUnits=Known(Sym("linear"));step.targetUnits=Known(Sym("linear"));step.alreadyApplied=Known(false);step.qualification=Known(f->meaning);
    C::BoundedList<C::SemanticClaim,8> parameters;C::SemanticClaim factor;factor.field=Sym("factor");
    factor.effective=Known(C::ScalarValue{.5});factor.raw=factor.effective;parameters.Push(factor);
    step.parameters=NrContextTests::PublishList(*store,parameters);conversionReq.transforms.Push(step);
    const auto conversion=S::DeriveRepresentationPlan(context,qualified.certificate->Value(),profile,conversionReq,facts,{},*store);
    const auto conversionPlan=S::PublishRepresentationPlan(conversion,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,50),Ev(),*store);
    check(conversion.path==S::PreparationPath::NewConversion&&conversionPlan.has_value(),"conversion control has a valid plan");
    if(!conversionPlan||!conversion.key)return 1;
    auto plan=conversionPlan->Value();const auto content=S::BuildPreparedContentKey(*conversion.key,f->source,f->target,f->frame,{},false,*store);
    check(content.has_value(),"conversion control has an exact prepared-content key");if(!content)return 1;
    C::PreparedView view;view.header=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Resource,51);
    view.plan={Sym(C::RepresentationPlan::WireName),plan.header.record,plan.header.revision};view.keys=plan.keys;
    view.view=store->Publish(f->target,C::OwnerDomain::Resource);auto applied=conversionReq.transforms;applied.Get(0)->alreadyApplied=Known(true);
    view.appliedTransforms=NrContextTests::PublishList(*store,applied);
    const S::PreparedViewEvidence cached{&view,&plan,&*content,{Sym(C::PreparedView::WireName),view.header.record,view.header.revision},plan.keys.semantic,plan.keys.plan};
    const std::span<const S::PreparedViewEvidence> prepared{&cached,1};
    const auto reuse=S::DeriveRepresentationPlan(context,qualified.certificate->Value(),profile,conversionReq,facts,prepared,*store);
    check(reuse.path==S::PreparationPath::ExistingPreparedView&&reuse.prepared.has_value(),"exact planning selects the compatible prepared candidate");
    const auto structureOnly=S::DeriveRepresentationPlan(context,qualified.certificate->Value(),profile,conversionReq,facts,prepared,*store,true);
    check(structureOnly.path==S::PreparationPath::StructuralOnly&&structureOnly.plannedPath==S::PreparationPath::NewConversion&&
        !structureOnly.prepared&&structureOnly.evidence.preparedLookups==0,"structural conversion ignores even a compatible prepared candidate");
    auto wrong=f->frame;auto another=sample;++another.producerOrdinal;wrong.nativeSample=store->Publish(another,C::OwnerDomain::Provider);
    candidate.frame=store->Publish(wrong,C::OwnerDomain::Provider);
    check(derive().path==S::PreparationPath::Unavailable,"N11 changed source sample cannot reuse C04");
    candidate.frame=request.frame;auto wrongEvidence=ev;++wrongEvidence.nativeFreshness->sample.revision;
    candidate.evidence=store->Publish(wrongEvidence,C::OwnerDomain::Provider);
    check(derive().path==S::PreparationPath::Unavailable,"N12 stale source freshness cannot reuse C04");
    std::cout<<"Sample planner: "<<count<<" checks, "<<failed<<" failures; CPU only\n";return failed?1:0;});
}
