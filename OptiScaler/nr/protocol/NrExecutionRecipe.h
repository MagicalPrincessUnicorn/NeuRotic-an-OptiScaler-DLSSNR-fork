#pragma once
#include "NativeProfilePreparation.h"

namespace Neurotic::Protocol
{
template<class Store> RecipeBuildResult BuildRecipe(const RecipeRequest& input,Store& store)
{
    RecipeBuildResult result;result.reason=Qualify(input,store);if(!result.reason.Empty())return result;
    const auto prepared=Detail::BuildProfileProjection(input,store,C::EvidenceRef{input.recipeHeader.record});
    if(!prepared.projection)return {{},prepared.reason};
    try
    {
        const auto& projection=prepared.projection->Value();
        const auto* context=S::ResolveMetadata(input.context,store);
        const auto* settings=S::ResolveMetadata(input.settings,store);
        if(!context||!settings)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
        auto product=std::make_unique<RecipeProduct>();auto& p=*product;auto& recipe=p.recipe;
        recipe.header=input.recipeHeader;recipe.evaluation=input.evaluation;recipe.frame=context->frame;
        recipe.committedPlan=input.externalPlan;recipe.context=input.currentContext;recipe.profile=input.requestedProfile;
        if(input.nativePlan)
        {
            const auto* plan=S::ResolveMetadata(*input.nativePlan,store);
            const auto* frame=S::ResolveMetadata(context->frame,store);
            if(!plan||!frame)return {{},Symbol("RENDER.Profile.SourceDisappeared")};
            recipe.nativeDelivery=plan->nativeDelivery;recipe.nativeSample=frame->nativeSample;
        }
        recipe.strategy=input.strategy;recipe.placement=input.placement;recipe.settingsSnapshot=settings->source;
        recipe.resetProjection=input.resetProjection;recipe.history=input.history;recipe.masks=context->masks;
        recipe.expectedOutput=projection.expectedOutput;recipe.representations=projection.representations;
        recipe.leaseRequests=projection.leaseRequests;recipe.conversions=projection.conversions;
        p.profileCertificate=projection.profileCertificate;p.semanticCertificate=projection.semanticCertificate;
        p.inputContext=projection.inputContext;p.inputSemantic=projection.inputSemantic;p.inputSettings=projection.inputSettings;
        p.inputRepresentations=projection.inputRepresentations;p.runtime=projection.runtime;
        p.bindingPlan=projection.bindingPlan;p.bindingPlan.recipe=recipe.header.record;
        p.dependencies=projection.dependencies;p.disposition=projection.disposition;p.bypassReason=projection.bypassReason;
        if(!S::ValidValues(p))return {{},Symbol("RENDER.Profile.InvalidProduct")};
        result.product.emplace(p);return result;
    }
    catch(const std::bad_alloc&){return {{},Symbol("RENDER.Profile.AllocationFailure")};}
}
}
