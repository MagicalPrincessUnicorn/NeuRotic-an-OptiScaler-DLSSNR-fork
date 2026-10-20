#include "RepresentationFixture.h"
#if __has_include(<nr/context/RepresentationPlanner.h>)
#include <nr/context/RepresentationPlanner.h>
#define HAS_PLANNER 1
#endif
using namespace NrRepresentationTests;
#ifdef HAS_PLANNER
struct Reader:NrContextTests::Store
{
    const C::AcquisitionCandidate* candidate=nullptr;
    std::array<C::AcquisitionCandidate,4> alternatives;
    mutable unsigned lookups=0;
    unsigned interns=0;
    int fixtureRightsState=0; // External owner fixture only; production CTX must never read this.
    std::list<std::pair<S::SemanticRepresentationKey,C::RecordKey>> semanticKeys;
    std::list<std::pair<S::RepresentationPlanKey,C::RecordKey>> planKeys;
    std::list<std::pair<S::AllocationRequirementKey,C::RecordKey>> allocationKeys;
    const C::AcquisitionCandidate* ResolveCandidate(const C::ContractRef<C::ContractId::C01>& ref) const
    {
        ++lookups;if(candidate&&candidate->header.record==ref.record)return candidate;
        for(const auto& other:alternatives)if(other.header.record==ref.record)return &other;return nullptr;
    }
    template<class T> C::RecordKey Put(const T& key,std::list<std::pair<T,C::RecordKey>>& entries)
    {
        for(const auto& p:entries)if(p.first==key)return p.second;
        const auto result=Key("representation.intern",++interns);entries.push_back({key,result});return result;
    }
    C::RecordKey Intern(const S::SemanticRepresentationKey& key){return Put(key,semanticKeys);}
    C::RecordKey Intern(const S::RepresentationPlanKey& key){return Put(key,planKeys);}
    C::RecordKey Intern(const S::AllocationRequirementKey& key){return Put(key,allocationKeys);}
};
#endif
int main(int argc,char** argv)
{
    return Guard([&]{Report r("representation-planner",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef HAS_PLANNER
        Fixture f;Reader store;S::ContextRequest request;
        request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,2);
        request.frame=store.Publish(f.frame,C::OwnerDomain::IdentityRegistry);request.boundary=f.boundary;
        C::GenerationVector initialGenerations;initialGenerations.Insert(Gen(C::IdentityKind::RasterGeneration,"raster",1));
        request.generations=store.Publish(initialGenerations,C::OwnerDomain::IdentityRegistry);
        request.renderRaster=store.Publish(f.source.raster);request.outputRaster=request.renderRaster;
        C::EvidenceVector ev;ev.provenance=Known(C::SourceClass::Native);ev.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        ev.ageInRealFrames=Known(uint64_t{0});ev.coverage=Known(C::CoverageKind::Complete);ev.access=Known(C::DescriptiveAccess::CallbackBorrow);
        ev.lineage.Push(Key("source.evidence"));request.continuity=store.Publish(ev,C::OwnerDomain::ColorContinuity);
        C::AcquisitionCandidate candidate;candidate.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        candidate.provider=Key("provider");candidate.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
        candidate.sourceSchema=Sym("representation.fixture");candidate.sourceVersion=Known(C::VersionNumber{1,0});
        candidate.semantic=C::SemanticKind::Color;candidate.frame=request.frame;candidate.boundary=f.boundary;
        candidate.evidence=store.Publish(ev,C::OwnerDomain::Provider);candidate.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);
        C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);candidate.color=Known(store.Publish(color,C::OwnerDomain::Provider));
        candidate.payload=Known(C::CandidateValue{store.Publish(f.source,C::OwnerDomain::Resource)});candidate.contentRevision=f.source.identity.contentRevision;
        std::array<C::AcquisitionCandidate,5> candidates;candidates[0]=candidate;
        for(std::size_t i=0;i<4;++i)
        {
            auto other=candidate;other.header.record=Key("alternative",i+1);other.color={};
            other.semantic=i==0?C::SemanticKind::Depth:(i==3?C::SemanticKind::Color:C::SemanticKind::Motion);
            if(i==0){C::DepthDescription depth;depth.kind=Known(C::DepthKind::Device);other.depth=Known(store.Publish(depth));}
            else if(i==3)other.color=candidate.color;
            else {auto motion=Analytic::MotionInput().motion;motion.units=Known(C::MotionUnits::ActiveRasterUV);other.motion=Known(store.Publish(motion));}
            auto resource=f.source;resource.identity=NrContextTests::Resource(i+3,resource.raster).identity;
            other.payload=Known(C::CandidateValue{store.Publish(resource,C::OwnerDomain::Resource)});
            store.alternatives[i]=other;candidates[i+1]=other;
        }
        C::ObservationSet observations;observations.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,3);observations.boundary=f.boundary;
        C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;
        for(const auto& item:candidates)refs.Push({Sym(C::AcquisitionCandidate::WireName),item.header.record,1});
        observations.candidates=NrContextTests::PublishList(store,refs);
        const auto built=S::BuildCanonicalFrameContext(observations,std::span(candidates),request,store);
        if(!built.context)throw std::runtime_error("C02 integration fixture failed");
        auto context=built.context->Value();S::SemanticProfile profile;
        profile.key=f.profile;profile.purpose=Sym("color");profile.rule=Sym("CTX.Semantics");profile.publication=Key("profile");
        profile.requirements.Push({Sym("color.scene-linear"),true});profile.sourceSchemas.Push({candidate.sourceSchema,{1,0}});
        profile.acceptedEvidence.Push(C::SourceClass::Native);profile.maximumAge=Known(uint64_t{0});profile.boundary=C::BoundaryKind::BeforeUpscale;
        profile.generationAxes.Push({C::IdentityKind::RasterGeneration,Sym("raster"),Sym("test002.owner")});
        const auto qualified=S::EvaluateSemanticCapability(context,std::span(candidates),profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,4),store);
        if(!qualified.certificate||qualified.certificate->Value().eligibility!=C::Eligibility::Eligible)throw std::runtime_error("C04 integration fixture failed");
        auto certificate=qualified.certificate->Value();store.candidate=&candidate;
        S::RepresentationRequirement req;req.kind=C::SemanticKind::Color;req.field=Sym("color.scene-linear");req.required=true;
        req.selected=*refs.Get(0);req.mapping=f.mapping;req.output=f.target.descriptor;req.precision=Sym("fp16");req.allocationClass=Sym("scratch");req.capabilities=f.caps;
        S::SourceRepresentationFacts facts;facts.source=f.source.identity;facts.precision=Known(Sym("fp16"));facts.capabilities=Known(f.caps);
        std::vector<S::PreparedViewEvidence> prepared;
        auto probe=[&]{return S::DeriveRepresentationPlan(context,certificate,profile,req,facts,prepared,store);};
        req.kind=C::SemanticKind::Depth;req.field=Sym("depth.device");req.selected=*refs.Get(1);req.required=false;
        const auto skipped=probe();
        r.Check("T15.unrequested-zero-work",skipped.path==S::PreparationPath::NoWork&&!skipped.key&&!skipped.allocation&&store.lookups==0&&store.interns==0,true,"DeriveRepresentationPlan");
        req.kind=C::SemanticKind::Motion;req.field=Sym("motion.canonical");req.selected=*refs.Get(2);
        const auto unusedMotion=probe();
        r.Check("T15.unrequested-motion-zero-work",unusedMotion.path==S::PreparationPath::NoWork&&!unusedMotion.key&&!unusedMotion.allocation&&store.lookups==0&&store.interns==0,true,"DeriveRepresentationPlan");
        req.requested=true;req.required=true;req.kind=C::SemanticKind::Color;req.field=Sym("color.scene-linear");req.selected=*refs.Get(0);
        const auto direct=probe();r.Check("T15.direct-zero-preparation",direct.path==S::PreparationPath::DirectUse&&!direct.allocation&&direct.evidence.preparedLookups==0,true,"DeriveRepresentationPlan");
        const auto directPlan=S::PublishRepresentationPlan(direct,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,60),Ev(),store);
        r.Check("C12.direct-publication",directPlan&&directPlan->Value().transforms.count==0&&!directPlan->Value().keys.allocation.IsKnown(),true,"PublishRepresentationPlan");
        r.Check("C12.success-no-refusal",!direct.evidence.refusal.reason.ownerCode.has_value(),true,"DeriveRepresentationPlan");
        req.precision=Sym("fp32");
        r.Check("T14.missing-conversion-recipe",probe().path==S::PreparationPath::Unavailable,true,"DeriveRepresentationPlan");req.precision=Sym("fp16");
        req.mapping.offset=Known(C::Vec2{1,0});req.metadataOnly=Known(true);
        const auto metadata=probe();r.Check("T15.metadata-only",metadata.path==S::PreparationPath::MetadataOnly,true,"DeriveRepresentationPlan");
        r.Check("C12.metadata-publication",S::PublishRepresentationPlan(metadata,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,61),Ev(),store).has_value(),true,"PublishRepresentationPlan");
        req.mapping=f.mapping;req.metadataOnly={};
        C::TransformStep step;step.kind=C::TransformKind::ExposureUndo;step.operation=Sym("fixture.scale");step.version=1;
        step.sourceUnits=Known(Sym("linear"));step.targetUnits=Known(Sym("linear"));step.alreadyApplied=Known(false);step.qualification=Known(f.meaning);
        C::BoundedList<C::SemanticClaim,8> params;C::SemanticClaim factor;factor.field=Sym("factor");factor.effective=Known(C::ScalarValue{.5});factor.raw=factor.effective;params.Push(factor);
        step.parameters=NrContextTests::PublishList(store,params);req.transforms.Push(step);
        S::FusionRule consumer;consumer.operation=Sym("consumer.affine");consumer.version=1;consumer.steps=req.transforms;consumer.mapping=req.mapping;consumer.precision=req.precision;
        consumer.output=req.output;consumer.capabilities=req.capabilities;req.consumerTransform=consumer;
        const auto deferred=probe();r.Check("T15.consumer-defer",deferred.path==S::PreparationPath::ConsumerTransform,true,"DeriveRepresentationPlan");
        r.Check("C12.consumer-publication",S::PublishRepresentationPlan(deferred,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,62),Ev(),store).has_value(),true,"PublishRepresentationPlan");
        req.output.format=Known(Sym("RGBA32F"));
        r.Check("T15.consumer-output-not-declared",probe().path!=S::PreparationPath::ConsumerTransform,true,"DeriveRepresentationPlan");req.output=f.target.descriptor;
        req.consumerTransform.reset();const auto conversion=probe();
        r.Check("T15.one-conversion",conversion.path==S::PreparationPath::NewConversion&&conversion.evidence.conversions==1&&conversion.evidence.allocationKeys==1,true,"DeriveRepresentationPlan");
        const auto published=S::PublishRepresentationPlan(conversion,NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Context,50),Ev(),store);
        r.Check("C12.canonical-publication",published&&published->Value().Check()==C::Error::None,true,"PublishRepresentationPlan");
        if(!published||!conversion.key)return r.Finish();
        auto plan=published->Value();auto content=S::BuildPreparedContentKey(*conversion.key,f.source,f.target,f.frame,{},false,store);
        if(!content)throw std::runtime_error("prepared integration fixture failed");
        C::PreparedView view;view.header=NrContextTests::Header(C::ContractId::C12,C::OwnerDomain::Resource,51);
        view.plan={Sym(C::RepresentationPlan::WireName),plan.header.record,plan.header.revision};view.keys=plan.keys;
        view.view=store.Publish(f.target,C::OwnerDomain::Resource);auto applied=req.transforms;applied.Get(0)->alreadyApplied=Known(true);
        view.appliedTransforms=NrContextTests::PublishList(store,applied);
        prepared.push_back({&view,&plan,&*content,{Sym(C::PreparedView::WireName),view.header.record,view.header.revision},plan.keys.semantic,plan.keys.plan});
        const auto existing=probe();
        r.Check("T13.existing-before-conversion",existing.path==S::PreparationPath::ExistingPreparedView&&existing.evidence.conversions==0&&!existing.allocation,true,"DeriveRepresentationPlan");
        for(int rights=0;rights<3;++rights)
        {
            store.fixtureRightsState=rights;
            const auto beforeLookup=store.lookups;
            const auto unchanged=probe();
            r.Check(std::string("T13.plan-rights-independent-")+std::to_string(rights),unchanged.path==S::PreparationPath::ExistingPreparedView&&unchanged.evidence.conversions==0,true,"DeriveRepresentationPlan");
            r.Check(std::string("T14.lineage-rights-independent-")+std::to_string(rights),unchanged.key==existing.key&&unchanged.delta==existing.delta,true,"DeriveRepresentationPlan");
            r.Check(std::string("T15.rights-no-alternative-")+std::to_string(rights),store.lookups==beforeLookup+1&&unchanged.evidence.allocationKeys==0,true,"DeriveRepresentationPlan");
        }
        const auto originalContext=context;const auto originalCertificate=certificate;
        C::GenerationVector changedGenerations;changedGenerations.Insert(Gen(C::IdentityKind::RasterGeneration,"raster",2));
        context.generations=store.Publish(changedGenerations,C::OwnerDomain::IdentityRegistry);++context.header.revision;
        const auto requalified=S::EvaluateSemanticCapability(context,std::span(candidates),profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,7),store);
        if(!requalified.certificate)throw std::runtime_error("structural requalification missing");certificate=requalified.certificate->Value();
        const auto changedPlan=probe();
        r.Check("T13.qualified-generation-replans",changedPlan.path==S::PreparationPath::NewConversion&&changedPlan.key!=existing.key,true,"DeriveRepresentationPlan");
        context=originalContext;certificate=originalCertificate;
        const auto originalProfile=profile;profile.publication=Key("profile.changed");++profile.key.version;
        const auto changedConsumer=S::EvaluateSemanticCapability(context,std::span(candidates),profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,8),store);
        if(!changedConsumer.certificate)throw std::runtime_error("consumer qualification missing");certificate=changedConsumer.certificate->Value();
        const auto consumerPlan=probe();
        r.Check("T13.consumer-not-semantic",consumerPlan.key&&existing.key&&consumerPlan.key->semantic==existing.key->semantic&&consumerPlan.key!=existing.key,true,"DeriveRepresentationPlan");
        profile=originalProfile;certificate=originalCertificate;
        auto original=candidate;candidate.header.revision=2;
        r.Check("C12.source-revision-bound",probe().path==S::PreparationPath::Unavailable,true,"DeriveRepresentationPlan");candidate=original;
        ++certificate.context.revision;r.Check("C12.context-bound",probe().path==S::PreparationPath::Unavailable,true,"DeriveRepresentationPlan");--certificate.context.revision;
        facts.source.contentRevision=Known(C::ContentRevision{2});r.Check("C12.source-facts-bound",probe().path==S::PreparationPath::Unavailable,true,"DeriveRepresentationPlan");facts.source=f.source.identity;
        req.selected.record=Key("unselected");r.Check("T15.no-alternative-preparation",probe().path==S::PreparationPath::Unavailable,true,"DeriveRepresentationPlan");req.selected=*refs.Get(0);
        req.requested=false;req.required=false;req.kind=C::SemanticKind::Mask;req.field=Sym("mask.reactive");facts={};const auto before=store.lookups;const auto optional=probe();
        r.Check("T15.optional-unknown-zero-work",optional.path==S::PreparationPath::NoWork&&store.lookups==before&&optional.evidence.planKeys==0&&optional.evidence.preparedLookups==0,true,"DeriveRepresentationPlan");
        // Required depth is direct. Both qualified motion alternatives remain untouched.
        req.requested=true;req.required=true;req.transforms={};req.kind=C::SemanticKind::Depth;req.field=Sym("depth.device");req.selected=*refs.Get(1);
        profile.requirements={};profile.requirements.Push({req.field,true});profile.publication=Key("profile.depth");profile.key.version=2;
        const auto depthQualified=S::EvaluateSemanticCapability(context,std::span(candidates),profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,5),store);
        if(!depthQualified.certificate)throw std::runtime_error("depth qualification missing");certificate=depthQualified.certificate->Value();
        const auto depthResource=S::ResolveMetadata(std::get<C::MetadataRef<C::ResourceView>>(candidates[1].payload.KnownPart()->value),store);
        facts.source=depthResource->identity;facts.precision=Known(Sym("fp16"));facts.capabilities=Known(f.caps);
        const auto beforeDepth=store.lookups;const auto depthDirect=probe();
        r.Check("T15.required-depth-direct",depthDirect.path==S::PreparationPath::DirectUse&&store.lookups==beforeDepth+1&&depthDirect.evidence.conversions==0,true,"DeriveRepresentationPlan");
        req.kind=C::SemanticKind::Motion;req.field=Sym("motion.canonical");req.selected=*refs.Get(2);
        profile.requirements={};profile.requirements.Push({req.field,true});profile.publication=Key("profile.motion");profile.key.version=3;
        const auto motionQualified=S::EvaluateSemanticCapability(context,std::span(candidates),profile,NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,6),store);
        if(!motionQualified.certificate)throw std::runtime_error("motion qualification missing");certificate=motionQualified.certificate->Value();
        const auto motionResource=S::ResolveMetadata(std::get<C::MetadataRef<C::ResourceView>>(candidates[2].payload.KnownPart()->value),store);
        facts.source=motionResource->identity;const auto beforeMotion=store.lookups;const auto motionDirect=probe();
        r.Check("T15.one-of-two-motion",motionDirect.path==S::PreparationPath::DirectUse&&store.lookups==beforeMotion+1&&motionDirect.evidence.conversions==0,true,"DeriveRepresentationPlan");
#else
        r.Check("T15.planner-missing",false,true,"Representation planner unavailable");
#endif
        return r.Finish();});
}
