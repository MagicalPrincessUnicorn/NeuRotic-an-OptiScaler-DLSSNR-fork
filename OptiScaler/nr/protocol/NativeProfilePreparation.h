#pragma once
#include "ProfileQualification.h"
#include "BindingPlan.h"
#include "NativeEncodeControls.h"
#include <nr/context/ContextBuilder.h>
#include <memory>

namespace Neurotic::Protocol
{
// Nonexecuting profile-owner projection. No C06 exists here, and the binding
// recipe key remains unset until the executable builder freezes a real C06.
struct NativeProfilePreparationProjection
{
    C::QualificationCertificate profileCertificate;
    std::optional<C::ContractRef<C::ContractId::C04>> semanticCertificate;
    C::MetadataRef<C::CanonicalFrameContext> inputContext;
    std::optional<C::MetadataRef<C::QualificationCertificate>> inputSemantic;
    C::MetadataRef<SettingsSnapshot> inputSettings;
    C::BoundedList<RepresentationUse,8> inputRepresentations;
    std::optional<RuntimeContract> runtime;
    BindingPlan bindingPlan;
    DependencyDeclaration dependencies;
    C::Symbol disposition,bypassReason;
    C::ResourceDescriptor expectedOutput;
    C::MetadataList<C::ContractRef<C::ContractId::C12>,32> representations;
    C::MetadataList<C::AccessRequirements,16> leaseRequests;
    C::MetadataList<C::TransformStep,16> conversions;
};
struct NativeProfilePreparationResult
{
    std::optional<C::ImmutableRecord<NativeProfilePreparationProjection>> projection;
    C::Symbol reason;
};
inline bool SettingsBindings(const SettingsSnapshot& settings,BindingPlan& output)
{
    constexpr std::array<std::string_view,7> keys={"DLSSNR.Hint.Render.Preset","DLSSNR.Style","DLSSNR.UseAutoMask",
        "DLSSNR.Intensity","DLSSNR.LocalStructureStrength","DLSSNR.LocalToneStrength","DLSSNR.SkinStructureStrength"};
    if(settings.controls.Size()!=keys.size())return false;
    for(std::size_t i=0;i<keys.size();++i)
    {
        const C::SemanticClaim* control=nullptr;
        for(const auto& value:settings.controls)if(value.field.View()==keys[i]){if(control)return false;control=&value;}
        if(!control||!S::Established(control->effective))return false;
        const auto& effective=control->effective.KnownPart()->value;
        if(i<3?!std::holds_alternative<std::uint64_t>(effective):!std::holds_alternative<double>(effective))return false;
        Binding binding;binding.purpose=i==2?Purpose::AutoSkinMaskEnable:Purpose::ColorModelInput;
        binding.key=control->field;binding.mode=Symbol("Scalar");binding.stage=Symbol(i==0?"Create":"CreateAndEvaluate");
        binding.raw=control->raw;binding.effective=control->effective;
        binding.effectiveSource=Symbol(control->raw.IsKnown()?"ExplicitConfig":"LegacyDefault");binding.rule=Symbol("LegacyNative.Settings.v1");
        if(!AddBinding(output,binding))return false;
    }
    return true;
}
template<class Reader> bool ApplyConsumerControls(const RepresentationUse& use,const Reader& reader,BindingPlan& bindings)
{
    const S::TransformLineage *applied=nullptr,*deferred=nullptr;
    if(!S::ResolveList(use.applied,reader,applied)||!S::ResolveList(use.consumerDeferred,reader,deferred))return false;
    const auto neutral=[&](std::string_view key,double value){
        for(auto& binding:bindings.bindings)if(binding.key.View()==key)
        {
            binding.effective=C::OptionalFact<C::ScalarValue>::FromKnown(C::ScalarValue{value},C::EvidenceRef{use.source.record});
            binding.effectiveSource=Symbol("C12.AlreadyApplied");binding.rule=Symbol("LegacyNative.Lineage.v1");return true;
        }return false;};
    if(applied)for(const auto& step:*applied)
    {
        if(step.kind==C::TransformKind::MotionScale&&use.purpose==Purpose::MotionVectorGuide)
        {if(!neutral("DLSSNR.MVecScaleX",1)||!neutral("DLSSNR.MVecScaleY",1))return false;}
        if(step.kind==C::TransformKind::JitterRemoval&&use.purpose==Purpose::MotionVectorGuide)
        {if(!neutral("Jitter.Offset.X",0)||!neutral("Jitter.Offset.Y",0))return false;}
        if(step.kind==C::TransformKind::ExposureUndo&&use.purpose==Purpose::ColorModelInput)
            if(!neutral("DLSS.Pre.Exposure",1))return false;
    }
    if(deferred)for(const auto& step:*deferred)
    {
        if(step.operation.View()=="LegacyNative.Encode.v1")
        {if(!AddNativeEncodeBindings(use,reader,bindings))return false;continue;}
        // Only inspected, versioned consumer operations are executable here.
        if(step.operation.View()!="LegacyNative.Binding.v1"||step.version!=1)return false;
        const C::BoundedList<C::SemanticClaim,8>* parameters=nullptr;
        if(!S::ResolveList(step.parameters,reader,parameters)||!parameters)return false;
        const bool motion=step.kind==C::TransformKind::MotionScale&&use.purpose==Purpose::MotionVectorGuide;
        const bool jitter=step.kind==C::TransformKind::JitterRemoval&&use.purpose==Purpose::MotionVectorGuide;
        const bool exposure=step.kind==C::TransformKind::ExposureUndo&&use.purpose==Purpose::ColorModelInput;
        if((!motion&&!jitter&&!exposure)||parameters->Size()!=(exposure?1u:2u))return false;
        for(const auto& parameter:*parameters)
        {
            const auto key=parameter.field.View();
            if(!(motion&&(key=="DLSSNR.MVecScaleX"||key=="DLSSNR.MVecScaleY"))&&
               !(jitter&&(key=="Jitter.Offset.X"||key=="Jitter.Offset.Y"))&&
               !(exposure&&key=="DLSS.Pre.Exposure"))return false;
            if(!Number(parameter.effective))return false;
            bool found=false;
            for(auto& binding:bindings.bindings)if(binding.key==parameter.field)
            {binding.effective=parameter.effective;binding.effectiveSource=Symbol("C12.ConsumerTransform");binding.rule=step.operation;found=true;}
            if(!found)return false;
        }
    }
    return true;
}
namespace Detail
{
template<class Store> NativeProfilePreparationResult BuildProfileProjection(
    const NativeProfilePreparationRequest& input,Store& store,const C::EvidenceRef& evidence,
    ProfilePreparationContent content=ProfilePreparationContent::ExactCurrent)
{
    NativeProfilePreparationResult result;result.reason=QualifyPreparation(input,store,content);if(!result.reason.Empty())return result;
    try
    {
        auto product=std::make_unique<NativeProfilePreparationProjection>();auto& p=*product;
        const auto* context=S::ResolveMetadata(input.context,store);const auto* settings=S::ResolveMetadata(input.settings,store);
        if(!context||!settings)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
        p.inputContext=input.context;p.inputSettings=input.settings;p.inputRepresentations=input.representations;
        p.dependencies.profile=input.requestedProfile;p.dependencies.strategy=input.strategy;p.dependencies.placement=input.placement;
        p.dependencies.provider=input.runtime.incarnation;p.dependencies.settings=settings->source;p.dependencies.history=input.history;
        p.bindingPlan.evaluation=input.evaluation;
        const bool native=input.family==Family::LegacyCompatibleNativeDlssNr;
        p.bindingPlan.modelEvaluation=native;p.disposition=Symbol(native?"Planned":"Bypassed");p.bypassReason=native?Symbol("NotBypassed"):input.bypassReason;
        auto& certificate=p.profileCertificate;certificate.header=input.profileHeader;certificate.context=input.currentContext;
        certificate.profile=input.requestedProfile;
        certificate.purpose=Symbol(!native?"RENDER.NoOpBypass":content==ProfilePreparationContent::StructuralOnly?
            "RENDER.NativeStructuralBindings":"RENDER.NativeBindings");
        certificate.eligibility=C::Eligibility::Eligible;
        const auto* frame=S::ResolveMetadata(context->frame,store);if(!frame)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
        if(frame->nativeSample)
        {
            const auto* sample=S::ResolveNativeSample(*frame,store);if(!sample)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
            certificate.nativeFreshness=S::NativeFreshness(*frame->nativeSample,*sample);
        }
        else certificate.maximumAge=C::OptionalFact<std::uint64_t>::FromKnown(0,evidence);
        C::BoundedList<C::ContractRef<C::ContractId::C12>,32> plans;C::BoundedList<C::AccessRequirements,16> access;
        S::TransformLineage conversions;
        if(native)
        {
            p.runtime=input.runtime;
            const auto* semantic=S::ResolveMetadata(input.semantic,store);if(!semantic)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
            p.semanticCertificate=Reference<C::ContractId::C04>(*semantic);p.inputSemantic=input.semantic;
            if(!BuildFrameControls(*context,evidence,store,p.bindingPlan)||!SettingsBindings(*settings,p.bindingPlan)||
               !ClearOptionalResources(evidence,p.bindingPlan))return {{},Symbol("RENDER.Profile.ControlBindingInvalid")};
            const auto* depth=S::ResolveOptional(context->depth,store);
            if(!depth||!Scalar(p.bindingPlan,Purpose::DepthGuide,"DLSSNR.DepthInverted",C::ScalarValue{std::uint64_t(depth->reversed.KnownPart()->value)},evidence)||
               !Scalar(p.bindingPlan,Purpose::ColorModelInput,"DLSSNR.Enabled",C::ScalarValue{std::uint64_t{1}},evidence))
                return {{},Symbol("RENDER.Profile.ControlBindingInvalid")};
            for(std::size_t ordinal=0;ordinal<5;++ordinal)
            {
                const auto purpose=ordinal==4?Purpose::ExposureResource:static_cast<Purpose>(ordinal);
                const RepresentationUse* use=nullptr;for(const auto& v:input.representations)if(v.purpose==purpose)use=&v;
                const char* key=ordinal==0?"DLSSNR.Color":ordinal==1?"DLSSNR.Output":ordinal==2?"DLSSNR.Depth":ordinal==3?"DLSSNR.MVec":"ExposureTexture";
                Binding resource;resource.purpose=purpose;resource.key=Symbol(key);resource.mode=Symbol(use?"Representation":"ExplicitNull");
                resource.effectiveSource=Symbol(use?"C12.Current":"LegacyAbsent");resource.rule=Symbol("LegacyNative.Binding.v1");
                if(ordinal==4)resource.stage=Symbol("Frame");
                if(use)
                {
                    const auto* plan=S::ResolveMetadata(use->plan,store);if(!plan)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
                    const auto* mapping=S::ResolveMetadata(plan->raster,store);if(!mapping)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
                    resource.representation=Reference<C::ContractId::C12>(*plan);
                    if(!plans.Push(*resource.representation)||!access.Push(plan->accessRequirements)||
                       !p.dependencies.structuralPlans.Push(plan->keys.plan)||!ApplyConsumerControls(*use,store,p.bindingPlan))
                        return {{},Symbol("RENDER.Profile.TransformBindingInvalid")};
                    const S::TransformLineage* deferred=nullptr;if(!S::ResolveList(use->consumerDeferred,store,deferred))return {{},Symbol("RENDER.Profile.SourceDisappeared")};
                    if(deferred)for(const auto& step:*deferred)if(!conversions.Push(step))return {{},Symbol("RENDER.Profile.CapacityExceeded")};
                    if(ordinal<4)
                    {
                        const auto rect=mapping->target.active.KnownPart()->value;
                        if(ordinal<2&&(rect.x!=0||rect.y!=0))return {{},Symbol("RENDER.Profile.ModelOriginUnsupported")};
                        const std::array<std::uint64_t,4> values={rect.x,rect.y,rect.width,rect.height};
                        const std::array<std::string_view,4> suffix={"SubrectBaseX","SubrectBaseY","SubrectWidth","SubrectHeight"};
                        for(std::size_t j=0;j<4;++j)if(!Scalar(p.bindingPlan,purpose,std::string(key)+std::string(suffix[j]),C::ScalarValue{values[j]},evidence))
                            return {{},Symbol("RENDER.Profile.CapacityExceeded")};
                        if(ordinal==1)
                        {
                            p.expectedOutput=plan->output;
                            if(!Scalar(p.bindingPlan,purpose,"DLSSNR.Width",C::ScalarValue{std::uint64_t(rect.width)},evidence)||
                               !Scalar(p.bindingPlan,purpose,"DLSSNR.Height",C::ScalarValue{std::uint64_t(rect.height)},evidence))return {{},Symbol("RENDER.Profile.CapacityExceeded")};
                        }
                    }
                }
                if(!AddBinding(p.bindingPlan,resource))return {{},Symbol("RENDER.Profile.CapacityExceeded")};
            }
            for(auto& binding:p.bindingPlan.bindings)
            {
                if(binding.key.View()=="DLSS.Pre.Exposure")binding.stage=Symbol("Frame");
                if(binding.key.View()=="DLSSNR.Enabled"||binding.key.View()=="DLSSNR.Width"||binding.key.View()=="DLSSNR.Height")binding.stage=Symbol("CreateAndEvaluate");
            }
            for(const auto key:{"CreationNodeMask","VisibilityNodeMask","DLSSNR.UICorrection"})
            {
                if(!Scalar(p.bindingPlan,Purpose::ColorModelInput,key,C::ScalarValue{std::uint64_t{1}},evidence))return {{},Symbol("RENDER.Profile.CapacityExceeded")};
                p.bindingPlan.bindings.Get(p.bindingPlan.bindings.Size()-1)->stage=Symbol("Create");
            }
        }
        if(!S::PublishList(plans,store,p.representations)||!S::PublishList(access,store,p.leaseRequests)||
           !S::PublishList(conversions,store,p.conversions))return {{},Symbol("RENDER.Profile.PublicationFailed")};
        C::StructuralSignature signature;signature.schema=Symbol("RENDER.Dependencies");signature.version=1;signature.generations=context->generations;
        C::BoundedList<C::RecordKey,32> dependencies;dependencies.Push(settings->source.record);
        for(const auto& key:p.dependencies.structuralPlans)dependencies.Push(key);
        if(!S::PublishList(dependencies,store,signature.structuralDependencies))return {{},Symbol("RENDER.Profile.PublicationFailed")};
        certificate.signature=store.Publish(signature);
        if(!S::ValidValues(p.profileCertificate)||!S::ValidValues(p.bindingPlan.bindings)||
           !S::ValidValues(p.dependencies)||!S::ValidValues(p.inputRepresentations)||
           !S::ValidValues(p.representations)||!S::ValidValues(p.leaseRequests)||!S::ValidValues(p.conversions)||
           (native&&!S::ValidValues(p.expectedOutput)))return {{},Symbol("RENDER.Profile.InvalidProduct")};
        result.projection.emplace(p);return result;
    }
    catch(const std::bad_alloc&){return {{},Symbol("RENDER.Profile.AllocationFailure")};}
}
}
template<class Store> NativeProfilePreparationResult PrepareNativeProfile(const NativeProfilePreparationRequest& input,Store& store)
{
    if(input.family!=Family::LegacyCompatibleNativeDlssNr)return {{},Symbol("RENDER.Profile.NativeRequired")};
    // This pre-commit projection describes planned bindings only. Fresh executable
    // recipes must requalify exact current content and publish their own C04.
    return Detail::BuildProfileProjection(input,store,C::EvidenceRef{input.profileHeader.record},ProfilePreparationContent::StructuralOnly);
}
}
