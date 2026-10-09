#pragma once
#include "RecipeTypes.h"
#include "NativeInvocationCurrentness.h"
#include <nr/context/RepresentationCacheKeys.h>

namespace Neurotic::Protocol
{
namespace S=Context;
template<C::ContractId Id,class T> C::ContractRef<Id> Reference(const T& value)
{return {Symbol(T::WireName),value.header.record,value.header.revision};}

inline bool AcceptedKind(Purpose purpose,const C::Symbol& kind)
{
    if(purpose==Purpose::ColorModelInput)return kind.View()=="ModelProxy"||kind.View()=="ScaledModelProxy";
    if(purpose==Purpose::OutputModelTarget)return kind.View()=="ModelTarget";
    if(purpose==Purpose::DepthGuide||purpose==Purpose::MotionVectorGuide)
        return kind.View()=="RawGuideOriginal"||kind.View()=="SemanticGuideView"||kind.View()=="PreparedConvertedView";
    return purpose==Purpose::ExposureResource&&kind.View()=="ExposureResource";
}
template<class Reader> bool LineageMatches(const RepresentationUse& use,const C::RepresentationPlan& plan,const Reader& reader)
{
    if(S::Established(use.prepared)&&(use.purpose==Purpose::OutputModelTarget||plan.purpose.View()=="OutputModelTarget"))return false;
    const S::TransformLineage *applied=nullptr,*required=nullptr,*deferred=nullptr,*planned=nullptr;
    if(!S::ResolveList(use.applied,reader,applied)||!S::ResolveList(use.required,reader,required)||
       !S::ResolveList(use.consumerDeferred,reader,deferred)||!S::ResolveList(plan.transforms,reader,planned))return false;
    const S::TransformLineage empty;
    const auto delta=S::CompareLineage(applied?*applied:empty,required?*required:empty,reader);
    // Every remaining operation is an exact, explicitly declared consumer operation.
    // Material preparation must already be represented by an owner-published PreparedView.
    if(!delta.compatible||!S::SemanticEqual(delta.delta,deferred?*deferred:empty))return false;
    if(!S::ValidLineage(planned?*planned:empty,false,reader))return false;
    if(!S::Established(use.prepared))
        return S::SemanticEqual(planned?*planned:empty,delta.delta);
    const auto* prepared=S::ResolveMetadata(use.prepared.KnownPart()->value,reader);
    if(!prepared||use.prepared.KnownPart()->value.owner!=C::OwnerDomain::Resource||
       prepared->header.scope!=plan.header.scope||prepared->plan!=Reference<C::ContractId::C12>(plan)||
       !S::SemanticEqual(prepared->keys,plan.keys))return false;
    const S::TransformLineage* actual=nullptr;
    const auto* target=S::ResolveMetadata(prepared->view,reader);
    if(!target||!S::CompleteContent(*target)||!S::SemanticEqual(target->descriptor,plan.output)||
       !S::ResolveList(prepared->appliedTransforms,reader,actual))return false;
    // C12's prepared publication binds current source keys and the exact applied lineage.
    return S::SemanticEqual(actual?*actual:empty,applied?*applied:empty);
}
enum class ProfilePreparationContent { ExactCurrent, StructuralOnly };
template<class Reader> C::Symbol QualifyPreparation(const NativeProfilePreparationRequest& input,const Reader& reader,
    ProfilePreparationContent content=ProfilePreparationContent::ExactCurrent)
{
    const auto reject=[](std::string_view reason){return Symbol(reason);};
    const auto* profile=LookupProfile(input.family,input.profileVersion);
    if(!profile)return reject("RENDER.Profile.Unknown");
    const auto key=BuildProfileKey(*profile,input.runtime,input.api,input.placement,input.strategy);
    if(!key||*key!=input.requestedProfile)return reject("RENDER.Profile.ContractMismatch");
    if(!S::ValidValues(input.profileHeader)||
       input.profileHeader.contract!=C::ContractId::C04||input.profileHeader.owner!=C::OwnerDomain::Strategy||
       !S::ValidValues(input.evaluation))return reject("RENDER.Profile.InvalidIdentity");
    const auto* context=S::ResolveMetadata(input.context,reader);
    if(!context||input.context.owner!=C::OwnerDomain::Context||
       input.currentContext!=Reference<C::ContractId::C02>(*context)||
       context->header.scope!=input.profileHeader.scope)
        return reject("RENDER.Profile.ContextMismatch");
    const auto* frame=S::ResolveMetadata(context->frame,reader);
    if(!frame||!S::SameObservationScope(*frame,*frame,reader))return reject("RENDER.Profile.FrameUnknown");
    const auto* settings=S::ResolveMetadata(input.settings,reader);
    if(!settings||input.settings.owner!=C::OwnerDomain::Configuration||settings->source.owner!=C::OwnerDomain::Configuration||
       settings->source.recordType.View()!="NR.RenderSettings"||settings->source.schemaVersion!=C::VersionNumber{1,0})
        return reject("RENDER.Profile.SettingsMissing");
    if(!profile->modelEvaluation)
        return input.representations.Size()==0&&input.requestedPurposes.Size()==0&&!input.bypassReason.Empty()?C::Symbol{}:
            reject("RENDER.Profile.BypassWorkForbidden");
    if(!S::Established(input.runtime.available)||!input.runtime.available.KnownPart()->value||
       !S::Established(input.runtime.incarnation))return reject("RENDER.Profile.RuntimeContractUnavailable");
    const auto* semantic=S::ResolveMetadata(input.semantic,reader);
    if(!semantic||input.semantic.owner!=C::OwnerDomain::Context||semantic->header.owner!=C::OwnerDomain::Context||
       semantic->header.scope!=context->header.scope||semantic->context!=input.currentContext||
       semantic->profile!=*key||semantic->purpose.View()!="RENDER.NativeSemantics"||
       semantic->eligibility!=C::Eligibility::Eligible)return reject("RENDER.Profile.SemanticCertificateMismatch");
    if(frame->nativeSample)
    {
        C::EvidenceVector freshness;freshness.nativeFreshness=semantic->nativeFreshness;freshness.ageInRealFrames=semantic->maximumAge;
        if(!S::NativeEvidenceFresh(freshness,*frame,reader))return reject("RENDER.Profile.SemanticCertificateMismatch");
    }
    else if(semantic->nativeFreshness||!S::Established(semantic->maximumAge)||semantic->maximumAge.KnownPart()->value!=0)
        return reject("RENDER.Profile.SemanticCertificateMismatch");
    const auto* signature=S::ResolveMetadata(semantic->signature,reader);
    if(!signature||signature->schema.View()!="CTX.Semantics"||signature->version!=1)
        return reject("RENDER.Profile.SemanticCertificateMismatch");
    const auto* color=S::ResolveOptional(context->color,reader);const auto* depth=S::ResolveOptional(context->depth,reader);
    const auto* motion=S::ResolveOptional(context->motion,reader);
    const auto* render=S::ResolveMetadata(context->renderRaster,reader);const auto* output=S::ResolveMetadata(context->outputRaster,reader);
    if(!color||!S::Established(color->domain)||!depth||!S::Established(depth->kind)||!S::Established(depth->reversed)||
       !motion||!S::Established(motion->units)||!S::Established(motion->direction)||!S::Established(motion->jitterConvention)||
       !S::ValidRaster(motion->currentRaster)||!S::ValidRaster(motion->previousRaster)||
       !render||!output||!S::ValidRaster(*render)||!S::ValidRaster(*output))
        return reject("RENDER.Profile.RequiredFieldUnknown");
    if(frame->nativeSample)
    {
        if(!S::NativeMotionMatches(*motion,*frame,reader))
            return reject("RENDER.Profile.NativeMotionUnbound");
    }
    else if(motion->nativeSample||
        Lifecycle::CompareTypedIdentity(motion->framePair.current,frame->baseRealFrameId)!=Lifecycle::IdentityStatus::Ok)
        return reject("RENDER.Profile.RequiredFieldUnknown");
    if(depth->kind.KnownPart()->value!=C::DepthKind::Device||
       motion->direction.KnownPart()->value!=C::MotionDirection::CurrentToPrevious)
        return reject("RENDER.Profile.RequiredFieldIncompatible");
    if(!S::ResolveMetadata(input.history,reader))return reject("RENDER.Profile.HistoryMissing");
    for(const auto purpose:input.requestedPurposes)
    {
        const auto support=Support(*profile,purpose);
        if(C::EnumName(purpose).empty()||support==PurposeSupport::Unsupported||support==PurposeSupport::Unknown||
           support==PurposeSupport::ExplicitNull)return reject("RENDER.Profile.UnsupportedPurpose");
    }
    if(content==ProfilePreparationContent::ExactCurrent&&frame->nativeSample&&input.placement==C::Placement::NativeAfter&&
       !CurrentSelectedSrOutput(input.representations,*context,*frame,reader))
        return reject("Native.PostSrOutputPending");
    std::array<bool,5> found{};
    for(const auto& use:input.representations)
    {
        if(use.nativeSample!=frame->nativeSample)return reject("RENDER.Profile.RepresentationSampleMismatch");
        if(!S::ValidValues(use)||use.source.owner!=C::OwnerDomain::Context||use.source.recordType.View()!="C12.ConsumerUse"||
           use.source.schemaVersion!=C::VersionNumber{1,0}||use.context!=input.currentContext||
           use.plan.owner!=C::OwnerDomain::Context||!AcceptedKind(use.purpose,use.kind)||
           (use.kind.View()=="PreparedConvertedView"&&!S::Established(use.prepared))||
           use.consumerContract.View()!="LegacyNative.Binding.v1")return reject("RENDER.Profile.RepresentationIncompatible");
        const std::size_t index=use.purpose==Purpose::ExposureResource?4:static_cast<std::size_t>(use.purpose);
        if(index>=found.size()||found[index])return reject("RENDER.Profile.DuplicatePurpose");found[index]=true;
        const auto* plan=S::ResolveMetadata(use.plan,reader);const auto* source=S::ResolveMetadata(use.currentSource,reader);
        const bool directNative=content==ProfilePreparationContent::ExactCurrent&&frame->nativeSample&&
            (input.placement==C::Placement::NativeBefore||input.placement==C::Placement::NativeAfter)&&use.sourceCandidate&&
            !S::Established(use.prepared)&&
            use.purpose!=Purpose::OutputModelTarget;
        const bool targetNative=content==ProfilePreparationContent::ExactCurrent&&frame->nativeSample&&
            (input.placement==C::Placement::NativeBefore||input.placement==C::Placement::NativeAfter)&&use.sourceCandidate&&
            use.purpose==Purpose::OutputModelTarget;
        if(!plan||!source||use.currentSource.owner!=C::OwnerDomain::Resource||!S::CompleteResourceStructure(*source)||
           (content==ProfilePreparationContent::ExactCurrent&&!directNative&&!targetNative&&!S::CompleteContent(*source))||
           (S::Established(use.prepared)&&!S::CompleteContent(*source))||
           source->descriptor.api.KnownPart()->value!=input.api||plan->header.scope!=context->header.scope||
           plan->profile!=*key||plan->purpose!=Symbol(C::EnumName(use.purpose))||
           !S::SemanticEqual(plan->source,source->identity)||!S::SemanticEqual(plan->keys.content,source->identity)||
           !S::SemanticEqual(use.currentContent,plan->source.contentRevision)||
           !S::CompleteDescriptor(plan->output)||plan->output.api.KnownPart()->value!=input.api)
            return reject("RENDER.Profile.CurrentRepresentationMissing");
        if(directNative&&!CurrentNativePublication(use,*context,*frame,reader,input.placement))
            return reject("RENDER.Profile.InvocationPublicationStale");
        if(targetNative&&(!CurrentNativePublication(use,*context,*frame,reader,input.placement)||
            (input.placement==C::Placement::NativeBefore&&S::Established(use.currentContent))||S::Established(use.prepared)))
            return reject("RENDER.Profile.TargetNotFuture");
        const auto* mapping=S::ResolveMetadata(plan->raster,reader);
        if(!mapping||!S::ValidMapping(*mapping)||!S::SemanticEqual(mapping->source,source->raster)||
           mapping->target.allocation.KnownPart()->value!=plan->output.allocation.KnownPart()->value||
           !LineageMatches(use,*plan,reader))return reject("RENDER.Profile.TransformLineageMismatch");
    }
    for(std::size_t i=0;i<4;++i)if(!found[i])return reject("RENDER.Profile.RequiredBindingMissing");
    return {};
}
template<class Reader> C::Symbol Qualify(const RecipeRequest& input,const Reader& reader)
{
    if(!S::ValidValues(input.recipeHeader)||input.recipeHeader.contract!=C::ContractId::C06||
       input.recipeHeader.owner!=C::OwnerDomain::RenderingProtocol||!S::ValidValues(input.externalPlan))
        return Symbol("RENDER.Profile.InvalidIdentity");
    const auto reason=QualifyPreparation(input,reader);if(!reason.Empty())return reason;
    if(input.recipeHeader.scope!=input.profileHeader.scope)return Symbol("RENDER.Profile.ContextMismatch");
    const auto* context=S::ResolveMetadata(input.context,reader);
    const auto* frame=context?S::ResolveMetadata(context->frame,reader):nullptr;
    if(!frame||frame->nativeSample.has_value()!=input.nativePlan.has_value())return Symbol("RENDER.Profile.NativePlanMissing");
    if(input.nativePlan)
    {
        const auto* plan=S::ResolveMetadata(*input.nativePlan,reader);
        if(input.nativePlan->owner!=C::OwnerDomain::StreamCoordinator||!plan||
            plan->header.owner!=C::OwnerDomain::StreamCoordinator||plan->header.scope!=input.recipeHeader.scope||
            Reference<C::ContractId::C05>(*plan)!=input.externalPlan||!plan->nativeDelivery)
            return Symbol("RENDER.Profile.NativePlanMismatch");
        const auto* delivery=S::ResolveMetadata(*plan->nativeDelivery,reader);
        if(!delivery||delivery->profile!=input.requestedProfile||delivery->placement!=input.placement)
            return Symbol("RENDER.Profile.NativeDeliveryMismatch");
    }
    return {};
}
}
