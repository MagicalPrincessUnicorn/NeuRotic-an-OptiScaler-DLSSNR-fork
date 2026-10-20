#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#include <inputs/universal_feeder/providers/NativeObservationOwner.h>
#include <inputs/universal_feeder/providers/NgxObservationAdapter.h>
#include <nr/lifecycle/NativeOwnerSet.h>
#include <nr/protocol/BindingPlan.h>
#if __has_include(<nr/context/NativeContextQualification.h>)
#include <nr/context/NativeContextQualification.h>
#define NR_NATIVE_CONTEXT 1
#endif
using namespace NrTest002;
namespace F=Neurotic::Feed;namespace L=Neurotic::Lifecycle;namespace S=Neurotic::Context;namespace P=Neurotic::Protocol;
struct Parameters
{
    int resources[6]{};bool missingY=false;float preExposure=1.25f;
    int Get(const char* key,unsigned* value)
    {*value=std::string_view(key)=="Reset"?1u:800u;return 0;}
    int Get(const char* key,float* value)
    {
        const std::string_view name=key;
        if(name=="Jitter.Offset.Y"&&missingY)return 1;
        *value=name=="MV.Scale.X"?2.5f:name=="MV.Scale.Y"?-3.f:name=="Jitter.Offset.X"?2.f:
            name=="Jitter.Offset.Y"?-1.f:name=="DLSS.Pre.Exposure"?preExposure:1.f;return 0;
    }
    int Get(const char* key,void** value)
    {
        constexpr std::array names={"Color","Output","Depth","MotionVectors","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
        for(std::size_t i=0;i<names.size();++i)if(std::string_view(key)==names[i]){*value=&resources[i];return 0;}
        *value=nullptr;return 1;
    }
};
#ifdef NR_NATIVE_CONTEXT
struct Fixture
{
    L::NativeOwnerSet owners{Sym("native.context.fixture")};
    std::unique_ptr<L::OwnerMetadataArena> arena=owners.NewInvocationMetadata(8*1024*1024,2048);
    L::OwnerMetadataArena::Publisher context=arena->ForOwner(C::OwnerDomain::Context);
    S::ContextRequest request;S::SemanticProfile profile;C::RecordHeader certificate;
    Parameters parameters;int feature=0;std::string_view acquisitionBoundary;
    std::unique_ptr<F::NativeObservationOwner> port;
    explicit Fixture(bool missingY=false,std::string_view boundary="evaluate",bool nativeSample=false,
                     bool wrongCallback=false,bool wrongMotion=false,bool enrolledScope=false,
                     bool unknownContent=false,bool invalidStructure=false,bool unknownProviderVersion=false,
                     bool independentJitter=false,bool unknownEpisode=false):acquisitionBoundary(boundary)
    {
        parameters.missingY=missingY;
        auto source=arena->ForOwner(C::OwnerDomain::Provider);auto resource=arena->ForOwner(C::OwnerDomain::Resource);
        F::SourceContext seed;seed.callback=owners.SourceJournal().Event().evidence.record;
        if(enrolledScope)[]<class T>(T& value){
            if constexpr(requires{value.enrolledScope;})value.enrolledScope=C::ScopeRef{Key("native.enrolled.scope",1)};
        }(seed);
        seed.evidence=owners.SourceJournal().Event().evidence;seed.lineageRoot=seed.evidence.record;
        seed.seed.provider=owners.SourceJournal().Binding().subject;
        seed.seed.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
        if(!unknownProviderVersion)seed.seed.sourceVersion=Known(C::VersionNumber{1,0});
        auto frame=NrContextTests::Frame();
        if(unknownEpisode)frame.episodeId={};
        if(nativeSample)
        {
            C::NativeSampleIdentityV1 sample;
            sample.session=frame.sessionId.KnownPart()->value;sample.stream=frame.renderStreamId.KnownPart()->value;
            sample.view=frame.viewId.KnownPart()->value;sample.feature=Id<C::ObjectIncarnation>("feature",1);
            sample.ingress=Id<C::ObjectIncarnation>("ingress",1);sample.producerOrdinal=1;sample.callback=seed.callback;
            if(wrongCallback)++sample.callback.value;
            frame.nativeSample=source.Publish(sample);frame.baseRealFrameId={};
        }
        seed.seed.frame=source.Publish(frame);
        seed.seed.masks=source.Publish(C::MaskSet{});seed.seed.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);
        seed.seed.boundary.episode=frame.episodeId;seed.generations=source.Publish(C::GenerationVector{});
        C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);seed.seed.color=Known(source.Publish(color));
        C::DepthDescription depth;depth.kind=Known(C::DepthKind::Device);depth.reversed=Known(false);seed.seed.depth=Known(source.Publish(depth));
        auto motion=Analytic::MotionInput().motion;motion.framePair.current=frame.baseRealFrameId;
        if(nativeSample){motion.framePair={};motion.nativeSample=frame.nativeSample;}
        if(wrongMotion&&motion.nativeSample)++motion.nativeSample->revision;
        seed.seed.motion=Known(source.Publish(motion));
        const auto raster=motion.currentRaster;F::NativeObservationOwner::Resources resources;
        auto renderRaster=raster;
        if(independentJitter)
        {auto active=renderRaster.active.KnownPart()->value;active.width/=2;active.height/=2;renderRaster.active=Known(active);}
        constexpr std::array names={"Color","Output","Depth","MotionVectors","ExposureTexture","DLSS.Input.Bias.Current.Color.Mask"};
        for(std::size_t i=0;i<names.size();++i)
        {
            auto physical=NrContextTests::Resource(10+i,raster);
            physical.descriptor.allocation=Known(C::Extent{1024,512});physical.descriptor.format=Known(Sym("RGBA16F"));
            physical.descriptor.sampleCount=Known(std::uint32_t{1});physical.descriptor.mipLevels=Known(std::uint32_t{1});
            physical.descriptor.arrayLayers=Known(std::uint32_t{1});physical.descriptor.api=Known(C::GraphicsApi::D3D12);
            physical.mip=Known(std::uint32_t{0});physical.arrayLayer=Known(std::uint32_t{0});physical.plane=Known(std::uint32_t{0});
            physical.device=Known(Id<C::ObjectIncarnation>("device",1));physical.adapter=Known(Sym("adapter"));
            if(unknownContent)physical.identity.contentRevision={};
            if(invalidStructure)physical.descriptor.api={};
            if(i==0&&independentJitter){physical.raster=renderRaster;seed.jitterBasis=Known(resource.Publish(physical));}
            resources.Push({Sym(names[i]),&parameters.resources[i],{Known(resource.Publish(physical)),{}}});
        }
        port=std::make_unique<F::NativeObservationOwner>(owners.SourceJournal(),*arena,seed,
            F::SourceDescriptor{"NGX",C::GraphicsApi::D3D12,acquisitionBoundary,7},&feature,resources);
        request.header=owners.ContextJournal().Header(C::ContractId::C02,{seed.callback});request.frame=seed.seed.frame;
        if(enrolledScope)request.header.scope={Key("native.enrolled.scope",1)};
        request.generations=seed.generations;request.renderRaster=context.Publish(renderRaster);request.outputRaster=request.renderRaster;
        request.continuity=context.Publish(C::EvidenceVector{});request.boundary=seed.seed.boundary;
        profile.key={Sym("Native"),Sym("Legacy"),1};profile.purpose=Sym("RENDER.NativeSemantics");profile.rule=Sym("CTX.Semantics");
        profile.publication=owners.ContextJournal().Event().evidence.record;profile.maximumAge=Known(std::uint64_t{0});
        if(nativeSample)
        {
            profile.maximumAge={};
            []<class T>(T& value){if constexpr(requires{value.nativeSampleDomainVersion;})value.nativeSampleDomainVersion=1;}(profile);
        }
        profile.boundary=C::BoundaryKind::BeforeUpscale;profile.acceptedEvidence.Push(C::SourceClass::Native);
        profile.sourceSchemas.Push({Sym("NGX"),{1,0}});profile.requirements.Push({Sym("color.domain"),true});
        profile.requirements.Push({Sym("depth.device"),true});
        if(nativeSample)profile.requirements.Push({Sym("motion.native"),true});
        certificate=owners.ContextJournal().Header(C::ContractId::C04,request.header.scope);
    }
    auto Run(int extra=0)
    {
        {F::ObservationScope scope(*port);F::Callback callback({"NGX",C::GraphicsApi::D3D12,acquisitionBoundary,7},&feature);
         F::ObserveNgxEvaluation(callback,&parameters,0);
         if(extra==1)callback.Value("MV.Scale.X",4.f);
         if(extra==3)callback.Value("adapter.version",1u);
         if(extra==2)callback.Scalar("Reset",Known(C::ScalarValue{std::uint64_t{1}}),
            C::OptionalFact<C::ScalarValue>::FromUnknown(S::Missing("owner.revoked",C::UnknownReason::Revoked)));}
        return S::BuildNativeContext(*port,request,profile,certificate,context);
    }
};
#endif
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-context-chain",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_CONTEXT
    auto f=std::make_unique<Fixture>();const auto result=f->Run();
    if(!result.context)std::cerr<<"Native context refusal: "<<result.reason.View()<<'\n';
    r.Check("T30.native-context-full-ngx-retained",f->port->Candidates().size()==28,true,"ObserveNgxEvaluation");
    r.Check("T30.native-context-qualified-chain",result.context&&result.certificate&&result.certificate->eligibility==C::Eligibility::Eligible,true,"BuildNativeContext");
    P::BindingPlan bindings;const bool built=result.context&&P::BuildFrameControls(*result.context,Ev(1),f->context,bindings);
    const auto numeric=[&](const char* key,double expected){const auto* binding=P::FindBinding(bindings,key);
        const auto* value=binding?P::Number(binding->effective):nullptr;return built&&value&&*value==expected;};
    r.Check("T30.native-context-mv-nonneutral",numeric("DLSSNR.MVecScaleX",2.5)&&numeric("DLSSNR.MVecScaleY",-3),true,"BuildFrameControls");
    r.Check("T30.native-context-pixel-jitter-once",numeric("Jitter.Offset.X",2)&&numeric("Jitter.Offset.Y",-1),true,"BuildFrameControls");
    r.Check("T30.native-context-preexposure",numeric("DLSS.Pre.Exposure",1.25),true,"BuildFrameControls");
    const auto* reset=P::FindBinding(bindings,"DLSSNR.Reset");
    r.Check("T29.native-context-requested-reset",built&&reset&&reset->effective.IsKnown()&&
        reset->effective.KnownPart()->value==C::ScalarValue{std::uint64_t{1}},true,"BuildFrameControls");
    auto conflict=std::make_unique<Fixture>();r.Check("T30.native-context-conflicting-control",!conflict->Run(1).context,true,"BuildNativeContext");
    auto revoked=std::make_unique<Fixture>();r.Check("T29.native-context-explicit-revocation",!revoked->Run(2).context,true,"BuildNativeContext");
    auto creation=std::make_unique<Fixture>(false,"create");r.Check("T30.native-context-wrong-acquisition-boundary",!creation->Run().context,true,"BuildNativeContext");
    auto missing=std::make_unique<Fixture>(true);const auto optional=missing->Run();P::BindingPlan neutral;
    const bool mapped=optional.context&&P::BuildFrameControls(*optional.context,Ev(1),missing->context,neutral);
    const auto* x=P::FindBinding(neutral,"Jitter.Offset.X");const auto* y=P::FindBinding(neutral,"Jitter.Offset.Y");
    r.Check("T30.native-context-missing-jitter-pair",mapped&&x&&y&&P::Number(x->effective)&&P::Number(y->effective)&&
        *P::Number(x->effective)==0&&*P::Number(y->effective)==0,true,"BuildFrameControls");
    const std::array<float,3> exposures={1e-8f,1e-6f,std::nextafter(1e-6f,1.f)};
    for(std::size_t i=0;i<exposures.size();++i)
    {
        auto threshold=std::make_unique<Fixture>();threshold->parameters.preExposure=exposures[i];
        const auto value=threshold->Run();P::BindingPlan controls;
        const bool valid=value.context&&P::BuildFrameControls(*value.context,Ev(1),threshold->context,controls);
        const auto* b=P::FindBinding(controls,"DLSS.Pre.Exposure");const double expected=i<2?1.0:static_cast<double>(exposures[i]);
        r.Check("T30.native-preexposure-threshold-"+std::to_string(i),valid&&b&&P::Number(b->effective)&&
            *P::Number(b->effective)==expected,true,"BuildFrameControls");
        r.Check("T30.native-preexposure-raw-preserved-"+std::to_string(i),valid&&b&&P::Number(b->raw)&&
            *P::Number(b->raw)==static_cast<double>(exposures[i]),true,"BuildFrameControls");
    }
#else
    r.Check("T30.native-context-production-chain",false,true,"BuildNativeContext");
#endif
    return r.Finish();});
}
