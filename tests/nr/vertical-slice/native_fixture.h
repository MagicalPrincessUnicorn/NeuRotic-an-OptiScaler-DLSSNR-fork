#pragma once
#include "../representations/RepresentationFixture.h"
#include <nr/protocol/NrExecutionRecipe.h>
namespace NativeTests
{
using namespace NrTest002;
namespace P=Neurotic::Protocol;
struct Fixture:NrRepresentationTests::Fixture
{
    P::RecipeRequest request;
    std::unique_ptr<P::RecipeProduct> product;
    explicit Fixture(bool buildRecipe=true,bool nativeBefore=false)
    {
        if(nativeBefore)request.placement=C::Placement::NativeBefore;
        request.runtime.source={C::OwnerDomain::Provider,Sym("NR.BackendContract"),{1,0},Key("backend"),1};
        request.runtime.family=Sym("AlphaFeature18");request.runtime.abiVersion=1;request.runtime.featureKind=18;
        request.runtime.incarnation=Known(Id<C::ProviderIncarnation>("provider",1));request.runtime.available=Known(true);
        request.requestedProfile=*P::BuildProfileKey(*P::LookupProfile(P::Family::LegacyCompatibleNativeDlssNr,1),
            request.runtime,request.api,request.placement,request.strategy);
        request.recipeHeader=NrContextTests::Header(C::ContractId::C06,C::OwnerDomain::RenderingProtocol,70);
        request.profileHeader=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Strategy,71);
        request.evaluation=Id<C::EvaluationId>("evaluation",1);
        frame.evaluationId=Known(request.evaluation);
        request.externalPlan={Sym(C::PlanCommit::WireName),Key("plan"),1};
        C::CanonicalFrameContext context;context.header=NrContextTests::Header(C::ContractId::C02,C::OwnerDomain::Context,72);
        context.frame=store.Publish(frame);context.renderRaster=store.Publish(source.raster);context.outputRaster=context.renderRaster;
        context.generations=store.Publish(C::GenerationVector{});context.masks=store.Publish(C::MaskSet{});
        context.continuity=store.Publish(C::EvidenceVector{});
        C::ColorDescription color;color.domain=Known(C::ColorDomain::SceneLinear);context.color=Known(store.Publish(color));
        C::DepthDescription depth;depth.kind=Known(C::DepthKind::Device);depth.reversed=Known(false);context.depth=Known(store.Publish(depth));
        auto motion=Analytic::MotionInput().motion;motion.framePair.current=frame.baseRealFrameId;context.motion=Known(store.Publish(motion));
        request.currentContext={Sym(C::CanonicalFrameContext::WireName),context.header.record,1};
        C::QualificationCertificate semantic;semantic.header=NrContextTests::Header(C::ContractId::C04,C::OwnerDomain::Context,73);
        semantic.context=request.currentContext;semantic.profile=request.requestedProfile;semantic.purpose=Sym("RENDER.NativeSemantics");
        semantic.eligibility=C::Eligibility::Eligible;semantic.maximumAge=Known(std::uint64_t{0});
        C::StructuralSignature signature;signature.schema=Sym("CTX.Semantics");signature.version=1;signature.generations=context.generations;
        semantic.signature=store.Publish(signature);request.semantic=store.Publish(semantic);
        P::SettingsSnapshot settings;settings.source={C::OwnerDomain::Configuration,Sym("NR.RenderSettings"),{1,0},Key("settings"),1};
        for(const auto key:{"DLSSNR.Hint.Render.Preset","DLSSNR.Style","DLSSNR.UseAutoMask","DLSSNR.Intensity",
            "DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"})
        {
            C::SemanticClaim control;control.field=Sym(key);
            const bool integer=control.field==Sym("DLSSNR.Hint.Render.Preset")||control.field==Sym("DLSSNR.Style")||control.field==Sym("DLSSNR.UseAutoMask");
            control.effective=Known(integer?C::ScalarValue{std::uint64_t{1}}:C::ScalarValue{0.5});settings.controls.Push(control);
        }
        request.settings=store.Publish(settings,C::OwnerDomain::Configuration);
        C::HistoryKey history;history.owner=Key("history");history.slot=Sym("main");history.dependencies=store.Publish(signature);
        request.history=store.Publish(history,C::OwnerDomain::History);
        for(const auto purpose:{P::Purpose::ColorModelInput,P::Purpose::OutputModelTarget,P::Purpose::DepthGuide,P::Purpose::MotionVectorGuide})
        {
            auto plan=Plan();plan.profile=request.requestedProfile;plan.purpose=Sym(C::EnumName(purpose));
            plan.header.record.value+=static_cast<std::uint64_t>(purpose);plan.keys.plan.value+=static_cast<std::uint64_t>(purpose);
            P::RepresentationUse use;use.source={C::OwnerDomain::Context,Sym("C12.ConsumerUse"),{1,0},plan.header.record,1};
            use.context=request.currentContext;use.purpose=purpose;
            use.kind=Sym(purpose==P::Purpose::ColorModelInput?"ModelProxy":purpose==P::Purpose::OutputModelTarget?"ModelTarget":"RawGuideOriginal");
            use.consumerContract=Sym("LegacyNative.Binding.v1");use.plan=store.Publish(plan);
            use.currentSource=store.Publish(source,C::OwnerDomain::Resource);use.currentContent=Known(C::ContentRevision{1});
            request.representations.Push(use);
        }
        request.context=store.Publish(context);if(buildRecipe)Rebuild();
    }
    // Explicitly synthetic source publication for the Spectre CPU tests. The
    // production source owner must supply its own feature/ingress and ordinal.
    void AsNativeSample(std::uint64_t ordinal,std::uint64_t feature=1,std::uint64_t ingress=1)
    {
        C::NativeSampleIdentityV1 sample;sample.session=frame.sessionId.KnownPart()->value;
        sample.stream=frame.renderStreamId.KnownPart()->value;sample.view=frame.viewId.KnownPart()->value;
        sample.feature=Id<C::ObjectIncarnation>("feature",feature);sample.ingress=Id<C::ObjectIncarnation>("ingress",ingress);
        sample.producerOrdinal=ordinal;sample.callback=Key("callback",ordinal);
        frame.nativeSample=store.Publish(sample,C::OwnerDomain::Provider);
        auto context=*Neurotic::Context::ResolveMetadata(request.context,store);
        context.frame=store.Publish(frame,C::OwnerDomain::Provider);
        auto motion=*Neurotic::Context::ResolveOptional(context.motion,store);
        motion.framePair={};motion.nativeSample=frame.nativeSample;context.motion=Known(store.Publish(motion));
        request.context=store.Publish(context);
        auto semantic=*Neurotic::Context::ResolveMetadata(request.semantic,store);semantic.maximumAge={};
        semantic.nativeFreshness=Neurotic::Context::NativeFreshness(*frame.nativeSample,sample);
        request.semantic=store.Publish(semantic);
        for(auto& use:request.representations)use.nativeSample=frame.nativeSample;
    }
    void Rebuild()
    {
        const auto built=P::BuildRecipe(request,store);
        if(!built.product)throw std::runtime_error(std::string(built.reason.View()));
        product=std::make_unique<P::RecipeProduct>(built.product->Value());
    }
};
}
