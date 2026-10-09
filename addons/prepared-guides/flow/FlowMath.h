#pragma once
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

namespace Neurotic::PreparedFlow
{
enum class Backend : std::uint32_t { Software, Nvidia, Auto };
enum class Encoding : std::uint32_t { Srgb = 0, Pq = 1, ScRgb = 2 };
struct Pair { std::uint64_t capture = 0, previousCapture = 0, stream = 0, generation = 0; };
struct Vector { float x, y; };
constexpr std::uint32_t Grid(Backend backend) { return backend == Backend::Nvidia ? 4 : 8; }
constexpr Vector Decode(std::int16_t x, std::int16_t y, Backend backend)
{
    const float divisor = backend == Backend::Nvidia ? 32.0f : 1.0f;
    return {x / divisor, y / divisor}; // Grid spacing never scales vector units.
}
constexpr std::uint32_t GridExtent(std::uint32_t pixels, std::uint32_t grid)
{ return pixels / grid + (pixels % grid != 0); }
inline bool FiniteLuminance(float low, float high)
{ return std::isfinite(low) && std::isfinite(high) && low >= 0 && high > low; }
constexpr bool Dimensions(std::uint32_t width, std::uint32_t height)
// The SDK's seven-level packed luma search reads four horizontal pixels at
// level six. Smaller input is rejected rather than reading a negative clamp.
{ return width >= 256 && height >= 64 && width <= 4096 && height <= 2160; }
constexpr bool Completed(std::uint64_t observed, std::uint64_t required)
{ return required != 0 && observed != (std::numeric_limits<std::uint64_t>::max)() && observed >= required; }
constexpr bool NvidiaCaps(std::uint32_t width, std::uint32_t height, std::uint32_t minWidth,
    std::uint32_t maxWidth, std::uint32_t minHeight, std::uint32_t maxHeight, bool grid4,
    bool grayscale8, bool sintVectors, bool uintCost, Encoding encoding)
{
    return Dimensions(width, height) && minWidth != 0 && minHeight != 0 &&
        maxWidth >= minWidth && maxHeight >= minHeight && width >= minWidth && width <= maxWidth &&
        height >= minHeight && height <= maxHeight && grid4 && grayscale8 && sintVectors && uintCost &&
        encoding == Encoding::Srgb;
}
// Commit only after successful submission. Failed/uncertain submissions poison
// the session instead of inventing a new history capture.
class PairHistory
{
    Pair last_{};
public:
    bool Accepts(Pair next, bool reset) const
    {
        if (!next.capture || !next.stream || !next.generation || next.previousCapture >= next.capture) return false;
        if (!last_.capture) return reset && next.previousCapture == 0;
        if (next.stream != last_.stream || next.generation != last_.generation)
            return reset && next.previousCapture == 0;
        if (next.capture <= last_.capture) return false;
        return reset ? next.previousCapture == 0 : next.previousCapture == last_.capture;
    }
    void Commit(Pair pair) { last_ = pair; }
    Pair Last() const { return last_; }
};
}
