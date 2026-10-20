#include "../representations/RepresentationFixture.h"
#if __has_include(<nr/protocol/ProfileQualification.h>)
#include <nr/protocol/ProfileQualification.h>
#define NR_PROTOCOL_QUALIFICATION 1
#endif
#if __has_include(<nr/protocol/NrExecutionRecipe.h>)
#include <nr/protocol/NrExecutionRecipe.h>
#define NR_PROTOCOL_RECIPE 1
#endif
#if __has_include(<nr/protocol/RecipeSerialization.h>)
#include <nr/protocol/RecipeSerialization.h>
#define NR_PROTOCOL_REPLAY 1
#endif
#if __has_include(<nr/protocol/LegacyNativeProfileAdapter.h>)
#include <nr/protocol/LegacyNativeProfileAdapter.h>
#define NR_PROTOCOL_ADAPTER 1
#endif
#if __has_include(<nr/protocol/RecipePublication.h>)
#include <nr/protocol/RecipePublication.h>
#define NR_PROTOCOL_PUBLICATION 1
#endif
#if __has_include(<nr/protocol/ProfileCertificate.h>)
#include <nr/protocol/ProfileCertificate.h>
#define NR_PROTOCOL_CERTIFICATE 1
#endif
using namespace NrTest002;
// Adversarial source-owner harness: live rights/config values are deliberately
// available beside immutable metadata. Production must never read either field.
struct ProtocolStore
{
    NrContextTests::Store& inner;std::uint64_t liveRights=0;double mutableIntensity=0.5;
    template<class T>auto Publish(const T& v,C::OwnerDomain owner=C::OwnerDomain::Context){return inner.Publish(v,owner);}
    template<class T>auto Resolve(const C::MetadataRef<T>& r)const{return inner.Resolve(r);}
};
struct ProtocolFixture:NrRepresentationTests::Fixture
{ProtocolStore store{NrRepresentationTests::Fixture::store};};
int main(int argc,char**argv)
{
 return Guard([&]{Report r("protocol-requirements",argc==2?std::string_view(argv[1]):std::string_view{});
#ifdef NR_PROTOCOL_QUALIFICATION
    namespace P=Neurotic::Protocol;
    const auto profile=P::LookupProfile(P::Family::LegacyCompatibleNativeDlssNr,1);
    ProtocolFixture fixture;
    auto request=std::make_unique<P::RecipeRequest>();
    request->runtime.source={C::OwnerDomain::Provider,Sym("NR.BackendContract"),{1,0},Key("backend"),1};
    request->runtime.family=Sym("AlphaFeature18");request->runtime.abiVersion=1;request->runtime.featureKind=18;
    request->runtime.incarnation=Known(Id<C::ProviderIncarnation>("provider",1));request->runtime.available=Known(true);
    request->requestedProfile=*P::BuildProfileKey(*profile,request->runtime,request->api,request->placement,request->strategy);
    request->recipeHeader=NrContextTests::Header(C::ContractId::C06,C::OwnerDomain::RenderingProtocol,70);
    request->profileHeader=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Strategy,71);
    request->evaluation=Id<C::EvaluationId>("evaluation",1);
    request->externalPlan={Sym("CommittedPlan"),Key("plan"),1};
    C::CanonicalFrameContext context;context.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,72);
    context.frame=fixture.store.Publish(fixture.frame);context.renderRaster=fixture.store.Publish(fixture.source.raster);
    context.outputRaster=context.renderRaster;
    context.generations=fixture.store.Publish(C::GenerationVector{});context.masks=fixture.store.Publish(C::MaskSet{});
    context.continuity=fixture.store.Publish(C::EvidenceVector{});
    C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);context.color=Known(fixture.store.Publish(color));
    C::DepthDescription depth;depth.kind=Known(C::DepthKind::Device);depth.reversed=Known(false);context.depth=Known(fixture.store.Publish(depth));
    auto motion=Analytic::MotionInput().motion;motion.framePair.current=fixture.frame.baseRealFrameId;
    context.motion=Known(fixture.store.Publish(motion));
    request->currentContext={Sym(C::CanonicalFrameContext::WireName),context.header.record,context.header.revision};
    C::QualificationCertificate semantic;semantic.header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,73);
    semantic.context=request->currentContext;semantic.profile=request->requestedProfile;semantic.purpose=Sym("RENDER.NativeSemantics");
    semantic.eligibility=C::Eligibility::Eligible;semantic.maximumAge=Known(std::uint64_t{0});
    C::StructuralSignature signature;signature.schema=Sym("CTX.Semantics");signature.version=1;
    signature.generations=fixture.store.Publish(C::GenerationVector{});semantic.signature=fixture.store.Publish(signature);
    request->semantic=fixture.store.Publish(semantic);
    P::SettingsSnapshot settings;settings.source={C::OwnerDomain::Configuration,Sym("NR.RenderSettings"),{1,0},Key("settings"),1};
    for(const auto key:{"DLSSNR.Hint.Render.Preset","DLSSNR.Style","DLSSNR.UseAutoMask","DLSSNR.Intensity",
        "DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"})
    {
        C::SemanticClaim control;control.field=Sym(key);
        const bool integer=control.field==Sym("DLSSNR.Hint.Render.Preset")||control.field==Sym("DLSSNR.Style")||control.field==Sym("DLSSNR.UseAutoMask");
        control.effective=Known(integer?C::ScalarValue{std::uint64_t{1}}:C::ScalarValue{0.5});settings.controls.Push(control);
    }
    request->settings=fixture.store.Publish(settings,C::OwnerDomain::Configuration);
    C::HistoryKey history;history.owner=Key("history");history.slot=Sym("main");history.dependencies=fixture.store.Publish(signature);
    request->history=fixture.store.Publish(history,C::OwnerDomain::History);
    for(const auto purpose:{P::Purpose::ColorModelInput,P::Purpose::OutputModelTarget,P::Purpose::DepthGuide,P::Purpose::MotionVectorGuide})
    {
        auto plan=fixture.Plan();plan.profile=request->requestedProfile;plan.purpose=Sym(C::EnumName(purpose));
        plan.header.record.value+=static_cast<std::uint64_t>(purpose);plan.keys.plan.value+=static_cast<std::uint64_t>(purpose);
        P::RepresentationUse use;use.source={C::OwnerDomain::Context,Sym("C12.ConsumerUse"),{1,0},plan.header.record,1};
        use.context=request->currentContext;use.purpose=purpose;
        use.kind=Sym(purpose==P::Purpose::ColorModelInput?"ModelProxy":purpose==P::Purpose::OutputModelTarget?"ModelTarget":"RawGuideOriginal");
        use.consumerContract=Sym("LegacyNative.Binding.v1");use.plan=fixture.store.Publish(plan);
        use.currentSource=fixture.store.Publish(fixture.source,C::OwnerDomain::Resource);
        use.currentContent=Known(C::ContentRevision{1});request->representations.Push(use);
    }
    request->context=fixture.store.Publish(context);
    const auto qualify=[&]{return P::Qualify(*request,fixture.store);};
    r.Check("T25.all-required-qualified",qualify().Empty(),true,"Qualify");
