#pragma once
#include "RecipeTypes.h"

namespace Neurotic::Protocol
{
// Invoke with the already captured NrConfigSnapshot (or its immutable first-layer
// view). It owns all values; no Config singleton, parser, or retained reference exists.
template<class Snapshot> std::optional<SettingsSnapshot> ProjectLegacySettings(const Snapshot& snapshot,
    const C::OwnerValueReference& source,const C::EvidenceRef& evidence)
{
    if(!Context::ValidValues(source)||source.owner!=C::OwnerDomain::Configuration||source.recordType.View()!="NR.RenderSettings"||
       source.schemaVersion!=C::VersionNumber{1,0}||!Lifecycle::ValidEvidence(evidence))return {};
    SettingsSnapshot result;result.source=source;
    const auto add=[&](std::string_view key,const auto& option){
        using T=std::remove_cvref_t<decltype(option.value_or_default())>;
        const auto scalar=[](const T& value)->C::ScalarValue{
            if constexpr(std::is_floating_point_v<T>)return static_cast<double>(value);
            else return static_cast<std::uint64_t>(value);};
        C::SemanticClaim control;control.field=Symbol(key);
        control.effective=C::OptionalFact<C::ScalarValue>::FromKnown(scalar(option.value_or_default()),evidence);
        if(option.has_value())control.raw=C::OptionalFact<C::ScalarValue>::FromKnown(scalar(option.value()),evidence);
        return Context::ValidValues(control)&&result.controls.Push(control);
    };
    if(!add("DLSSNR.Hint.Render.Preset",snapshot.DlssNrPreset)||!add("DLSSNR.Style",snapshot.DlssNrStyle)||
       !add("DLSSNR.UseAutoMask",snapshot.DlssNrAutoMask)||!add("DLSSNR.Intensity",snapshot.DlssNrIntensity)||
       !add("DLSSNR.LocalStructureStrength",snapshot.DlssNrLocalStructure)||!add("DLSSNR.LocalToneStrength",snapshot.DlssNrLocalTone)||
       !add("DLSSNR.SkinStructureStrength",snapshot.DlssNrSkinStructure))return {};
    return result;
}
}
