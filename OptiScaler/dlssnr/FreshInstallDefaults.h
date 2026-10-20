#pragma once
#include "AutomaticSetupRequirements.h"
#include "DlssNr_StageUi.h"
#include <cstdint>
namespace DlssNr::FreshInstallDefaults {
struct Selection { unsigned route, nativeResolution; int32_t renderingMode; };
// Raw presence matters: auto, invalid, partial and future saved values are not
// evidence of a fresh selection. Existing parsers still own explicit values.
template<class HasKey> bool HasSavedSelection(HasKey hasKey)
{
    for(const auto* key:SetupRequirements::SavedSelectionKeys)
        if(hasKey(key))return true;
    return false;
}
inline constexpr Selection Missing(bool savedSelection)
{
    return savedSelection ? Selection{2,0,1} : Selection{
        SetupRequirements::Route,SetupRequirements::NativeResolution,
        static_cast<int32_t>(SetupRequirements::RenderingMode)};
}
// Preserve the established absent-key reset on Reload before supplying the
// profile-aware fallback. Explicit values still use StageUi's existing loader.
template<class C> void LoadRoute(C& cfg,std::optional<uint32_t> saved,unsigned missingRoute)
{
    StageUi::LoadRoute(cfg,saved);
    if(!saved)cfg.DlssNrRoute.set_from_config(missingRoute);
}
// Route=auto on disk historically meant Present Enhanced. Persist the effective
// route explicitly so a fresh Native selection survives the next saved-key load.
template<class Ini,class C> void SaveRoute(Ini& ini,C& cfg)
{
    ini.SetLongValue("DlssNr","Route",static_cast<long>(
        cfg.DlssNrRoute.value_for_config_or(SetupRequirements::Route)));
}
}
