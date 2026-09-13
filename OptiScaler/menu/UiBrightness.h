#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Neurotic::UiBrightness
{
inline float Clamp(float gain) { return std::isfinite(gain) ? std::clamp(gain, 1.0f, 3.0f) : 1.0f; }
inline uint32_t Apply(uint32_t color, float gain)
{
    gain = Clamp(gain);
    if (gain == 1.0f) return color;
    // ImGui packs alpha in the upper byte in both supported RGB/BGR layouts.
    uint32_t result = color & 0xff000000u;
    for (unsigned shift = 0; shift < 24; shift += 8)
        result |= static_cast<uint32_t>(std::min(255.0f, ((color >> shift) & 255u) * gain + 0.5f)) << shift;
    return result;
}
}
