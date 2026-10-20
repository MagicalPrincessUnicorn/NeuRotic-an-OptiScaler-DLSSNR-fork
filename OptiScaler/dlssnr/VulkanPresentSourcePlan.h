#pragma once
#include "PresentSourcePolicy.h"
#include "DlssNr_PresentInputDecision.h"
#include "VulkanNrModelLifetime.h"

namespace DlssNr {
// Diagnostic return projection only; ReturnedTuple is not a readiness promise.
enum class VkNrPresentLifetimeRead {NotVisited,ConfigUnavailable,Disabled,WrongRoute,BusyRecording,BusyState,Uninitialized,Inactive,NoDevice,ReturnedTuple};
struct VkNrPresentGuardObservation {
    const char* predicate=nullptr;VkNrPresentLifetimeRead read=VkNrPresentLifetimeRead::NotVisited;
    bool preferenceKnown=false;uint64_t expectedPreference=0,actualPreference=0;
    std::optional<VkNrPresentModelLifetime> expectedModel,actualModel;
};
std::optional<VkNrPresentModelLifetime> CurrentVulkanPresentModelLifetimeVk(VkNrPresentLifetimeRead*);
}

namespace DlssNr::VkPresentSources
{
// Availability is supplied only by the existing exact-acquisition owner.
enum class GuideKind { None, Native, Captured };
struct Decision
{
    PresentSources::Decision sources;
    PresentInputDecision::Decision input;
    PresentInput::Policy admissionPolicy = PresentInput::Policy::Invalid;
    bool allowed=false,useNativeGuides=false,useCapturedGuides=false;
};
inline Decision Plan(const Connections::Mixed::Policy& policy, GuideKind kind,
    PresentInput::Policy requested, bool renderDimensionsQualified,
    PresentResolution::Policy resolution) noexcept
{
    Decision result;
    const bool available=kind!=GuideKind::None&&requested!=PresentInput::Policy::ImageOnly;
    result.sources=PresentSources::Choose(policy,available,
        kind==GuideKind::Captured?Connections::Source::BuiltIn:Connections::Source::Native);
    result.admissionPolicy=requested;
    if(!result.sources.allowed)return result;
    // A captured callback owns a whole tuple/return roundtrip. Refusal must
    // precede bind; it cannot silently transfer that owner to image-only.
    if(kind==GuideKind::Captured&&(result.sources.guidesOmitted||requested==PresentInput::Policy::ImageOnly)){
        result.sources.reason="Captured guide sources refused. Image unchanged.";
        return result;
    }
    const bool guides=available&&!result.sources.guidesOmitted;
    if(result.sources.guidesOmitted&&kind==GuideKind::Native&&
       requested!=PresentInput::Policy::Invalid&&requested!=PresentInput::Policy::ImageOnly)
        result.admissionPolicy=PresentInput::Policy::AutoGuides;
    result.input=PresentInputDecision::Choose(result.admissionPolicy,guides,renderDimensionsQualified,resolution);
    result.allowed=result.input.input!=PresentInputDecision::InputClass::Refused;
    result.useNativeGuides=result.allowed&&guides&&kind==GuideKind::Native;
    result.useCapturedGuides=result.allowed&&guides&&kind==GuideKind::Captured;
    if(!result.useNativeGuides&&!result.useCapturedGuides){
        result.sources.receipt.depth=result.sources.receipt.motion=0;
        using Connections::Mixed::Reason;
        for(auto* reason:{&result.sources.receipt.depthReason,&result.sources.receipt.motionReason})
            if(*reason==uint32_t(Reason::Ready)||*reason==uint32_t(Reason::PreferenceFallback))
                *reason=uint32_t(Reason::Unavailable);
    }
    return result;
}
inline bool CanSubmit(const Decision& plan,uint64_t revision) noexcept
{ return plan.allowed&&plan.sources.receipt.revision==revision; }

// Diagnostic identity, never an acquisition or owner capability.
struct Context
{
    uintptr_t device=0,queue=0,swapchain=0;
    uint64_t deviceGeneration=0,swapchainGeneration=0,routeEpoch=0;
    uint32_t route=0;
    bool applicationFacing=false;
    std::optional<VkNrPresentModelLifetime> modelLifetime;
    bool operator==(const Context&)const=default;
};
inline bool CurrentModelMatches(const Context& context,const std::optional<VkNrPresentModelLifetime>& current) noexcept
{
    return current&&current->Qualified()&&context.modelLifetime&&*context.modelLifetime==*current&&
        context.device==current->device&&context.deviceGeneration==current->deviceGeneration&&
        context.routeEpoch==current->routeEpoch;
}
struct Receipt
{
    Connections::SignalStatusWireV2 signals;
    Context context;
    uint64_t ticket=0,acquisition=0;
    uintptr_t image=0;
    uint32_t imageIndex=UINT32_MAX;
    bool completeTarget=false;
};
}