#ifdef NR_PROTOCOL_RECIPE
    auto recipe=std::make_unique<P::RecipeBuildResult>(P::BuildRecipe(*request,fixture.store));
    r.Check("T25.recipe-production",recipe->product.has_value(),true,"BuildRecipe");
    if(recipe->product)
    {
        const auto& built=recipe->product->Value();
        r.Check("T25.recipe-preserve-original",built.recipe.failureDisposition==C::FailureDisposition::PreserveOriginal&&built.bindingPlan.modelEvaluation&&!built.bindingPlan.historyAdvanceRequested,true,"BuildRecipe");
        r.Check("T25.recipe-no-ui",P::FindBinding(built.bindingPlan,"DLSSNR.UI")->mode==Sym("ExplicitNull"),true,"BuildRecipe");
        r.Check("T25.recipe-config-snapshot",built.recipe.settingsSnapshot==settings.source,true,"BuildRecipe");
        fixture.store.liveRights=5;const auto rightsChanged=P::BuildRecipe(*request,fixture.store);fixture.store.liveRights=0;
        r.Check("T25.rights-not-profile-requirements",rightsChanged.product&&rightsChanged.product->Value().recipe.profile==built.recipe.profile&&rightsChanged.product->Value().bindingPlan==built.bindingPlan,true,"BuildRecipe");
        fixture.store.mutableIntensity=0.9;const auto mutableChanged=P::BuildRecipe(*request,fixture.store);fixture.store.mutableIntensity=0.5;
        r.Check("T25.mutable-config-not-read",mutableChanged.product&&mutableChanged.product->Value().bindingPlan==built.bindingPlan,true,"BuildRecipe");
        const auto originalContentUses=request->representations;
        for(auto& use:request->representations)
        {
            auto plan=*fixture.store.Resolve(use.plan).value;auto source=*fixture.store.Resolve(use.currentSource).value;
            source.identity.contentRevision=Known(C::ContentRevision{2});plan.source=source.identity;plan.keys.content=source.identity;
            use.plan=fixture.store.Publish(plan);use.currentSource=fixture.store.Publish(source,C::OwnerDomain::Resource);use.currentContent=Known(C::ContentRevision{2});
        }
        const auto newContent=P::BuildRecipe(*request,fixture.store);request->representations=originalContentUses;
        r.Check("T25.content-not-profile-key",newContent.product&&newContent.product->Value().recipe.profile==built.recipe.profile&&newContent.product->Value().dependencies==built.dependencies,true,"BuildRecipe");
#ifdef NR_PROTOCOL_PUBLICATION
        auto receiptHeader=NrContextTests::Header(C::ContractId::C11,C::OwnerDomain::RenderingProtocol,91);
        const auto receipt=P::BuildPlanningReceipt(built,receiptHeader,1,fixture.store);
        r.Check("T25.planned-receipt-causal",receipt&&receipt->Value().operation==Sym("RENDER.RecipePlanned")&&receipt->Value().causalRecords.count>=9,true,"BuildPlanningReceipt");
        receiptHeader.owner=C::OwnerDomain::Diagnostics;
        r.Check("T25.receipt-owner-enforced",!P::BuildPlanningReceipt(built,receiptHeader,1,fixture.store),true,"BuildPlanningReceipt");
#else
        r.Check("T25.planned-receipt-causal",false,true,"BuildPlanningReceipt");
#endif
#ifdef NR_PROTOCOL_REPLAY
        const auto encoded=P::EncodeRecipe(built,fixture.store);
        const auto again=P::EncodeRecipe(built,fixture.store);
        r.Check("T25.complete-replay-bytes",encoded.error==C::Error::None&&encoded.text==again.text,true,"EncodeRecipe");
        const auto decoded=C::Codec::Decode<P::RecipeProduct>(encoded.text);
        r.Check("T25.recipe-roundtrip",decoded.record&&decoded.record->Value()==built,true,"FixtureCodec");
#else
        r.Check("T25.complete-replay-bytes",false,true,"EncodeRecipe");
#endif
#ifdef NR_PROTOCOL_ADAPTER
        const auto commands=P::BuildLegacyCommands(built.bindingPlan,P::LegacyStage::Evaluate);
        r.Check("T25.adapter-commands",commands.has_value(),true,"BuildLegacyCommands");
        bool preset=false,nullUi=false; if(commands)for(const auto& command:*commands)
        {preset|=command.key==Sym("DLSSNR.Hint.Render.Preset");nullUi|=command.key==Sym("DLSSNR.UI")&&command.kind==P::CommandKind::NullResource;}
        r.Check("T25.adapter-exact-evaluate-stage",commands&&!preset&&nullUi,true,"BuildLegacyCommands");
#else
        r.Check("T25.adapter-commands",false,true,"BuildLegacyCommands");
#endif
        auto saved=settings.source;++settings.source.revision;request->settings=fixture.store.Publish(settings,C::OwnerDomain::Configuration);
        const auto changed=P::BuildRecipe(*request,fixture.store);
        r.Check("T25.config-revision-not-profile-key",changed.product&&changed.product->Value().recipe.profile==built.recipe.profile&&changed.product->Value().dependencies.settings!=built.dependencies.settings,true,"BuildRecipe");
        settings.source=saved;request->settings=fixture.store.Publish(settings,C::OwnerDomain::Configuration);
        auto bypass=std::make_unique<P::RecipeRequest>(*request);bypass->family=P::Family::NoOpBypass;
        bypass->runtime={};bypass->representations={};bypass->requestedPurposes={};bypass->bypassReason=Sym("ExplicitDisabled");
        bypass->requestedProfile=*P::BuildProfileKey(*P::LookupProfile(bypass->family,1),bypass->runtime,bypass->api,bypass->placement,bypass->strategy);
        const auto bypassed=P::BuildRecipe(*bypass,fixture.store);
        r.Check("T25.bypass-without-model-backend",bypassed.product&&!bypassed.product->Value().bindingPlan.modelEvaluation&&bypassed.product->Value().bindingPlan.bindings.Size()==0&&bypassed.product->Value().disposition==Sym("Bypassed"),true,"BuildRecipe");
        const auto originalUses=request->representations;
        auto exposureUse=*originalUses.Get(0);auto exposurePlan=fixture.Plan();exposurePlan.profile=request->requestedProfile;
        exposurePlan.purpose=Sym("ExposureResource");exposureUse.plan=fixture.store.Publish(exposurePlan);
        exposureUse.purpose=P::Purpose::ExposureResource;exposureUse.kind=Sym("ExposureResource");request->representations.Push(exposureUse);
        const auto exposed=P::BuildRecipe(*request,fixture.store);request->representations=originalUses;
        const auto vanished=P::BuildRecipe(*request,fixture.store);
        r.Check("T25.vanished-exposure-cleared",exposed.product&&vanished.product&&P::FindBinding(exposed.product->Value().bindingPlan,"ExposureTexture")->representation&&P::FindBinding(vanished.product->Value().bindingPlan,"ExposureTexture")->mode==Sym("ExplicitNull"),true,"BuildRecipe");
    }
