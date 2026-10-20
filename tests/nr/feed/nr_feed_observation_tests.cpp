#include "../contracts/Test002Support.h"
#include "../context/ContextFixtureStore.h"
#include "../oracles/Test002AnalyticCases.h"
#include <nr/context/CapabilityQualification.h>
#include <nr/lifecycle/ConsumptionLeaseAdapter.h>
#if __has_include(<inputs/universal_feeder/providers/ObservationPublisher.h>)
#include <inputs/universal_feeder/providers/ObservationPublisher.h>
#define NR_FEED_IMPLEMENTED 1
#endif
#if __has_include(<inputs/universal_feeder/providers/NgxObservationAdapter.h>)
#include <inputs/universal_feeder/providers/NgxObservationAdapter.h>
#include <inputs/universal_feeder/providers/FsrObservationAdapter.h>
#include <inputs/universal_feeder/providers/XessObservationAdapter.h>
#include <inputs/universal_feeder/providers/StreamlineObservationAdapter.h>
#include <inputs/universal_feeder/providers/PresentGuideObservationAdapter.h>
#define NR_FEED_FAMILIES 1
#endif
using namespace NrTest002;
#ifdef NR_FEED_IMPLEMENTED
namespace F=Neurotic::Feed;
struct FixturePort final:F::PublicationPort
{
    NrContextTests::Store store;
    F::SourceContext context;
    std::vector<C::AcquisitionCandidate> candidates;
    std::vector<C::OwnerReceipt> receipts;
    std::vector<C::OptionalFact<C::RecordReference>> rights;
    std::uint64_t sequence=100;
    int begins=0,projections=0;
    bool throwBegin=false,throwDeliver=false;
    F::ResourceProjection projection;
    FixturePort()
    {
        context.seed.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Provider,1);
        context.seed.provider=Key("feed.provider",1);
        context.seed.providerIncarnation=Known(Id<C::ProviderIncarnation>("feed.provider",1));
        context.seed.frame=store.Publish(NrContextTests::Frame(),C::OwnerDomain::IdentityRegistry);
        context.seed.masks=store.Publish(C::MaskSet{},C::OwnerDomain::Provider);
        context.seed.boundary.kind=Known(C::BoundaryKind::BeforeUpscale);
        context.seed.boundary.episode=NrContextTests::Frame().episodeId;
        context.generations=store.Publish(C::GenerationVector{},C::OwnerDomain::IdentityRegistry);
        context.lineageRoot=Key("feed.lineage",1);context.callback=Key("feed.callback",1);
        context.evidence=Ev(1);
    }
    std::optional<F::SourceContext> Begin(const F::SourceDescriptor&,const void*) override
    {++begins;if(throwBegin)throw std::runtime_error("owner unavailable");return context;}
    C::RecordHeader Next(C::ContractId id) override
    {return NrContextTests::Header(id,C::OwnerDomain::Provider,++sequence);}
    C::MetadataRef<C::EvidenceVector> StoreEvidence(const C::EvidenceVector& e) override
    {return store.Publish(e,C::OwnerDomain::Provider);}
    C::MetadataList<C::SemanticClaim,16> StoreClaims(const C::BoundedList<C::SemanticClaim,16>& c) override
    {return NrContextTests::PublishList(store,c);}
    C::MetadataList<C::OwnerFact,16> StoreFacts(const C::BoundedList<C::OwnerFact,16>& c) override
    {return NrContextTests::PublishList(store,c);}
    C::MetadataList<C::RecordReference,32> StoreCauses(const C::BoundedList<C::RecordReference,32>& c) override
    {return NrContextTests::PublishList(store,c);}
    F::ResourceProjection Project(std::string_view,const void*) override {++projections;return projection;}
    void Deliver(const C::AcquisitionCandidate& c,const C::OptionalFact<C::RecordReference>& right) override
    {if(throwDeliver)throw std::runtime_error("dropped");candidates.push_back(c);rights.push_back(right);}
    void DeliverReceipt(const C::OwnerReceipt& r) override {receipts.push_back(r);}
    const C::SemanticClaim& Claim(std::size_t index) const
    {return *store.Resolve(*candidates.at(index).claims.backing).value->Get(0);}
};
#endif
int main(int argc,char**argv)
{
 return Guard([&]{Report r("feed-observations",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_FEED_IMPLEMENTED
    FixturePort p;
    F::SourceDescriptor ngx{"NGX",C::GraphicsApi::D3D12,"evaluate"};
    {F::Callback callback(ngx,nullptr);callback.Scalar("jitter.x",{},Known(C::ScalarValue{0.0}));}
    r.Check("T10.off-no-publication",p.begins==0&&p.candidates.empty(),true,"Callback");
    {
      F::ObservationScope enabled(p);
      {F::Callback callback(ngx,nullptr);callback.Scalar("mv.scale.x",{},Known(C::ScalarValue{1.0}));}
      r.Check("T20.missing-not-default",!p.Claim(0).raw.IsKnown(),true,"Callback::Scalar");
      r.Check("T20.effective-separate",p.Claim(0).effective==Known(C::ScalarValue{1.0}),true,"Callback::Scalar");
      r.Check("T20.provider-version-unknown",!p.candidates.front().sourceVersion.IsKnown(),true,"Callback::Scalar");
      {F::Callback callback(ngx,nullptr);callback.Scalar("jitter.x",Known(C::ScalarValue{3.0}));}
      {F::Callback callback(ngx,nullptr);callback.Scalar("jitter.x",{});}
      r.Check("T20.vanished-is-unknown",p.Claim(1).raw.IsKnown()&&!p.Claim(2).raw.IsKnown(),true,"Callback::Scalar");
      {F::Callback callback(ngx,nullptr);callback.Scalar("quality",Known(C::ScalarValue{uint64_t{2}}),Known(C::ScalarValue{uint64_t{4}}));}
      r.Check("T20.raw-not-overwritten",p.Claim(3).raw==Known(C::ScalarValue{uint64_t{2}}),true,"Callback::Scalar");
      const auto before=p.candidates.size();
      {F::Callback callback(ngx,nullptr);callback.Resource("depth",C::SemanticKind::Depth,nullptr);}
      r.Check("T21.no-right-still-publishes",p.candidates.size()==before+1&&!p.rights.back().IsKnown(),true,"Callback::Resource");
      r.Check("T20.no-resource-query-on-null",p.projections==0,true,"Callback::Resource");
      auto old=p.context.seed;old.sourceSchema=Sym("NGX");
      r.Check("T20.creation-same-incarnation",F::CurrentCreation(old,p.context.seed.providerIncarnation)!=nullptr,true,"CurrentCreation");
      r.Check("T20.creation-reincarnated",F::CurrentCreation(old,Known(Id<C::ProviderIncarnation>("feed.provider",2)))==nullptr,true,"CurrentCreation");
      r.Check("T20.creation-unknown-incarnation",F::CurrentCreation(old,{})==nullptr,true,"CurrentCreation");
      {F::Callback upstream({"FSR2",C::GraphicsApi::D3D12,"dispatch"},nullptr);
       F::TranslationScope translated(upstream,"FSR2.to.NGX");
       F::Callback child(ngx,nullptr);child.Scalar("jitter.x",Known(C::ScalarValue{3.0}));}
      const auto alias=p.store.Resolve(p.candidates.back().evidence).value;
      r.Check("T20.translation-alias",alias&&alias->aliasOf==Known(p.context.lineageRoot),true,"TranslationScope");
      r.Check("T20.translation-derived",alias&&alias->provenance==Known(C::SourceClass::Derived),true,"TranslationScope");
      r.Check("T20.translation-not-provider-raw",!p.Claim(p.candidates.size()-1).raw.IsKnown()&&p.Claim(p.candidates.size()-1).effective==Known(C::ScalarValue{3.0}),true,"TranslationScope");
      {F::Callback callback(ngx,nullptr);callback.Scalar("reset",Known(C::ScalarValue{false}),Known(C::ScalarValue{true}));}
      r.Check("T20.raw-reset-false",p.Claim(p.candidates.size()-1).raw==Known(C::ScalarValue{false}),true,"Callback::Scalar");
      r.Check("T20.lexical-alias-ended",!p.store.Resolve(p.candidates.back().evidence).value->aliasOf.IsKnown(),true,"TranslationScope");
      p.throwBegin=true;{F::Callback callback(ngx,nullptr);callback.Scalar("x",Known(C::ScalarValue{1.0}));}p.throwBegin=false;
      r.Check("T10.owner-throw-contained",true,true,"Callback");
      p.throwDeliver=true;{F::Callback callback(ngx,nullptr);callback.Scalar("x",Known(C::ScalarValue{1.0}));}p.throwDeliver=false;
      r.Check("T10.delivery-throw-contained",true,true,"Callback::Scalar");
      const auto bounded=p.candidates.size();
      {F::Callback callback(ngx,nullptr);for(int i=0;i<100;++i)callback.Scalar("x",Known(C::ScalarValue{uint64_t{1}}));}
      r.Check("T10.bounded-callback",p.candidates.size()-bounded==64,true,"Callback::Scalar");
    }
    const auto ended=p.candidates.size();{F::Callback callback(ngx,nullptr);callback.Scalar("x",Known(C::ScalarValue{1.0}));}
    r.Check("T10.unregister-off",p.candidates.size()==ended,true,"ObservationScope");
    r.Check("T10.owner-receipt",!p.receipts.empty()&&p.receipts.front().header.owner==C::OwnerDomain::Provider,true,"DeliverReceipt");
    std::optional<F::Callback> expired;
    {F::ObservationScope registration(p);expired.emplace(ngx,nullptr);}
    const auto expiredCount=p.candidates.size();expired->Scalar("expired",Known(C::ScalarValue{1.0}));
    r.Check("T10.expired-registration",p.candidates.size()==expiredCount,true,"Callback::Scalar");
    expired.reset();
    // Same production FEED -> NFC observations, independently tested through real LIFE admission.
    FixturePort cross;
    const auto raster=Analytic::Raster({0,0,800,400},{1024,512});
    C::MotionDescription motion;motion.currentRaster=raster;
    cross.context.seed.motion=Known(cross.store.Publish(motion,C::OwnerDomain::Provider));
    {F::ObservationScope registration(cross);F::Callback cb(ngx,nullptr);cb.Jitter(Known(C::Vec2{4,-2}));}
    namespace X=Neurotic::Context;namespace L=Neurotic::Lifecycle;
    X::ContextRequest request;
    request.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,700);
    request.frame=cross.context.seed.frame;request.generations=cross.context.generations;
    request.renderRaster=cross.store.Publish(raster);request.outputRaster=request.renderRaster;
    request.boundary=cross.context.seed.boundary;
    request.continuity=cross.store.Publish(C::EvidenceVector{},C::OwnerDomain::ColorContinuity);
    C::ObservationSet set;set.header=NrContextTests::Header(C::ContractId::C01,C::OwnerDomain::Acquisition,701);
    set.boundary=request.boundary;
    C::BoundedList<C::ContractRef<C::ContractId::C01>,64> refs;
    const auto& first=cross.candidates.front();refs.Push({Sym(C::AcquisitionCandidate::WireName),first.header.record,first.header.revision});
    set.candidates=NrContextTests::PublishList(cross.store,refs);
    C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);
    cross.context.seed.color=Known(cross.store.Publish(color,C::OwnerDomain::Provider));
    cross.context.seed.provider=Key("streamline.provider",2);cross.context.lineageRoot=Key("streamline.lineage",2);
    cross.projection.view=Known(cross.store.Publish(NrContextTests::Resource(10,raster),C::OwnerDomain::Resource));
    {F::ObservationScope registration(cross);F::Callback cb({"Streamline",C::GraphicsApi::D3D12,"tag"},nullptr);
     cb.Resource("Color",C::SemanticKind::Color,&color);}
    const auto& second=cross.candidates.back();refs.Push({Sym(C::AcquisitionCandidate::WireName),second.header.record,second.header.revision});
    set.candidates=NrContextTests::PublishList(cross.store,refs);
    X::SemanticProfile profile;profile.key={Sym("feed.test"),Sym("same-frame"),1};profile.purpose=Sym("same-frame");
    profile.rule=Sym("CTX.Semantics");profile.publication=Key("feed.test.profile",1);
    profile.maximumAge=Known(uint64_t{0});profile.boundary=C::BoundaryKind::BeforeUpscale;
    profile.acceptedEvidence.Push(C::SourceClass::Native);
    profile.sourceSchemas.Push({Sym("NGX"),{1,0}});profile.sourceSchemas.Push({Sym("Streamline"),{1,0}});
    profile.requirements.Push({Sym("jitter"),true});profile.requirements.Push({Sym("color.domain"),true});
    const auto coherent=[&]{auto result=X::BuildContextForPurpose(set,cross.candidates,request,profile,cross.store);
        return result.context&&result.context->Value().jitter.IsKnown()&&result.context->Value().color.IsKnown();};
    r.Check("T21.raw-publication-not-qualified",!coherent(),true,"BuildContextForPurpose");
    // Test owner supplies separate semantic/freshness evidence. FEED cannot invent qualification.
    // The generic observation context retains age-independent facts; this purpose requires same-frame inputs.
    for(auto& candidate:cross.candidates)
    {
        auto evidence=*cross.store.Resolve(candidate.evidence).value;
        evidence.ageInRealFrames=Known(uint64_t{0});evidence.coverage=Known(C::CoverageKind::Complete);
        evidence.semanticCertainty=Known(C::SemanticCertainty::Qualified);
        candidate.evidence=cross.store.Publish(evidence,C::OwnerDomain::Provider);
        candidate.sourceVersion=Known(C::VersionNumber{1,0});
    }
    L::LeaseBinding binding;binding.description.header=NrContextTests::Header(C::ContractId::C03,C::OwnerDomain::Resource,702);
    binding.description.context={Sym(C::CanonicalFrameContext::WireName),request.header.record,1};binding.description.candidate=*refs.Get(0);
    binding.description.resource=NrContextTests::Resource(10,raster).identity;
    binding.description.use=C::UsageKind::CallbackRead;binding.description.commandScope.key=Key("callback",1);
    binding.description.device=Known(Id<C::ObjectIncarnation>("device",1));binding.description.revoked=Known(false);
    binding.description.revocationEpoch=Known(Key("revocation",1));binding.description.expirationBoundary=Known(Key("callback-end",1));
    binding.description.submissionRecheckRequired=Known(true);binding.description.generations=cross.context.generations;
    binding.consumer=Key("consumer",1);binding.purpose=Sym("NativeTemporal");binding.dependencyRequired=false;binding.callbackScoped=true;
    L::OwnerFacts owner;owner.current=binding;owner.permission=L::Fact::Yes;owner.callbackActive=L::Fact::Yes;
    owner.apiCompatible=L::Fact::Yes;owner.registryHealthy=L::Fact::Yes;owner.nativeBindingMatches=L::Fact::Yes;
    owner.externalRegistrationsComplete=L::Fact::Yes;
    r.Check("T21.coherent-valid-right",coherent()&&L::BeginConsumption(binding,owner).has_value(),true,"BuildCanonicalFrameContext/BeginConsumption");
    owner.permission=L::Fact::Unknown;
    r.Check("T21.coherent-missing-right",coherent()&&!L::BeginConsumption(binding,owner),true,"BuildCanonicalFrameContext/BeginConsumption");
    owner.permission=L::Fact::Yes;auto wrong=NrContextTests::Frame();wrong.viewId=Known(Id<C::ViewId>("view",2));
    cross.candidates.front().frame=cross.store.Publish(wrong,C::OwnerDomain::IdentityRegistry);
    r.Check("T21.incoherent-valid-right",!coherent()&&L::BeginConsumption(binding,owner).has_value(),true,"BuildCanonicalFrameContext/BeginConsumption");
    owner.permission=L::Fact::Unknown;
    r.Check("T21.incoherent-missing-right",!coherent()&&!L::BeginConsumption(binding,owner),true,"BuildCanonicalFrameContext/BeginConsumption");
    owner.permission=L::Fact::Yes;owner.callbackActive=L::Fact::No;
    r.Check("T21.callback-expired",!L::BeginConsumption(binding,owner),true,"BeginConsumption");
    cross.candidates.front().frame=cross.context.seed.frame;
    wrong=NrContextTests::Frame();wrong.episodeId=Known(Id<C::EpisodeId>("episode",2));
    cross.candidates.back().frame=cross.store.Publish(wrong,C::OwnerDomain::IdentityRegistry);
    r.Check("T21.wrong-episode",!coherent(),true,"BuildCanonicalFrameContext");
    wrong=NrContextTests::Frame();wrong.baseRealFrameId=Known(Id<C::BaseRealFrameId>("stream.main",11));
    cross.candidates.back().frame=cross.store.Publish(wrong,C::OwnerDomain::IdentityRegistry);
    r.Check("T21.wrong-base-frame",!coherent(),true,"BuildCanonicalFrameContext");
    cross.candidates.back().frame=cross.store.Publish(C::FrameIdentity{},C::OwnerDomain::IdentityRegistry);
    r.Check("T21.unknown-association",!coherent(),true,"BuildCanonicalFrameContext");
    cross.candidates.back().frame=cross.context.seed.frame;
    r.Check("T21.other-source-survives",coherent(),true,"BuildCanonicalFrameContext");
#ifdef NR_FEED_FAMILIES
    struct Parameters
    {
        bool present=true;
        int Get(const char* key,float* out){if(present&&std::string_view(key)=="Jitter.Offset.X"){*out=2.5f;return 1;}return 0;}
        int Get(const char*,unsigned*){return 0;}
        int Get(const char*,void**){return 0;}
    } params;
    FixturePort family;
    const auto field=[&](std::string_view name)->const C::SemanticClaim*{
        for(std::size_t i=family.candidates.size();i>0;--i)if(family.Claim(i-1).field.View()==name)return &family.Claim(i-1);return nullptr;};
    F::ObservationScope familyScope(family);
    {F::Callback cb(ngx,nullptr);F::ObserveNgxEvaluation(cb,&params,1);}
    r.Check("T20.ngx-get-presence",field("Jitter.Offset.X")&&field("Jitter.Offset.X")->raw==Known(C::ScalarValue{2.5}),true,"ObserveNgxEvaluation");
    r.Check("T20.ngx-missing-flags",field("DLSS.Feature.Create.Flags")&&!field("DLSS.Feature.Create.Flags")->raw.IsKnown(),true,"ObserveNgxEvaluation");
    r.Check("T20.ngx-incomplete-jitter-pair",!family.candidates.back().jitter.IsKnown(),true,"ObserveNgxEvaluation");
    params.present=false;{F::Callback cb(ngx,nullptr);F::ObserveNgxEvaluation(cb,&params,1);}
    r.Check("T20.ngx-vanished",field("Jitter.Offset.X")&&!field("Jitter.Offset.X")->raw.IsKnown(),true,"ObserveNgxEvaluation");
    struct FsrFixture {struct V{float x,y;};V jitterOffset{4,-2},motionVectorScale{2,3};float preExposure=2;bool reset=false;} fsr;
    {F::Callback cb({"FSR2",C::GraphicsApi::D3D12,"dispatch"},nullptr);F::ObserveFsrDispatch(cb,&fsr);}
    r.Check("T20.fsr-native-scale",field("motionVectorScale.x")&&field("motionVectorScale.x")->raw==Known(C::ScalarValue{2.0}),true,"ObserveFsrDispatch");
    struct XessFixture {float jitterOffsetX=3,jitterOffsetY=-2,exposureScale=1;unsigned inputWidth=800,inputHeight=400,resetHistory=0;} xess;
    {F::Callback cb({"XeSS",C::GraphicsApi::D3D12,"execute"},nullptr);F::ObserveXessExecute(cb,&xess);}
    r.Check("T20.xess-raw-jitter",field("jitterOffsetX")&&field("jitterOffsetX")->raw==Known(C::ScalarValue{3.0}),true,"ObserveXessExecute");
#ifdef NR_FEED_XESS_TRANSFORM
    {F::Callback cb({"XeSS",C::GraphicsApi::D3D12,"execute"},nullptr);F::ObserveXessJitterTransform(cb,&xess,2.0f,3.0f);}
    r.Check("T20.xess-transform",field("jitterOffsetX")&&field("jitterOffsetX")->raw==Known(C::ScalarValue{3.0})&&
        field("jitterOffsetX")->effective==Known(C::ScalarValue{6.0}),true,"ObserveXessJitterTransform");
    r.Check("T20.xess-carried-scale-not-current-raw",field("legacy.jitterScaleX")&&!field("legacy.jitterScaleX")->raw.IsKnown()&&
        field("legacy.jitterScaleX")->effective==Known(C::ScalarValue{2.0}),true,"ObserveXessJitterTransform");
    r.Check("T20.xess-formula-not-empty",field("transform.jitter.formula")&&
        field("transform.jitter.formula")->raw==Known(C::ScalarValue{Sym("XeSS.jitterOffset.Multiply.context.jitterScale")}),true,"ObserveXessJitterTransform");
#else
    r.Check("T20.xess-transform",false,true,"ObserveXessJitterTransform");
#endif
    struct SlFixture {unsigned structVersion=2;struct V{float x,y;};V jitterOffset{2,1},mvecScale{1,1};unsigned reset=0;} sl;
    {F::Callback cb({"Streamline",C::GraphicsApi::D3D12,"constants"},nullptr);F::ObserveStreamlineConstants(cb,sl);}
    r.Check("T20.streamline-before-transform",field("jitterOffset.x")&&field("jitterOffset.x")->raw==Known(C::ScalarValue{2.0}),true,"ObserveStreamlineConstants");
    sl.jitterOffset.x=(std::numeric_limits<float>::max)();sl.reset=2;
    {F::Callback cb({"Streamline",C::GraphicsApi::D3D12,"constants"},nullptr);F::ObserveStreamlineConstants(cb,sl);}
    r.Check("T20.streamline-invalid-float",field("jitterOffset.x")&&!field("jitterOffset.x")->raw.IsKnown(),true,"ObserveStreamlineConstants");
    r.Check("T20.streamline-invalid-boolean",field("reset")&&!field("reset")->raw.IsKnown(),true,"ObserveStreamlineConstants");
    r.Check("T20.streamline-invalid-jitter-pair",!family.candidates.back().jitter.IsKnown(),true,"ObserveStreamlineConstants");
    struct GuideFixture {float MvScaleX=1,MvScaleY=1,JitterX=0,JitterY=0,PreExposure=1;unsigned RenderSubrectWidth=800,RenderSubrectHeight=400;} guide;
    {F::Callback cb({"PresentGuides",C::GraphicsApi::D3D12,"capture"},nullptr);F::ObservePresentGuide(cb,guide,nullptr,nullptr);}
    r.Check("T20.present-default-not-raw",field("legacy.MvScaleX")&&!field("legacy.MvScaleX")->raw.IsKnown()&&field("legacy.MvScaleX")->effective==Known(C::ScalarValue{1.0}),true,"ObservePresentGuide");
#else
    r.Check("T20.family-adapters-present",false,true,"ProviderAdapters");
#endif
#else
    r.Check("T20.production-adapter-present",false,true,"ObservationPublisher");
#endif
    return r.Finish();});
}
