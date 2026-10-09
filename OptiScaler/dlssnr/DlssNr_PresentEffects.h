#pragma once

#include <string_view>

namespace DlssNr::PresentEffects
{
// Submission can modify the target even when its completion signal cannot be tracked.
constexpr std::string_view FallbackPlacement(bool copybackSubmitted)
{
    return copybackSubmitted ? "Present output may have changed" : "Original Present fallback";
}
}