#else
    r.Check("T25.recipe-production",false,true,"BuildRecipe");
#endif
    request->runtime.available={};r.Check("T25.feature-not-availability",qualify()==Sym("RENDER.Profile.RuntimeContractUnavailable"),true,"Qualify");request->runtime.available=Known(true);
    auto savedProfile=request->requestedProfile;request->requestedProfile.version=2;
    r.Check("T25.profile-mismatch",!qualify().Empty(),true,"Qualify");request->requestedProfile=savedProfile;
    request->requestedPurposes.Push(P::Purpose::ReactiveMask);r.Check("T26.no-reactive-alias",!qualify().Empty(),true,"Qualify");request->requestedPurposes={};
    request->representations.Get(0)->currentContent=Known(C::ContentRevision{2});
    r.Check("T25.exact-current-content",!qualify().Empty(),true,"Qualify");request->representations.Get(0)->currentContent=Known(C::ContentRevision{1});
    request->representations.Get(0)->kind=Sym("RawColor");r.Check("T25.wrong-representation",!qualify().Empty(),true,"Qualify");request->representations.Get(0)->kind=Sym("ModelProxy");
    request->representations.Get(0)->consumerContract=Sym("Unknown");r.Check("T14.no-inferred-consumer-transform",!qualify().Empty(),true,"Qualify");request->representations.Get(0)->consumerContract=Sym("LegacyNative.Binding.v1");
    const auto savedUses=request->representations;
    request->representations={};for(std::size_t i=0;i<3;++i)request->representations.Push(*savedUses.Get(i));
    r.Check("T25.feature-exists-binding-missing",qualify()==Sym("RENDER.Profile.RequiredBindingMissing"),true,"Qualify");request->representations=savedUses;
    request->representations.Get(2)->kind=Sym("PreparedConvertedView");
    r.Check("T25.prepared-body-required",!qualify().Empty(),true,"Qualify");request->representations=savedUses;
    auto wrongPlan=*fixture.store.Resolve(savedUses.Get(1)->plan).value;wrongPlan.output.allocation=Known(C::Extent{512,512});
    request->representations.Get(1)->plan=fixture.store.Publish(wrongPlan);
    r.Check("T25.output-mapping-mismatch",!qualify().Empty(),true,"Qualify");request->representations=savedUses;
    ++request->currentContext.revision;r.Check("T25.stale-context",!qualify().Empty(),true,"Qualify");--request->currentContext.revision;
    depth.reversed={};context.depth=Known(fixture.store.Publish(depth));request->context=fixture.store.Publish(context);
    r.Check("T25.unknown-depth-polarity",!qualify().Empty(),true,"Qualify");
    depth.reversed=Known(false);context.depth=Known(fixture.store.Publish(depth));request->context=fixture.store.Publish(context);
    semantic.eligibility=C::Eligibility::Ineligible;request->semantic=fixture.store.Publish(semantic);
    r.Check("T25.nfc-cannot-be-overridden",!qualify().Empty(),true,"Qualify");
#ifdef NR_PROTOCOL_CERTIFICATE
    const auto refused=P::QualifyProfile(*request,fixture.store);
    r.Check("T25.rejection-certificate",refused&&refused->Value().eligibility==C::Eligibility::Ineligible&&refused->Value().reasons.count==1,true,"QualifyProfile");
#else
    r.Check("T25.rejection-certificate",false,true,"QualifyProfile");
#endif
#else
    r.Check("T25.qualification-production",false,true,"Qualify");
#endif
    return r.Finish();});
}
