#include "nr/semantic/character/CharacterInspectorSettings.h"
#include "NrConfigState.h"
#include <SimpleIni.h>
#include <iostream>
#include <stdexcept>
using namespace Neurotic::Semantic::Character;
struct Fixture {
    CustomOptional<bool> CharacterInspectorEnabled { false };
    CustomOptional<std::string> CharacterInspectorProvider { "auto" };
    CustomOptional<bool> CharacterInspectorBodyBoxes { true };
    CustomOptional<bool> CharacterInspectorTorsoEstimate { false };
    CustomOptional<uint32_t> CharacterInspectorMaximumPersons { 4 };
    CustomOptional<uint32_t> CharacterInspectorMaximumLabels { 8 };
    CustomOptional<uint32_t> CharacterInspectorBoxHoldMs { 80 };
    CustomOptional<uint32_t> CharacterInspectorBoxThickness { 2 };
    CustomOptional<uint32_t> CharacterInspectorLabelScalePercent { 100 };
    CustomOptional<uint32_t> CharacterInspectorUpdateIntervalMs { 33 };
    CustomOptional<bool> CharacterInspectorAutoLabelScale { true };
    CustomOptional<bool> CharacterInspectorSmartBoxHandoff { true };
    CustomOptional<bool> CharacterInspectorDetectObjects { true };
    bool nrEnabled=false;
};
static int checks=0;
static void Check(bool ok,const char* why) {++checks;if(!ok)throw std::runtime_error(why);}
static void Load(Fixture& config,CSimpleIniA& ini) {
    const auto text=[&](const char* section,const char* name)->std::optional<std::string> {
        const auto* p=ini.GetValue(section,name,nullptr); if(!p || std::string(p)=="auto") return {};
        return std::string(p);
    };
    const auto boolean=[&](const char* section,const char* name)->std::optional<bool> {
        auto p=text(section,name);if(!p)return {};return *p=="true";
    };
    const auto number=[&](const char* section,const char* name)->std::optional<uint32_t> {
        auto p=text(section,name);if(!p)return {};return static_cast<uint32_t>(std::stoul(*p));
    };
    LoadSettings(config,boolean,number,text);
}
int main() {
 try {
    Fixture config; CSimpleIniA ini;
    const auto nrRevision=NrConfigSynchronization::ObservationRevision();
    Load(config,ini);
    Check(NrConfigSynchronization::ObservationRevision()==nrRevision,"inspector load leaves NR observation revision unchanged");
    auto settings=ReadSettings(config);
    Check(!settings.enabled && settings.bodyBoxes && !settings.torsoEstimate,"absent config inert defaults");
    Check(settings.maximumPersons==4 && settings.maximumLabels==8,"default bounds");
    Check(settings.boxHoldMs==80,"default box hold is 80ms");
    Check(settings.updateIntervalMs==33&&settings.smartBoxHandoff&&settings.autoLabelScale&&settings.boxThickness==2&&settings.labelScalePercent==100&&settings.detectObjects,"responsive appearance defaults");
    Check(settings.provider=="auto","Auto remains a request, not installed model evidence");
    for(bool nr:{false,true}) {
        config.nrEnabled=nr; config.CharacterInspectorEnabled=false;
        Check(!ReadSettings(config).enabled,"NR cannot enable inspector");
        config.CharacterInspectorEnabled=true;
        Check(ReadSettings(config).enabled && config.nrEnabled==nr,"inspector cannot change NR");
    }
    Fixture explicitConfig;
    Check(ini.LoadData("[CharacterInspector]\nEnabled=true\nProvider=future_provider\nBodyBoxes=false\nTorsoEstimate=true\nMaximumPersons=4294967295\nMaximumLabels=0\n[DlssNr]\nEnabled=true\n")>=0,"parse fixture");
    Load(explicitConfig,ini); settings=ReadSettings(explicitConfig);
    Check(settings.enabled && !settings.bodyBoxes && settings.torsoEstimate,"explicit options loaded");
    Check(settings.maximumPersons==16 && settings.maximumLabels==0,"effective values bounded");
    Check(settings.provider=="future_provider","unknown provider preserved");
    SaveSettings(ini,explicitConfig);
    Check(std::string(ini.GetValue("DlssNr","Enabled"))=="true","saving inspector preserves NR section");
    Check(std::string(ini.GetValue("CharacterInspector","MaximumPersons"))=="4294967295","save preserves explicit raw value");
    std::string saved;Check(ini.Save(saved)>=0,"serialize roundtrip");
    CSimpleIniA loaded;Check(loaded.LoadData(saved.c_str())>=0,"parse roundtrip");
    Fixture restored;Load(restored,loaded);
    auto roundtrip=ReadSettings(restored);
    Check(roundtrip.enabled && roundtrip.provider=="future_provider" && roundtrip.maximumLabels==0,
          "roundtrip independent settings");
    explicitConfig.CharacterInspectorMaximumPersons=0;
    explicitConfig.CharacterInspectorMaximumLabels=999;
    settings=ReadSettings(explicitConfig);
    Check(settings.maximumPersons==1 && settings.maximumLabels==16,"lower/upper effective limits");
    explicitConfig.CharacterInspectorBoxHoldMs=9999;
    Check(ReadSettings(explicitConfig).boxHoldMs==2000,"hold time upper limit");
    SaveSettings(ini,explicitConfig);Fixture holdRestored;Load(holdRestored,ini);
    Check(ReadSettings(holdRestored).boxHoldMs==2000,"hold option roundtrips with effective bound");
    explicitConfig.CharacterInspectorBoxHoldMs=0;
    Check(ReadSettings(explicitConfig).boxHoldMs==0,"zero disables hold");
    explicitConfig.CharacterInspectorBoxThickness=99;explicitConfig.CharacterInspectorLabelScalePercent=1;
    explicitConfig.CharacterInspectorUpdateIntervalMs=0;explicitConfig.CharacterInspectorAutoLabelScale=false;
    explicitConfig.CharacterInspectorSmartBoxHandoff=false;explicitConfig.CharacterInspectorDetectObjects=true;
    SaveSettings(ini,explicitConfig);Fixture appearance;Load(appearance,ini);const auto bounded=ReadSettings(appearance);
    Check(bounded.boxThickness==8&&bounded.labelScalePercent==50&&bounded.updateIntervalMs==20,"appearance and cadence clamp on roundtrip");
    Check(!bounded.autoLabelScale&&!bounded.smartBoxHandoff&&bounded.detectObjects,"detector and appearance mode roundtrip");
    SaveSettings(ini,config); // defaults use the project's auto/unset serialization.
    Check(NrConfigSynchronization::ObservationRevision()==nrRevision,"inspector edits/save cannot reset NR exposure calibration");
    Check(std::string(ini.GetValue("CharacterInspector","Provider"))=="auto","default provider saved auto");
    std::cout << "PASS " << checks << " settings/roundtrip checks\n"; return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
