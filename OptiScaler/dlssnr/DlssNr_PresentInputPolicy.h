#pragma once
#include <cstdint>
#include <optional>

namespace DlssNr::PresentInput
{
// Persisted schema version 1. Values are intentionally not tied to the route IDs:
// old route 1/2 profiles can retain their strict meaning until the user selects Present.
enum class Policy : uint32_t { ImageOnly = 0, RequireGuides = 1, AutoGuides = 2, Invalid = 3 };

inline Policy Resolve(std::optional<uint32_t> saved, uint32_t route, bool existingProfile)
{
    if (saved)
        return *saved <= static_cast<uint32_t>(Policy::AutoGuides)
            ? static_cast<Policy>(*saved) : Policy::Invalid;
    if (existingProfile && route == 1) return Policy::ImageOnly;
    if (existingProfile && route == 2) return Policy::RequireGuides;
    return Policy::AutoGuides;
}

template<class C> Policy Selected(const C& cfg)
{
    return Resolve(cfg.DlssNrPresentInputPolicy.value_or_default(),
        cfg.DlssNrRoute.value_or_default(), false);
}

template<class C> void LoadConfig(C& cfg, std::optional<uint32_t> saved,
                                  bool keyPresent, bool existingProfile)
{
    cfg.DlssNrPresentInputPolicy.set_from_config(static_cast<uint32_t>(
        Resolve(keyPresent ? std::optional<uint32_t>(saved.value_or(
            static_cast<uint32_t>(Policy::Invalid))) : std::nullopt,
            cfg.DlssNrRoute.value_or_default(), existingProfile)));
}

template<class Ini, class C> void SaveConfig(Ini& ini, const C& cfg)
{
    ini.SetLongValue("DlssNr", "PresentInputPolicy",
        cfg.DlssNrPresentInputPolicy.value_or_default());
}
}
