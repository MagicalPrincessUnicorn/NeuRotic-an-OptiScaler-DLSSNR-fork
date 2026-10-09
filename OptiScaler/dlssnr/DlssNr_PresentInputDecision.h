#pragma once
#include "DlssNr_PresentInputPolicy.h"
#include "DlssNr_PresentResolution.h"

namespace DlssNr::PresentInputDecision
{
enum class InputClass { Refused, ImageOnly, Guided };
inline bool ChangesHistory(InputClass previous, InputClass next)
{
    return previous != InputClass::Refused && next != InputClass::Refused && previous != next;
}
struct Decision
{
    InputClass input = InputClass::Refused;
    PresentResolution::Policy resolution;
    bool workloadFallback = false;
    const char* reason = nullptr;
};

// The caller supplies a qualified metadata match, not merely an observed texture.
// Bind/recording failures occur later and must never call Choose again for this frame.
inline Decision Choose(PresentInput::Policy policy, bool metadataQualified,
                       bool renderDimensionsQualified, PresentResolution::Policy requested)
{
    if (policy == PresentInput::Policy::Invalid)
        return {InputClass::Refused, requested, false, "Unsupported Present input policy"};
    if (policy == PresentInput::Policy::RequireGuides && !metadataQualified)
        return {InputClass::Refused, requested, false, "Required Native guides unavailable"};
    if (policy == PresentInput::Policy::ImageOnly && requested.mode == PresentResolution::FollowNative &&
        !renderDimensionsQualified)
        return {InputClass::Refused, requested, false, "Fresh native render-subrect dimensions are unavailable"};
    Decision result {metadataQualified && policy != PresentInput::Policy::ImageOnly
            ? InputClass::Guided : InputClass::ImageOnly, requested};
    if (policy == PresentInput::Policy::AutoGuides && !metadataQualified)
        result.reason = "Native guides unavailable; image-only baseline";
    if (policy == PresentInput::Policy::AutoGuides && requested.mode == PresentResolution::FollowNative &&
        !renderDimensionsQualified)
    {
        result.resolution = {PresentResolution::FullOutput, 0};
        result.workloadFallback = true;
        result.reason = "Native render size unavailable; image-only full-output workload";
    }
    return result;
}

struct Workload
{
    PresentResolution::Size size;
    bool retained = false;
};
// admitted is an existing allocation from the SAME live Present context. It is
// only a workload choice for image-only fallback, never current Native metadata
// or permission to reuse a depth/motion resource. Unknown first use remains full.
inline Workload ResolveWorkload(const Decision& decision, uint32_t outputW, uint32_t outputH,
                               uint32_t nativeW, uint32_t nativeH,
                               PresentResolution::Size admitted = {})
{
    if (decision.input == InputClass::Refused) return {};
    if (decision.workloadFallback && decision.input == InputClass::ImageOnly &&
        !admitted.reason && outputW <= 8192 && outputH <= 8192 &&
        admitted.width >= 8 && admitted.height >= 8 &&
        admitted.width <= outputW && admitted.height <= outputH)
        return {admitted, true};
    return {PresentResolution::Resolve(decision.resolution, outputW, outputH, nativeW, nativeH), false};
}
}
