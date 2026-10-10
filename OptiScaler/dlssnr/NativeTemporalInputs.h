#pragma once
#include <cstdint>
#include <optional>
#include "NativeTemporalInputFacts.h"
#include "NativeNgxCreationParameters.h"
#include "NativeTemporalSource.h"
#include <memory>

namespace DlssNr {class NativeNgxCallCapture;}

namespace DlssNr::NativeTemporalInputs
{
// Per-call observation, never a resource/lifetime owner. A compatibility API
// success on a skipped/recreated frame cannot publish a successful evaluation.
class OutputEvaluation
{
    bool success_ = false, rayReconstructed_ = false;
  public:
    template<class Evaluate> bool Invoke(bool nativeRr, Evaluate&& evaluate)
    {
        success_ = rayReconstructed_ = false;
        const bool success = evaluate();
        success_ = success; rayReconstructed_ = success && nativeRr;
        return success;
    }
    bool Succeeded() const { return success_; }
    bool RayReconstructed() const { return rayReconstructed_; }
};
struct Metadata
{
    unsigned flags = 0;
    unsigned outputWidth = 0, outputHeight = 0;
    unsigned handle = 0;
    uint64_t generation = 0;
    std::shared_ptr<const NativeNgxCallCapture> alternateOriginal;
    NativeTemporalSourceObservation alternateSource;
};
// Borrow immutable facts from the existing successful-feature registry. Missing
// flags remain unknown; a known zero is a valid convention, not missing data.
inline std::optional<Metadata> FromCreation(const NativeNgxCreationParameters& creation,
                                             unsigned handle, uint64_t generation)
{
    const auto flags=creation.Value("DLSS.Feature.Create.Flags");
    const auto width=creation.Value("OutWidth"), height=creation.Value("OutHeight");
    if (!handle || !generation || !flags || !width || !height || !*width || !*height) return {};
    return Metadata{*flags,*width,*height,handle,generation};
}
struct Rect
{
    unsigned x = 0, y = 0, width = 0, height = 0;
};
constexpr unsigned ResolveFlags(std::optional<unsigned> creation, std::optional<unsigned> evaluation)
{
    return NativeTemporalInputFacts::ResolveFlags(creation, evaluation).value;
}
constexpr bool Fits(Rect region, uint64_t width, uint64_t height)
{
    return region.width && region.height && region.x <= width && region.y <= height &&
           region.width <= width - region.x && region.height <= height - region.y;
}
constexpr Rect GuideRect(unsigned x, unsigned y, unsigned renderWidth, unsigned renderHeight,
                         unsigned outputWidth, unsigned outputHeight, bool lowResolution)
{
    return {x, y, lowResolution ? renderWidth : outputWidth, lowResolution ? renderHeight : outputHeight};
}
constexpr bool Authoritative(bool superResolution, bool dlssBackend, bool dx12, bool adapter)
{
    return superResolution && dlssBackend && dx12 && !adapter;
}
}
