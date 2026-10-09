#pragma once
#include "CustomOptional.h"
#include <algorithm>
#include <cstdint>
#include <optional>
#include <string>

namespace Neurotic::Semantic::Character {
struct InspectorSettings {
    bool enabled=false,bodyBoxes=true,torsoEstimate=false;
    std::string provider="auto"; // Requested only; never an inference/capture qualification.
    uint32_t maximumPersons=4,maximumLabels=8;
    uint32_t boxHoldMs=80;
    uint32_t boxThickness=2,labelScalePercent=100,updateIntervalMs=33;
    bool autoLabelScale=true,smartBoxHandoff=true,detectObjects=true;
};
// Config/menu owner only, matching ordinary UI options. Future workers receive
// detached values; they must not read these mutable options from Present.
template<class ConfigT> InspectorSettings ReadSettings(const ConfigT& config) {
    return {config.CharacterInspectorEnabled.value_or_default(),
        config.CharacterInspectorBodyBoxes.value_or_default(),
        config.CharacterInspectorTorsoEstimate.value_or_default(),
        config.CharacterInspectorProvider.value_or_default(),
        std::clamp(config.CharacterInspectorMaximumPersons.value_or_default(),1u,16u),
        std::min(config.CharacterInspectorMaximumLabels.value_or_default(),16u),
        std::min(config.CharacterInspectorBoxHoldMs.value_or_default(),2000u),
        std::clamp(config.CharacterInspectorBoxThickness.value_or_default(),1u,8u),
        std::clamp(config.CharacterInspectorLabelScalePercent.value_or_default(),50u,300u),
        std::clamp(config.CharacterInspectorUpdateIntervalMs.value_or_default(),20u,200u),
        config.CharacterInspectorAutoLabelScale.value_or_default(),
        config.CharacterInspectorSmartBoxHandoff.value_or_default(),
        config.CharacterInspectorDetectObjects.value_or_default()};
}
template<class ConfigT,class BoolReader,class UIntReader,class StringReader>
void LoadSettings(ConfigT& config,BoolReader boolean,UIntReader number,StringReader text) {
    config.CharacterInspectorEnabled.set_from_config(boolean("CharacterInspector","Enabled"));
    config.CharacterInspectorProvider.set_from_config(text("CharacterInspector","Provider"));
    config.CharacterInspectorBodyBoxes.set_from_config(boolean("CharacterInspector","BodyBoxes"));
    config.CharacterInspectorTorsoEstimate.set_from_config(boolean("CharacterInspector","TorsoEstimate"));
    config.CharacterInspectorMaximumPersons.set_from_config(number("CharacterInspector","MaximumPersons"));
    config.CharacterInspectorMaximumLabels.set_from_config(number("CharacterInspector","MaximumLabels"));
    config.CharacterInspectorBoxHoldMs.set_from_config(number("CharacterInspector","BoxHoldMs"));
    config.CharacterInspectorBoxThickness.set_from_config(number("CharacterInspector","BoxThickness"));
    config.CharacterInspectorLabelScalePercent.set_from_config(number("CharacterInspector","LabelScalePercent"));
    config.CharacterInspectorUpdateIntervalMs.set_from_config(number("CharacterInspector","UpdateIntervalMs"));
    config.CharacterInspectorAutoLabelScale.set_from_config(boolean("CharacterInspector","AutoLabelScale"));
    config.CharacterInspectorSmartBoxHandoff.set_from_config(boolean("CharacterInspector","SmartBoxHandoff"));
    config.CharacterInspectorDetectObjects.set_from_config(boolean("CharacterInspector","DetectObjects"));
}
template<class IniT,class ConfigT> void SaveSettings(IniT& ini,ConfigT& config) {
    const auto boolean=[&](const char* key,std::optional<bool> value) {
        ini.SetValue("CharacterInspector",key,value ? (*value ? "true":"false") : "auto");
    };
    const auto number=[&](const char* key,std::optional<uint32_t> value) {
        ini.SetValue("CharacterInspector",key,value ? std::to_string(*value).c_str():"auto");
    };
    boolean("Enabled",config.CharacterInspectorEnabled.value_for_config());
    ini.SetValue("CharacterInspector","Provider",
                 config.CharacterInspectorProvider.value_for_config().value_or("auto").c_str());
    boolean("BodyBoxes",config.CharacterInspectorBodyBoxes.value_for_config());
    boolean("TorsoEstimate",config.CharacterInspectorTorsoEstimate.value_for_config());
    number("MaximumPersons",config.CharacterInspectorMaximumPersons.value_for_config());
    number("MaximumLabels",config.CharacterInspectorMaximumLabels.value_for_config());
    number("BoxHoldMs",config.CharacterInspectorBoxHoldMs.value_for_config());
    number("BoxThickness",config.CharacterInspectorBoxThickness.value_for_config());
    number("LabelScalePercent",config.CharacterInspectorLabelScalePercent.value_for_config());
    number("UpdateIntervalMs",config.CharacterInspectorUpdateIntervalMs.value_for_config());
    boolean("AutoLabelScale",config.CharacterInspectorAutoLabelScale.value_for_config());
    boolean("SmartBoxHandoff",config.CharacterInspectorSmartBoxHandoff.value_for_config());
    boolean("DetectObjects",config.CharacterInspectorDetectObjects.value_for_config());
}
}
