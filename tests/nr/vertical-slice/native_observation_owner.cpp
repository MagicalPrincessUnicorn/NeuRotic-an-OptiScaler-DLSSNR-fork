#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#if __has_include(<inputs/universal_feeder/providers/NativeObservationOwner.h>)
#include <inputs/universal_feeder/providers/NativeObservationOwner.h>
#include <nr/lifecycle/NativeOwnerSet.h>
#define NR_NATIVE_OBSERVATION_OWNER 1
#endif
using namespace NrTest002;
int main(int argc,char**argv)
{
 return Guard([&]{Report r("native-observation-owner",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_NATIVE_OBSERVATION_OWNER
    namespace F=Neurotic::Feed;namespace L=Neurotic::Lifecycle;
    auto owners=std::make_unique<L::NativeOwnerSet>(Sym("native.feed.fixture"));
    auto arena=owners->NewInvocationMetadata(4*1024*1024,512);
    auto metadata=arena->ForOwner(C::OwnerDomain::Provider);
    auto resource=arena->ForOwner(C::OwnerDomain::Resource);
    F::SourceContext context;context.callback=owners->SourceJournal().Event().evidence.record;
    context.evidence=owners->SourceJournal().Event().evidence;context.lineageRoot=context.evidence.record;
    context.seed.provider=owners->SourceJournal().Binding().subject;
    context.seed.providerIncarnation=Known(Id<C::ProviderIncarnation>("provider",1));
    context.seed.sourceVersion=Known(C::VersionNumber{1,0});
    context.seed.frame=metadata.Publish(NrContextTests::Frame());context.seed.masks=metadata.Publish(C::MaskSet{});
    context.seed.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);
    context.seed.boundary.episode=NrContextTests::Frame().episodeId;
    context.generations=metadata.Publish(C::GenerationVector{});
    const auto raster=Analytic::Raster({0,0,800,400},{1024,512});
    const auto view=resource.Publish(NrContextTests::Resource(10,raster));
    int feature=0,other=0,native=0;F::NativeObservationOwner::Resources resources;
    resources.Push({Sym("Color"),&native,{Known(view),{}}});
    const F::SourceDescriptor descriptor{"NGX",C::GraphicsApi::D3D12,"evaluate",7};
    auto port=std::make_unique<F::NativeObservationOwner>(owners->SourceJournal(),*arena,context,
        descriptor,&feature,resources);
    r.Check("T30.native-source-wrong-callback",!port->Begin(descriptor,&other),true,"NativeObservationOwner");
    auto foreign=descriptor;foreign.localGeneration=8;
    r.Check("T30.native-source-generation-refused",!port->Begin(foreign,&feature),true,"NativeObservationOwner");
    {
        F::ObservationScope scope(*port);F::Callback callback(descriptor,&feature);
        r.Check("T30.native-source-current-active",callback.Active(),true,"NativeObservationOwner");
        callback.Resource("Color",C::SemanticKind::Color,&native);
        callback.Resource("Color",C::SemanticKind::Color,&other);
        callback.Value("Reset",0u);
        r.Check("T30.native-source-reentry-refused",!port->Begin(descriptor,&feature),true,"NativeObservationOwner");
    }
    const auto candidates=port->Candidates();
    r.Check("T30.native-source-real-feed-publications",port->Healthy()&&candidates.size()==3&&port->Receipts().size()==3,true,"NativeObservationOwner");
    r.Check("T30.native-source-owner-sequence",port->Receipts().size()==3&&
        port->Receipts()[0].ownerSequence<port->Receipts()[1].ownerSequence&&
        port->Receipts()[1].ownerSequence<port->Receipts()[2].ownerSequence,true,"NativeObservationOwner");
    r.Check("T30.native-source-exact-resource",candidates.size()==3&&candidates[0].payload.IsKnown()&&
        !candidates[1].payload.IsKnown(),true,"NativeObservationOwner");
    const auto set=port->Observations();
    r.Check("T30.native-source-owned-observation-set",set&&set->Check()==C::Error::None&&
        set->header.owner==C::OwnerDomain::Provider&&set->candidates.count==3,true,"NativeObservationOwner");
    port->Close();
    r.Check("T30.native-source-expired-refused",!port->Begin(descriptor,&feature)&&
        !port->Project("Color",&native).view.IsKnown(),true,"NativeObservationOwner");
    auto wrong=context;wrong.seed.provider=Key("foreign");
    auto rejected=std::make_unique<F::NativeObservationOwner>(owners->SourceJournal(),*arena,wrong,descriptor,&feature,resources);
    r.Check("T30.native-source-foreign-owner-refused",!rejected->Healthy()&&!rejected->Begin(descriptor,&feature),true,"NativeObservationOwner");
    auto exhausted=std::make_unique<F::NativeObservationOwner>(owners->SourceJournal(),*arena,context,descriptor,&feature,resources);
    {F::ObservationScope scope(*exhausted);F::Callback callback(descriptor,&feature);
     callback.Value("Reset",1u);
     try{for(std::size_t i=0;i<512;++i)metadata.Publish(C::Symbol{});}catch(const L::MetadataRefusal&){}
     callback.Value("Reset",0u);}
    r.Check("T29.native-source-partial-publication-refused",!exhausted->Healthy()&&!exhausted->Observations(),true,"NativeObservationOwner");
    r.Check("T29.native-source-partial-evidence-retained",exhausted->Candidates().size()==1&&exhausted->Receipts().size()==1,true,"NativeObservationOwner");
    auto journalFailure=std::make_unique<F::NativeObservationOwner>(owners->SourceJournal(),*arena,context,descriptor,&feature,resources);
    {F::ObservationScope scope(*journalFailure);F::Callback callback(descriptor,&feature);
     auto moved=std::move(owners->SourceJournal());callback.Value("Reset",0u);}
    r.Check("T29.native-source-journal-failure-refused",!journalFailure->Healthy(),true,"NativeObservationOwner");
#else
    r.Check("T30.native-source-production-owner",false,true,"NativeObservationOwner");
#endif
    return r.Finish();});
}
