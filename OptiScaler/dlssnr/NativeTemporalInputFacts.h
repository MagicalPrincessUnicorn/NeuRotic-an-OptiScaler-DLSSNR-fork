#pragma once
#include <cmath>
#include <optional>

namespace DlssNr::NativeTemporalInputFacts
{
enum class FlagSource : unsigned { Unknown, Creation, Evaluation };
struct Flags
{
    unsigned value = 0;
    FlagSource source = FlagSource::Unknown;
    constexpr bool Known() const { return source != FlagSource::Unknown; }
};
constexpr Flags ResolveFlags(std::optional<unsigned> creation, std::optional<unsigned> evaluation)
{
    if (creation) return {*creation, FlagSource::Creation};
    if (evaluation) return {*evaluation, FlagSource::Evaluation};
    return {};
}
inline bool Finite(float mvX, float mvY, float jitterX, float jitterY)
{
    return std::isfinite(mvX) && std::isfinite(mvY) && std::isfinite(jitterX) && std::isfinite(jitterY);
}
struct Scalars
{
    std::optional<float> mvX, mvY, jitterX, jitterY;
    bool Finite() const
    {
        const auto valid = [](std::optional<float> value) { return !value || std::isfinite(*value); };
        return valid(mvX) && valid(mvY) && valid(jitterX) && valid(jitterY);
    }
    bool JitterComplete() const { return jitterX.has_value() && jitterY.has_value(); }
    float MvX() const { return mvX.value_or(1.0f); }
    float MvY() const { return mvY.value_or(1.0f); }
    // Preserve the existing pair fallback, while retaining each original fact.
    float JitterX() const { return JitterComplete() ? *jitterX : 0.0f; }
    float JitterY() const { return JitterComplete() ? *jitterY : 0.0f; }
    unsigned ObservedMask() const
    {
        return (mvX ? 1u : 0u) | (mvY ? 2u : 0u) | (jitterX ? 4u : 0u) | (jitterY ? 8u : 0u);
    }
};
inline Scalars Resolve(std::optional<float> mvX, std::optional<float> mvY,
                       std::optional<float> jitterX, std::optional<float> jitterY)
{
    return {mvX, mvY, jitterX, jitterY};
}
template<class Parameters, class Result>
std::optional<float> Read(Parameters* parameters, const char* key, Result success)
{
    float value = 0;
    if (!parameters || parameters->Get(key, &value) != success) return {};
    return value;
}
}
