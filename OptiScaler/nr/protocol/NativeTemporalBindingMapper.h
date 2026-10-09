#pragma once
#include "LegacyNativeProfileAdapter.h"
#include "ProfileQualification.h"
#include "NativeEncodeControls.h"

namespace Neurotic::Protocol
{
struct NativePlannedResource
{
    Purpose purpose=Purpose::ColorModelInput;C::Symbol key;
    C::ContractRef<C::ContractId::C12> plan;
    C::MetadataRef<C::ResourceView> view;
    std::optional<C::ContractRef<C::ContractId::C01>> sourceCandidate;
    C::ResourceView description;
    // The current readable source is distinct from the requested allocation.
    // Only a Resource-owned PreparedView can attest actual preparation.
    C::ResourceDescriptor plannedOutput;
    bool prepared=false;
};
struct NativeBindingMap
{
    C::BoundedList<LegacyCommand,64> create,evaluate;
    C::BoundedList<NativePlannedResource,5> resources;
    C::RecordKey recipe;C::EvaluationId evaluation;
    C::Placement placement=C::Placement::NativeAfter;
    float preExposure=1;
    bool exposureExplicitNull=false;
    std::optional<NativeEncodeControls> encode;
};
// Authenticated immutable RenderingProtocol publication only. A deserialized product
// is not an ingress capability. This mapper does not redo NFC qualification or C12
// planning and conveys no rights to a native object.
template<class Reader>std::optional<NativeBindingMap> BuildNativeBindingMap(const RecipeProduct& p,const Reader& reader)
{
    if(!S::ValidValues(p)||!p.runtime||!p.bindingPlan.modelEvaluation||p.bindingPlan.historyAdvanceRequested||
       p.disposition.View()!="Planned"||p.recipe.strategy.View()!="NativeTemporal"||
       p.recipe.placement==C::Placement::UnifiedPresent||p.bindingPlan.recipe!=p.recipe.header.record||
       p.bindingPlan.evaluation!=p.recipe.evaluation||p.recipe.header.owner!=C::OwnerDomain::RenderingProtocol)return {};
    const auto expected=BuildProfileKey(*LookupProfile(Family::LegacyCompatibleNativeDlssNr,1),*p.runtime,
        C::GraphicsApi::D3D12,p.recipe.placement,p.recipe.strategy);
    if(!expected||*expected!=p.recipe.profile||p.profileCertificate.profile!=p.recipe.profile||
       p.profileCertificate.context!=p.recipe.context||p.profileCertificate.eligibility!=C::Eligibility::Eligible||
       p.profileCertificate.purpose.View()!="RENDER.NativeBindings")return {};
    const auto* context=S::ResolveMetadata(p.inputContext,reader);
    const auto* settings=S::ResolveMetadata(p.inputSettings,reader);
    const auto* frame=S::ResolveMetadata(p.recipe.frame,reader);
    if(!context||!settings||!frame||context->frame!=p.recipe.frame||Reference<C::ContractId::C02>(*context)!=p.recipe.context||
       settings->source!=p.recipe.settingsSnapshot||
       (frame->nativeSample?(!S::ResolveNativeSample(*frame,reader)||p.recipe.nativeSample!=frame->nativeSample):
        (!S::Established(frame->evaluationId)||frame->evaluationId.KnownPart()->value!=p.recipe.evaluation)))return {};
    if(frame->nativeSample&&p.recipe.placement==C::Placement::NativeAfter&&
       !CurrentSelectedSrOutput(p.inputRepresentations,*context,*frame,reader))return {};
    const auto create=BuildLegacyCommands(p.bindingPlan,LegacyStage::Create);
    const auto evaluate=BuildLegacyCommands(p.bindingPlan,LegacyStage::Evaluate);
    if(!create||!evaluate)return {};
    NativeBindingMap result;result.create=*create;result.evaluate=*evaluate;
    result.recipe=p.recipe.header.record;result.evaluation=p.recipe.evaluation;result.placement=p.recipe.placement;
    const C::BoundedList<C::ContractRef<C::ContractId::C12>,32>* recipePlans=nullptr;
    if(!S::ResolveList(p.recipe.representations,reader,recipePlans)||!recipePlans||recipePlans->Size()!=p.inputRepresentations.Size())return {};
    for(const auto& binding:p.bindingPlan.bindings)
    {
        if(binding.mode.View()!="Representation")continue;
        const RepresentationUse* use=nullptr;
        for(const auto& current:p.inputRepresentations)if(current.purpose==binding.purpose){if(use)return {};use=&current;}
        if(!use||!binding.representation||use->context!=p.recipe.context||use->consumerContract.View()!="LegacyNative.Binding.v1"||
           !AcceptedKind(use->purpose,use->kind)||use->plan.owner!=C::OwnerDomain::Context)return {};
        const auto* plan=S::ResolveMetadata(use->plan,reader);
        const auto* source=S::ResolveMetadata(use->currentSource,reader);
        const bool directNative=frame->nativeSample&&
            (p.recipe.placement==C::Placement::NativeBefore||p.recipe.placement==C::Placement::NativeAfter)&&
            use->sourceCandidate&&
            !S::Established(use->prepared)&&use->purpose!=Purpose::OutputModelTarget;
        const bool targetNative=frame->nativeSample&&
            (p.recipe.placement==C::Placement::NativeBefore||p.recipe.placement==C::Placement::NativeAfter)&&
            use->sourceCandidate&&
            use->purpose==Purpose::OutputModelTarget;
        if(!plan||!source||use->currentSource.owner!=C::OwnerDomain::Resource||
           Reference<C::ContractId::C12>(*plan)!=*binding.representation||plan->profile!=p.recipe.profile||
           (!directNative&&!targetNative&&!S::CompleteContent(*source))||
           (directNative&&!CurrentNativePublication(*use,*context,*frame,reader,p.recipe.placement))||
           (targetNative&&(!CurrentNativePublication(*use,*context,*frame,reader,p.recipe.placement)||
                (p.recipe.placement==C::Placement::NativeBefore&&S::Established(use->currentContent))))||
           !S::SameResourceStructure(source->identity,plan->source)||
           (directNative||targetNative?
             (!S::SemanticEqual(source->identity.contentRevision,plan->source.contentRevision)||
              !S::SemanticEqual(source->identity.contentRevision,use->currentContent)):
             (!S::SameContent(source->identity.contentRevision,plan->source.contentRevision)||
              !S::SameContent(source->identity.contentRevision,use->currentContent)))||
           !LineageMatches(*use,*plan,reader))return {};
        std::size_t matches=0;for(const auto& ref:*recipePlans)if(ref==*binding.representation)++matches;
        if(matches!=1)return {};
        auto view=use->currentSource;
        if(S::Established(use->prepared))
        {const auto* prepared=S::ResolveMetadata(use->prepared.KnownPart()->value,reader);if(!prepared)return {};view=prepared->view;}
        const auto* actual=S::ResolveMetadata(view,reader);
        if(!actual||view.owner!=C::OwnerDomain::Resource||
           (!directNative&&!targetNative&&!S::CompleteContent(*actual))||
           (S::Established(use->prepared)&&!S::SemanticEqual(actual->descriptor,plan->output)))return {};
        if(!result.resources.Push({binding.purpose,binding.key,*binding.representation,view,use->sourceCandidate,*actual,
            plan->output,S::Established(use->prepared)}))return {};
    }
    for(const auto purpose:{Purpose::ColorModelInput,Purpose::OutputModelTarget,Purpose::DepthGuide,Purpose::MotionVectorGuide})
    {std::size_t count=0;for(const auto& resource:result.resources)if(resource.purpose==purpose)++count;if(count!=1)return {};}
    for(const auto key:{"DLSSNR.UI","DLSSNR.UIAlpha","DLSSNR.Backbuffer"})
    {const auto* b=FindBinding(p.bindingPlan,key);if(!b||b->mode.View()!="ExplicitNull"||b->representation)return {};}
    const auto* exposure=FindBinding(p.bindingPlan,"ExposureTexture");
    const auto* scalar=FindBinding(p.bindingPlan,"DLSS.Pre.Exposure");
    const auto* number=scalar?Number(scalar->effective):nullptr;
    if(!exposure||exposure->stage.View()!="Frame"||!number||*number<=0||*number>(std::numeric_limits<float>::max)())return {};
    result.preExposure=static_cast<float>(*number);result.exposureExplicitNull=exposure->mode.View()=="ExplicitNull";
    if(result.exposureExplicitNull?exposure->representation.has_value():exposure->mode.View()!="Representation")return {};
    if(result.resources.Size()!=p.inputRepresentations.Size())return {};
    bool declaresEncode=false,hasEncodeBindings=false;
    for(const auto& use:p.inputRepresentations)
    {
        const S::TransformLineage* deferred=nullptr;if(!S::ResolveList(use.consumerDeferred,reader,deferred))return {};
        if(deferred)for(const auto& step:*deferred)if(step.operation.View()=="LegacyNative.Encode.v1")
        {
            if(declaresEncode)return {};declaresEncode=true;
            const auto declared=NativeEncodeForUse(use,reader);const auto bound=ReadNativeEncodeBindings(p.bindingPlan);
            if(!declared||!bound||*declared!=*bound)return {};result.encode=bound;
        }
    }
    for(const auto& binding:p.bindingPlan.bindings)if(binding.key.View().starts_with("Native.Encode."))hasEncodeBindings=true;
    if(hasEncodeBindings!=declaresEncode)return {};
    return result;
}
}
