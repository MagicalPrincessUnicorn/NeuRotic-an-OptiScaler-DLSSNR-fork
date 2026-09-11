#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <optional>

namespace DlssNr::PresentResolution
{
enum Mode : uint32_t { FollowNative = 0, FullOutput = 1, Custom = 2 };
inline constexpr std::array<uint32_t, 6> Percent {100, 77, 67, 58, 50, 33};
inline constexpr const char* Names[] = {
    "Follow native render resolution", "Always full output resolution", "Custom scale"
};
struct Policy { uint32_t mode = FullOutput, scale = 0; };
inline Policy Load(std::optional<uint32_t> mode, std::optional<uint32_t> scale,
                   std::optional<uint32_t> legacy = {})
{
    const auto old = (std::min)(legacy.value_or(0), 5u);
    return {mode ? (std::min)(*mode, 2u) : (old == 0 ? FullOutput : Custom),
            scale ? (std::min)(*scale, 5u) : old};
}
template<class C, class Reader> void LoadConfig(C& cfg, Reader read)
{
    const auto image = Load(read("PresentResolution"), read("PresentCustomScale"), read("PresentWorkload"));
    const auto enhanced = Load(read("EnhancedResolution"), read("EnhancedCustomScale"));
    cfg.DlssNrPresentResolution.set_from_config(image.mode);
    cfg.DlssNrPresentCustomScale.set_from_config(image.scale);
    cfg.DlssNrEnhancedResolution.set_from_config(enhanced.mode);
    cfg.DlssNrEnhancedCustomScale.set_from_config(enhanced.scale);
}
template<class Ini, class C> void SaveConfig(Ini& ini, const C& cfg)
{
    ini.Delete("DlssNr", "PresentWorkload");
    ini.SetLongValue("DlssNr", "PresentResolution", cfg.DlssNrPresentResolution.value_or_default());
    ini.SetLongValue("DlssNr", "PresentCustomScale", cfg.DlssNrPresentCustomScale.value_or_default());
    ini.SetLongValue("DlssNr", "EnhancedResolution", cfg.DlssNrEnhancedResolution.value_or_default());
    ini.SetLongValue("DlssNr", "EnhancedCustomScale", cfg.DlssNrEnhancedCustomScale.value_or_default());
}
template<class C> Policy Selected(const C& cfg)
{
    return cfg.DlssNrRoute.value_or_default() == 2
        ? Load(cfg.DlssNrEnhancedResolution.value_or_default(), cfg.DlssNrEnhancedCustomScale.value_or_default())
        : Load(cfg.DlssNrPresentResolution.value_or_default(), cfg.DlssNrPresentCustomScale.value_or_default());
}
template<class C> uint64_t CaptureKey(const C& cfg)
{
    const auto p = Selected(cfg);
    return 1ull + cfg.DlssNrRoute.value_or_default() * 64ull + p.mode * 8ull + p.scale;
}
struct Size { uint32_t width = 0, height = 0; const char* reason = nullptr; };
inline uint32_t Scaled(uint32_t full, uint32_t scale)
{
    if (full < 8) return 0;
    const auto value = (uint64_t(full) * Percent[(std::min)(scale, 5u)] + 50) / 100;
    return (std::min)((std::max)(8u, uint32_t((value + 4) & ~7ull)), full & ~7u);
}
inline Size Resolve(Policy policy, uint32_t outputW, uint32_t outputH,
                    uint32_t nativeW = 0, uint32_t nativeH = 0)
{
    if (outputW < 8 || outputH < 8 || outputW > 8192 || outputH > 8192)
        return {0, 0, "Output dimensions are outside supported NR bounds"};
    if (policy.mode == FollowNative)
    {
        if (!nativeW || !nativeH || nativeW > outputW || nativeH > outputH)
            return {0, 0, "Fresh native render-subrect dimensions are unavailable or invalid"};
        // Only the private NR allocation is aligned; guide subrects retain exact game dimensions.
        if (nativeW < 8 || nativeH < 8)
            return {0, 0, "Native render subrect is too small for NR"};
        return {nativeW & ~7u, nativeH & ~7u};
    }
    if (policy.mode == FullOutput || policy.scale == 0) return {outputW, outputH};
    return {Scaled(outputW, policy.scale), Scaled(outputH, policy.scale)};
}
}
