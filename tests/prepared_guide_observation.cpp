#define main OriginalFeedTestMain
#include "nr/feed/nr_feed_observation_tests.cpp"
#undef main
#include <inputs/universal_feeder/providers/PreparedGuideObservationAdapter.h>
#include <dlssnr/PreparedGuideFrame.h>
#include <shaders/dlssnr/DlssNr_Common.h>
#include <nr/lifecycle/PreparedObservationPublication.h>
int main()
{
    FixturePort p;
    F::Prepared::Descriptor d;d.producer=1;d.session=1;d.stream=1;d.device=1;d.generation=1;
    d.capture=2;d.previousCapture=1;d.color=d.depth=d.motion={16,16,0,0,16,16};
    const auto values=Neurotic::Context::NormalizePreparedGuides(d);
    if(!values)return 1;
    {F::ObservationScope scope(p);F::Callback cb({"NeuRotic.PreparedGuides",C::GraphicsApi::D3D12,"prepared",1,C::SourceClass::External},&p);
     F::ObservePreparedGuides(cb,*values.value,reinterpret_cast<void*>(1),reinterpret_cast<void*>(2),reinterpret_cast<void*>(3),nullptr);}
    bool depth=false,motion=false,jitter=false;
    for(const auto& candidate:p.candidates)
    {
        auto evidence=Neurotic::Context::ResolveMetadata(candidate.evidence,p.store);
        if(!evidence||!evidence->provenance.IsKnown())return 2;
        const auto origin=evidence->provenance.KnownPart()->value;
        if(origin==C::SourceClass::Native)return 3;
        if(candidate.semantic==C::SemanticKind::Depth)depth=origin==C::SourceClass::HostObserved;
        if(candidate.semantic==C::SemanticKind::Motion)motion=origin==C::SourceClass::Derived;
        const C::BoundedList<C::SemanticClaim,16>* claims=nullptr;
        if(!Neurotic::Context::ResolveList(candidate.claims,p.store,claims))return 4;
        for(const auto& claim:*claims)if(claim.field.View()=="prepared.jitterX")
            jitter=!claim.raw.IsKnown()&&claim.effective.IsKnown();
    }
    if(!depth||!motion||!jitter)return 5;
    // Exercise the production entry's real bounded journal, without a fixture
    // ObservationScope. Metadata must remain truthful about unknown real frames.
    auto live=Neurotic::Lifecycle::PreparedObservationPublication::Capture(*values.value,&p,&d,&p,&values,nullptr);
    if(!live || live->Publication().Candidates().size()!=19){std::printf("Live publication: %s, candidates %zu\n",live?"present":"absent",live?live->Publication().Candidates().size():0);return 7;}
    const auto* liveFrame=Neurotic::Context::ResolveMetadata(live->Publication().Source().seed.frame,live->Metadata());
    if(!liveFrame || liveFrame->baseRealFrameId.IsKnown() || liveFrame->nativeSample)return 8;
    std::array<std::shared_ptr<Neurotic::Lifecycle::PreparedObservationPublication>,7> pending;
    for(auto& slot:pending){slot=Neurotic::Lifecycle::PreparedObservationPublication::Capture(*values.value,&p,&d,&p,&values,nullptr);if(!slot)return 9;}
    if(Neurotic::Lifecycle::PreparedObservationPublication::Capture(*values.value,&p,&d,&p,&values,nullptr))return 10;
    pending[0].reset();
    pending[0]=Neurotic::Lifecycle::PreparedObservationPublication::Capture(*values.value,&p,&d,&p,&values,nullptr);
    if(!pending[0])return 11;
    DlssNrFrameInfo frame;DlssNr::ProjectPreparedGuideFrame(*values.value,frame);
    if(!frame.PreparedSource||frame.PreparedSource->capture!=2||frame.MotionSubrectWidth!=16||frame.ExposureTexture)return 6;
    std::puts("PASS: per-signal prepared provenance and frame projection");return 0;
}
