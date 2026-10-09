#include <menu/Localization.h>
#pragma once
#include "HubViewModel.h"
#include "NumericSettings.h"
#include "KeybindCapture.h"
#include "ObjectRuleHost.h"
#include "SleekWidgets.h"
#include "imgui_internal.h"
#include "../../../OptiScaler/menu/SleekContentCard.h"
#include "../../../OptiScaler/menu/SleekPilotLight.h"
#include <string_view>

namespace nh {
// Keep the names and ordering aligned with OptiScaler/menu/SleekShell.h.
inline const const char* SavedSettingsPages[]={Neurotic::UiLiteral("desktop.option.c910d474dcd7", "General"),Neurotic::UiLiteral("desktop.option.d827fea4f9a0", "Upscaling"),Neurotic::UiLiteral("desktop.gamesettingsview.neural_rendering_6adc4e31", "Neural Rendering"),Neurotic::UiLiteral("desktop.gamesettingsview.frame_generation_faaea68e", "Frame Generation"),Neurotic::UiLiteral("desktop.gamesettingsview.advanced_0f9eefbe", "Advanced"),Neurotic::UiLiteral("desktop.option.ea93d6a262ec", "Tools"),Neurotic::UiLiteral("desktop.hubshell.diagnostics_1be508a4", "Diagnostics")};
inline const std::vector<const char*>& SavedSettingsChildren(int page){
 static const std::vector<const char*> children[]={
  {"Updates",Neurotic::UiLiteral("desktop.gamesettingsview.key_binds_2fb9566d", "Key Binds"),Neurotic::UiLiteral("desktop.gamesettingsview.gameplay_input_560d4e0f", "Gameplay Input"),"Theme & Color","Brightness",Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language"),Neurotic::UiLiteral("desktop.gamesettingsview.animations_3cd42432", "Animations")},
  {"Upscalers","DLSS","FSR / XeSS","Image","Initialization"},
  {"Main",Neurotic::UiLiteral("desktop.gamesettingsview.multi_pass_878b745f", "Multi Pass"),Neurotic::UiLiteral("desktop.gamesettingsview.object_rules_experimental_8bfc57d8", "Object Rules (Experimental)"),"Overrides","Inspector (Experimental)"},
  {Neurotic::UiLiteral("desktop.gamesettingsview.native_mfg_600c59ca", "Native MFG"),Neurotic::UiLiteral("desktop.gamesettingsview.frame_generation_faaea68e", "Frame Generation")},
  {Neurotic::UiLiteral("desktop.settings.v-sync/forcevsync.784de20752", "V-Sync"),Neurotic::UiLiteral("desktop.gamesettingsview.anisotropic_filtering_acc43b27", "Anisotropic Filtering"),"Settings",Neurotic::UiLiteral("desktop.gamesettingsview.resource_barriers_d9fde047", "Resource Barriers"),Neurotic::UiLiteral("desktop.gamesettingsview.root_signatures_b4bc7597", "Root Signatures")},
  {"Screenshots","Magnifier",Neurotic::UiLiteral("desktop.gamesettingsview.mipmap_bias_af26814f", "Mipmap Bias")},
  {"Logging","Quirks",Neurotic::UiLiteral("desktop.gamesettingsview.fps_overlay_42426f0c", "FPS Overlay"),Neurotic::UiLiteral("desktop.anythingview.capture_452b69c6", "Capture")}};
 return children[std::clamp(page,0,6)];
}
// Presentation order is independent of existing saved page identities.
inline int SavedSettingsVisualChild(int,int ordinal){return ordinal;}
struct SavedSettingsLocation {int page=0,child=0;const char* section="Settings";};
inline bool SavedKeyIn(std::string_view key,std::initializer_list<std::string_view> keys){return std::find(keys.begin(),keys.end(),key)!=keys.end();}
inline bool IsInstallationCompatibilitySetting(std::string_view section,std::string_view key){
 return section=="Plugins"||section=="Hooks"||section=="Spoofing"||
  (section=="Hotfix"&&SavedKeyIn(key,{"RestoreComputeSignature","RestoreGraphicSignature"}))||
  (section=="OptiFG"&&key=="DisableHUDFix");
}
inline SavedSettingsLocation SavedSettingsPlacement(std::string_view section,std::string_view key){
 if(IsInstallationCompatibilitySetting(section,key))return {-3,-1,"Installation & compatibility"};
 if(section=="ObjectRules"&&key=="ProfileHex")return {2,2,Neurotic::UiLiteral("desktop.gamesettingsview.object_rules_bb34e3c8", "Object Rules")};
 if(section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")){
  if(key=="PreflightExpanded")return {-2,-1,Neurotic::UiLiteral("desktop.gamesettingsview.in_game_layout_preference_5142df12", "In-game layout preference")};
  if(key=="LightTheme")return {-2,-1,Neurotic::UiLiteral("desktop.gamesettingsview.preserved_theme_preference_0b9820cc", "Preserved theme preference")};
  if(key=="ReduceMotion")return {0,6,Neurotic::UiLiteral("desktop.gamesettingsview.animations_3cd42432", "Animations")};
  if(key=="ExtendedLimits")return {4,2,"Settings"};
  if(SavedKeyIn(key,{"ShortcutKey","FpsShortcutKey","FpsCycleShortcutKey","FGShortcutKey","EscapeClosesMenu"}))return {0,1,Neurotic::UiLiteral("desktop.gamesettingsview.key_binds_2fb9566d", "Key Binds")};
  if(key.starts_with("AllowGame"))return {0,2,Neurotic::UiLiteral("desktop.gamesettingsview.gameplay_input_560d4e0f", "Gameplay Input")};
  if(key=="Brightness")return {0,4,"Brightness"};
  if(key==Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language"))return {0,5,Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language")};
  if(key.starts_with("AccentColor"))return {0,3,Neurotic::UiLiteral("desktop.gamesettingsview.accent_colour_f6a004f9", "Accent Colour")};
  if(key.starts_with("BGColor"))return {0,3,Neurotic::UiLiteral("desktop.gamesettingsview.background_colour_ea46cc3d", "Background Colour")};
  if(key=="ShowFps"||key.starts_with("Fps"))return {6,2,Neurotic::UiLiteral("desktop.gamesettingsview.fps_overlay_42426f0c", "FPS Overlay")};
  return {0,3,"Theme & Color"};
 }
 if(section==Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")){
  if(key=="ToggleKey")return {0,1,Neurotic::UiLiteral("desktop.gamesettingsview.key_binds_2fb9566d", "Key Binds")};
  if(key=="Route"||key=="PresentInputPolicy")return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")};
  if(SavedKeyIn(key,{"ExperimentalMode","PreSrSoftReset","PreparedDepth"}))return {2,3,Neurotic::UiLiteral("desktop.gamesettingsview.neural_rendering_experimental_overrides_d350ad7d", "Neural Rendering - Experimental Overrides")};
  if(key=="MultipassEnabled"||key=="Passes")return {2,1,"Multipass"};
  if(key=="DebugView")return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.debug_view_d4c28b96", "Debug view")};
  if(key.starts_with(Neurotic::UiLiteral("desktop.settings.dlssnr/compare.e888155d8c", "Compare"))||key=="TagScale")return {2,0,Neurotic::UiLiteral("desktop.settings.dlssnr/compare.e888155d8c", "Compare")};
  if(SavedKeyIn(key,{"WhitePointSource","WhitePointScale","WhitePointTrim","ScanTrim","ScanInverted","ScanExposure","ScanMeter","ScanAnchorWhitePoint","InputExposureScale","ExposureTrim","MaxRatio"}))return {2,0,"Colour"};
  if(SavedKeyIn(key,{"ScalingDownscaler","Transfer"}))return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.advanced_resampling_993d6684", "Advanced resampling")};
  if(key=="ReversibleMode")return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.reversible_proxy_4e3e290b", "Reversible proxy")};
  if(SavedKeyIn(key,{"LocalStructure","LocalTone","SkinStructure","AutoMask"}))return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.local_structure_and_skin_d7adf3b8", "Local structure and skin")};
  return {2,0,Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")};
 }
 if(section=="DlssNrBasic")return {2,1,"Basic"};
 if(section.starts_with("DlssNrLayer")){
  const char* passes[]={Neurotic::UiLiteral("desktop.provider.b0c3ecfce51e", "Pass 2"),Neurotic::UiLiteral("desktop.provider.8283f273c34e", "Pass 3"),Neurotic::UiLiteral("desktop.provider.338f803f213f", "Pass 4"),Neurotic::UiLiteral("desktop.provider.d87384d4d1b8", "Pass 5"),Neurotic::UiLiteral("desktop.provider.bdcb2fec05b8", "Pass 6"),Neurotic::UiLiteral("desktop.provider.41d23cd4f1d6", "Pass 7"),Neurotic::UiLiteral("desktop.provider.bd3b2b3c5194", "Pass 8"),Neurotic::UiLiteral("desktop.provider.24cc594aecb4", "Pass 9"),Neurotic::UiLiteral("desktop.provider.8f95295589b0", "Pass 10")};
  for(int i=2;i<=10;++i)if(section=="DlssNrLayer"+std::to_string(i))return {2,1,passes[i-2]};
 }
 if(section=="CharacterInspector"){
  if(SavedKeyIn(key,{"Enabled","DetectObjects","TorsoEstimate","MaximumPersons","UpdateIntervalMs"}))return {2,4,"Detection"};
  return {2,4,"Overlay"};
 }
 if(section=="DLSSG")return {3,0,Neurotic::UiLiteral("desktop.gamesettingsview.native_mfg_600c59ca", "Native MFG")};
 if(section=="FrameGen")return {3,1,Neurotic::UiLiteral("desktop.gamesettingsview.frame_generation_faaea68e", "Frame Generation")};
 if(section=="FSRFG"||section=="FSRFGInputs")return {3,1,Neurotic::UiLiteral("desktop.gamesettingsview.extended_fsr_fg_settings_0f769110", "Extended FSR FG Settings")};
 if(section=="XeFG")return {3,1,Neurotic::UiLiteral("desktop.gamesettingsview.extended_xefg_settings_1859a89e", "Extended XeFG Settings")};
 if(section=="OptiFG")return {3,1,Neurotic::UiLiteral("desktop.gamesettingsview.advanced_optifg_settings_7a608566", "Advanced OptiFG Settings")};
 if(section=="NvngxFG")return {3,1,Neurotic::UiLiteral("desktop.gamesettingsview.native_ngx_frame_generation_a6b9e802", "Native NGX Frame Generation")};
 if(section=="Framerate"||section=="fakenvapi")return {3,1,"Framerate & latency"};
 if(section=="Screenshots")return key==Neurotic::UiLiteral("desktop.anythingview.key_df9dc9c4", "Key")?SavedSettingsLocation{0,1,Neurotic::UiLiteral("desktop.gamesettingsview.key_binds_2fb9566d", "Key Binds")}:SavedSettingsLocation{5,0,"Images"};
 if(section=="Magnifier")return {5,1,"Magnifier"};
 if(section=="Mipmap")return {5,2,Neurotic::UiLiteral("desktop.gamesettingsview.mipmap_bias_af26814f", "Mipmap Bias")};
 if(section=="Log")return {6,0,"Logging"};
 if(section=="Hotfix"){
  if(key=="CheckForUpdate")return {0,0,"Updates"};
  if(key=="UsePrecompiledShaders")return {4,2,"Settings"};
  if(key=="RestoreComputeSignature"||key=="RestoreGraphicSignature")return {4,4,Neurotic::UiLiteral("desktop.gamesettingsview.root_signatures_b4bc7597", "Root Signatures")};
  if(key.find("ResourceBarrier")!=key.npos)return {4,3,Neurotic::UiLiteral("desktop.gamesettingsview.resource_barriers_d9fde047", "Resource Barriers")};
  return {6,1,"Quirks"};
 }
 if(section==Neurotic::UiLiteral("desktop.settings.v-sync/forcevsync.784de20752", "V-Sync"))return {4,0,Neurotic::UiLiteral("desktop.settings.v-sync/forcevsync.784de20752", "V-Sync")};
 if(section=="Anisotropy")return {4,1,Neurotic::UiLiteral("desktop.gamesettingsview.anisotropic_filtering_acc43b27", "Anisotropic Filtering")};
 if(section=="DRS"||section=="Shaders")return {4,2,"Settings"};
 if(section=="DLSS")return {1,1,"DLSS"};
 if(section=="DLSSD")return {1,1,Neurotic::UiLiteral("desktop.gamesettingsview.ray_reconstruction_d3716237", "Ray Reconstruction")};
 if(section=="FSR"||section=="XeSS")return {1,2,section=="FSR"?"FSR":"XeSS"};
 if(section=="Sharpness"||section=="OutputScaling"||section=="CAS")return {1,3,"Image"};
 if(section=="InitFlags")return {1,4,"Initialization"};
 if(section=="Inputs"||section=="Upscalers")return {1,0,"Upscalers"};
 if(section=="Dx11withDx12")return {1,0,Neurotic::UiLiteral("desktop.gamesettingsview.dx11_with_dx12_settings_c9eab7c7", "Dx11 with Dx12 Settings")};
 if(section=="UpscaleRatio"||section=="QualityOverrides")return {1,0,Neurotic::UiLiteral("desktop.gamesettingsview.upscale_ratio_overrides_4aad47ec", "Upscale Ratio Overrides")};
 return {-1,-1,Neurotic::UiLiteral("desktop.gamesettingsview.additional_saved_settings_df0294d0", "Additional saved settings")};
}

// Inventory only: these values stay untouched by the desktop editor. They do not
// create disabled buttons or runtime-only sections in the saved-settings UI.
struct UnavailableSavedControl {int page,child;const char* section;const char* key;const char* label;const char* reason;};
inline const UnavailableSavedControl UnavailableSavedControls[]={
 {1,2,"FSR","UpscalerIndex",Neurotic::UiLiteral("desktop.gamesettingsview.ffx_upscaler_92b2f444", "FFX Upscaler"),Neurotic::UiLiteral("desktop.gamesettingsview.choices_are_indexed_by_the_ffx_sdk_loaded_in_the_baa98c45", "Choices are indexed by the FFX SDK loaded in the game. An index cannot be matched to an installed provider here.")},
 {1,2,"FSR","FsrNonLinearColorSpace",Neurotic::UiLiteral("desktop.gamesettingsview.input_color_space_26b0f8ac", "Input Color Space"),Neurotic::UiLiteral("desktop.gamesettingsview.requires_the_active_fsr_4_input_format_the_loade_22e5aac7", "Requires the active FSR 4 input format. The loader also derives Non-Linear from the explicit sRGB/PQ keys; this editor preserves that coupled legacy state.")},
 {1,2,"FSR","FsrNonLinearSRGB",Neurotic::UiLiteral("desktop.gamesettingsview.input_color_space_26b0f8ac", "Input Color Space"),Neurotic::UiLiteral("desktop.gamesettingsview.requires_the_active_fsr_4_input_format_the_loade_22e5aac7", "Requires the active FSR 4 input format. The loader also derives Non-Linear from the explicit sRGB/PQ keys; this editor preserves that coupled legacy state.")},
 {1,2,"FSR","FsrNonLinearPQ",Neurotic::UiLiteral("desktop.gamesettingsview.input_color_space_26b0f8ac", "Input Color Space"),Neurotic::UiLiteral("desktop.gamesettingsview.requires_the_active_fsr_4_input_format_the_loade_22e5aac7", "Requires the active FSR 4 input format. The loader also derives Non-Linear from the explicit sRGB/PQ keys; this editor preserves that coupled legacy state.")},
 {3,1,"FSR","FGIndex",Neurotic::UiLiteral("desktop.gamesettingsview.ffx_fg_13d08769", "FFX FG"),Neurotic::UiLiteral("desktop.gamesettingsview.choices_are_indexed_by_the_ffx_sdk_loaded_in_the_447a9db5", "Choices are indexed by the FFX SDK loaded in the game. Existing provider indices are preserved.")},
 {3,1,"FrameGen","RectLeft",Neurotic::UiLiteral("desktop.gamesettingsview.rect_left_a28f1bf1", "Rect Left"),Neurotic::UiLiteral("desktop.gamesettingsview.the_valid_rectangle_depends_on_the_game_s_curren_30f2fc17", "The valid rectangle depends on the game's current frame dimensions and active frame-generation provider.")},
 {3,1,"FrameGen","RectTop",Neurotic::UiLiteral("desktop.gamesettingsview.rect_top_c16f835a", "Rect Top"),Neurotic::UiLiteral("desktop.gamesettingsview.the_valid_rectangle_depends_on_the_game_s_curren_30f2fc17", "The valid rectangle depends on the game's current frame dimensions and active frame-generation provider.")},
 {3,1,"FrameGen","RectWidth",Neurotic::UiLiteral("desktop.gamesettingsview.rect_width_97f1cad6", "Rect Width"),Neurotic::UiLiteral("desktop.gamesettingsview.the_valid_rectangle_depends_on_the_game_s_curren_30f2fc17", "The valid rectangle depends on the game's current frame dimensions and active frame-generation provider.")},
 {3,1,"FrameGen","RectHeight",Neurotic::UiLiteral("desktop.gamesettingsview.rect_height_9ac43421", "Rect Height"),Neurotic::UiLiteral("desktop.gamesettingsview.the_valid_rectangle_depends_on_the_game_s_curren_30f2fc17", "The valid rectangle depends on the game's current frame dimensions and active frame-generation provider.")},
 {3,1,"FSRFG","FPTSafetyMarginInMs",Neurotic::UiLiteral("desktop.gamesettingsview.safety_margins_in_ms_ef5f972a", "Safety Margins in ms"),Neurotic::UiLiteral("desktop.gamesettingsview.the_in_game_control_is_unbounded_no_validated_po_8664cbde", "The in-game control is unbounded. No validated portable provider range is defined, so its saved value remains preserved.")},
 {3,1,"OptiFG","DepthScaleMax",Neurotic::UiLiteral("desktop.gamesettingsview.fg_scale_depth_max_ce0155b3", "FG Scale Depth Max"),Neurotic::UiLiteral("desktop.gamesettingsview.this_value_divides_active_game_depth_the_in_game_43991363", "This value divides active game depth. The in-game input is unbounded; a safe range cannot be inferred without its depth format.")},
 {3,1,"NvngxFG","DispatchFlags",Neurotic::UiLiteral("desktop.gamesettingsview.raw_dispatchflags_65dabb41", "Raw DispatchFlags"),Neurotic::UiLiteral("desktop.gamesettingsview.provider_specific_bit_flags_require_the_running__a0000708", "Provider-specific bit flags require the running game's dispatch definitions. Arbitrary bitfield writes are unavailable here.")},
 {4,1,"Anisotropy","ModifyComparison",Neurotic::UiLiteral("desktop.gamesettingsview.modify_compare_a31a314e", "Modify Compare"),Neurotic::UiLiteral("desktop.gamesettingsview.the_game_loader_reads_afmodifycomparison_while_i_c363b7b2", "The game loader reads AFModifyComparison while its saver writes ModifyComparison. Both existing keys remain preserved until this mismatch is resolved.")},
 {4,1,"Anisotropy","ModifyMinMax",Neurotic::UiLiteral("desktop.gamesettingsview.modify_min_max_02123306", "Modify Min/Max"),Neurotic::UiLiteral("desktop.gamesettingsview.the_game_loader_reads_afmodifyminmax_while_its_s_7d42b506", "The game loader reads AFModifyMinMax while its saver writes ModifyMinMax. Both existing keys remain preserved until this mismatch is resolved.")},
 {2,0,Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr"),"ScanAnchors",Neurotic::UiLiteral("desktop.gamesettingsview.anchor_here_7b75e0a9", "Anchor here"),Neurotic::UiLiteral("desktop.gamesettingsview.anchors_serialize_measurements_from_the_current__0208224f", "Anchors serialize measurements from the current exposure scan. Creating an anchor requires a running game; saved anchors remain preserved.")},
 {2,0,Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr"),"HoldFrame",Neurotic::UiLiteral("desktop.gamesettingsview.hold_frame_d9b69908", "Hold frame"),Neurotic::UiLiteral("desktop.gamesettingsview.holding_a_frame_requires_the_running_game_s_curr_c86f6178", "Holding a frame requires the running game's current image. Existing saved hold-frame state remains preserved.")}
};
inline void RenderSavedSetting(SettingsField& field,HubModel& model,KeybindCaptureState& capture,const std::string& scope,bool compatibility=false,bool card=false){
 // Key capture polls raw input, so cancel listening as well as disabling widgets.
 if(model.installer.busy)capture.ResetListening();
 ImGui::PushID((field.section+"/"+field.key).c_str());ImGui::BeginDisabled(!field.available||model.installer.busy);
 if(field.section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")&&field.key==Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language")){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.language_is_shared_across_games_manage_it_in_app_fc7f57e2", "Language is shared across games. Manage it in App Settings. The old INI value is preserved."));ImGui::EndDisabled();ImGui::PopID();return;}
 if(field.section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")&&field.key=="ReduceMotion"){
  bool reduced=std::string_view(field.draft.data())==Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true");
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,2.f);
  if(ImGui::Checkbox(Neurotic::UiLiteral("desktop.gamesettingsview.turn_animations_off_d9407864", "Turn animations off"),&reduced))strcpy_s(field.draft.data(),field.draft.size(),reduced?Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true"):Neurotic::UiLiteral("desktop.gamesettingsview.false_ec9d3153", "false"));
  ImGui::PopStyleVar();ImGui::EndDisabled();ImGui::PopID();return;
 }
 if(field.type=="objectrules"){
  DrawObjectRuleHost(field,model.settings);
  if(field.profileDraft!=field.original&&ImGui::SmallButton(Neurotic::UiLiteral("desktop.gamesettingsview.undo_profile_changes_49dfe1bf", "Undo profile changes"))){field.profileDraft=field.original;field.ruleStore.reset();field.ruleEditor.reset();}
  ImGui::EndDisabled();
  if(!field.available)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.the_existing_profile_is_preserved_resolve_duplic_feed7f2c", "The existing profile is preserved. Resolve duplicate or invalid Object Rules entries in OptiScaler.ini before editing."));
  ImGui::PopID();return;
 }
 const auto label=field.label.empty()?field.key:field.label;
 Neurotic::ScopedUiLiteral labelBinding(field.labelId,label.c_str());Neurotic::ScopedUiLiteral descriptionBinding(field.descriptionId,field.description.c_str());
 const auto experimentalHelp=[&]{
  if(field.section!=Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")||!SavedKeyIn(field.key,{"ExperimentalMode","PreSrSoftReset","PreparedDepth"})||field.description.empty())return;
  ImGui::SameLine();ImGui::TextDisabled("(?)");
  if(ImGui::IsItemHovered(ImGuiHoveredFlags_DelayShort))ImGui::SetTooltip("%s",Neurotic::Translate(field.description.c_str()).c_str());
 };
 const bool boolean=std::find(field.choices.begin(),field.choices.end(),Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true"))!=field.choices.end()&&
  std::find(field.choices.begin(),field.choices.end(),Neurotic::UiLiteral("desktop.gamesettingsview.false_ec9d3153", "false"))!=field.choices.end()&&field.choices.size()<=3;
 if(boolean&&!compatibility){
  const std::string value=field.draft.data();bool enabled=value==Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true");
  const bool mixed=value!=Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true")&&value!=Neurotic::UiLiteral("desktop.gamesettingsview.false_ec9d3153", "false");
  ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,mixed);
  // A large theme rounding made the empty/automatic box look circular, while
  // the explicit checked state contained a square check. Keep one box shape.
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,2.f);
  if(ImGui::Checkbox(label.c_str(),&enabled))strcpy_s(field.draft.data(),field.draft.size(),enabled?Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true"):Neurotic::UiLiteral("desktop.gamesettingsview.false_ec9d3153", "false"));
  ImGui::PopStyleVar();
  ImGui::PopItemFlag();
  experimentalHelp();
  if(mixed){ImGui::SameLine();ImGui::TextDisabled("(%s)",Neurotic::Translate(value==Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto")?Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c", "Automatic"):value.c_str()).c_str());}
  if(std::find(field.choices.begin(),field.choices.end(),Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"))!=field.choices.end()){
   ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.numericsettings.auto_d2dcb07c", "Auto")))strcpy_s(field.draft.data(),field.draft.size(),Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"));
  }
 }else{
  const bool compactTheme=field.section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")&&SavedSettingsPlacement(field.section,field.key).child==3;
  ImGui::AlignTextToFramePadding();ImGui::TextWrapped("%s",Neurotic::Translate(label.c_str()).c_str());
  experimentalHelp();
  if(compactTheme&&ImGui::GetContentRegionAvail().x>ImGui::GetFontSize()*34)ImGui::SameLine(ImGui::GetFontSize()*14);
  ImGui::SetNextItemWidth(card?ImGui::GetContentRegionAvail().x:std::min(ImGui::GetFontSize()*20,ImGui::GetContentRegionAvail().x));
  if(field.type=="keycode")RenderKeybindCapture(field,model.settings,capture,scope);
  else if(!field.choices.empty()){
   auto friendly=[&](const std::string& value){if(compatibility&&value==Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"))return std::string(Neurotic::UiLiteral("desktop.numericsettings.auto_d2dcb07c", "Auto"));auto found=field.valueLabels.find(value);return found!=field.valueLabels.end()?found->second:value==Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto")?std::string(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c", "Automatic")):value==Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true")?std::string("Enabled"):value==Neurotic::UiLiteral("desktop.gamesettingsview.false_ec9d3153", "false")?std::string("Disabled"):value;};
   auto choiceId=[&](const std::string& value){auto found=field.valueLabelIds.find(value);return found!=field.valueLabelIds.end()?found->second:std::string{};};
   auto preview=friendly(field.draft.data());Neurotic::ScopedUiLiteral previewBinding(choiceId(field.draft.data()),preview.c_str());
   if(ImGui::BeginCombo("##Value",preview.c_str())){for(auto& choice:field.choices){auto text=friendly(choice);Neurotic::ScopedUiLiteral choiceBinding(choiceId(choice),text.c_str());ImGui::PushID(choice.c_str());if(ImGui::Selectable(text.c_str(),choice==field.draft.data()))strncpy_s(field.draft.data(),field.draft.size(),choice.c_str(),_TRUNCATE);ImGui::PopID();}ImGui::EndCombo();}
  }else{
   const double savedMinimum=field.minimum,savedMaximum=field.maximum;
   if((field.section=="QualityOverrides"&&field.key!="QualityRatioOverrideEnabled")||
      (field.section=="UpscaleRatio"&&field.key=="UpscaleRatioOverrideValue")){
    const auto extended=std::find_if(model.settings.begin(),model.settings.end(),[](const auto& item){return item.section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")&&item.key=="ExtendedLimits";});
    const bool wide=extended!=model.settings.end()&&_stricmp(extended->draft.data(),Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true"))==0;
    field.minimum=wide?.1:1;field.maximum=wide?6:3;
   }
   RenderNumericSetting(field,model.settings,card);field.minimum=savedMinimum;field.maximum=savedMaximum;
  }
 }
 if(compatibility){ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset"))){capture.ResetListening();strcpy_s(field.draft.data(),field.draft.size(),Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"));}}
 if(field.original!=field.draft.data()){ImGui::SameLine();if(ImGui::SmallButton(Neurotic::UiLiteral("desktop.provider.a8283ade3185", "Undo"))){capture.ResetListening();strncpy_s(field.draft.data(),field.draft.size(),field.original.c_str(),_TRUNCATE);}}
 ImGui::EndDisabled();if(!field.available)ImGui::TextDisabled(Neurotic::UiLiteral("desktop.gamesettingsview.this_entry_is_preserved_duplicate_keys_sections__376ab0f6", "This entry is preserved: duplicate keys/sections or a value that cannot be safely edited here. Check OptiScaler.ini before editing this setting."));
 ImGui::PopID();
}
inline void RenderSavedSettingsReview(HubModel& model){
 ImGui::Separator();
 const bool unfinishedRule=std::any_of(model.settings.begin(),model.settings.end(),[](const auto& field){return field.ruleEditor&&field.ruleEditor->dirty;});
 if(unfinishedRule)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.apply_or_reset_the_unfinished_rule_under_neural__ec9e0dcc", "Apply or reset the unfinished rule under Neural Rendering > Object Rules (Experimental) before reviewing settings changes."));
 ImGui::BeginDisabled(unfinishedRule||model.installer.busy);if(ui::Button(Neurotic::UiLiteral("desktop.gamesettingsview.save_changes_c134f04c", "Save Changes")))model.PlanSettings();
 ImGui::SameLine();if(ui::Button(Neurotic::UiLiteral("desktop.gamesettingsview.restore_defaults_32425ec5", "Restore Defaults")))model.RestoreSettingsDefaults();ImGui::EndDisabled();
}
inline void RenderSavedGameCompatibility(HubModel& model){
 if(model.settings.empty()){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.select_a_compatible_game_executable_with_an_exis_0fa9f74f", "Select a compatible game executable with an existing OptiScaler.ini. Install NeuRotic first if this is a new game."));return;}
 KeybindCaptureState capture;
 Neurotic::Sleek::ContentCard startup("##SavedStartupOptions",Neurotic::UiLiteral("desktop.gamesettingsview.startup_options_for_this_game_b3d2ff6c", "Startup Options for This Game"),true);
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_keeps_neurotic_s_existing_defaults_and_f81ebf82", "Automatic keeps NeuRotic's existing defaults and game-specific compatibility rules. Changes apply on the next launch."));
 for(auto& field:model.settings)if(IsInstallationCompatibilitySetting(field.section,field.key))RenderSavedSetting(field,model,capture,{},true);
 RenderSavedSettingsReview(model);
}
// These lamps represent explicit saved requests, never live service health.
inline Neurotic::Sleek::PilotState SavedSettingsPilotState(const HubModel& model,int child){
 const char* section=child==4?"CharacterInspector":Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr");
 const char* key=child==1?"MultipassEnabled":child==3?"ExperimentalMode":"Enabled";
 const auto field=std::find_if(model.settings.begin(),model.settings.end(),[&](const auto& item){return item.section==section&&item.key==key;});
 return field!=model.settings.end()&&std::string_view(field->draft.data())==Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true")?Neurotic::Sleek::PilotState::On:Neurotic::Sleek::PilotState::Off;
}
inline void RenderSavedGameSettings(HubModel& model){
 static KeybindCaptureState capture;static int lastFrame=-2;static std::string selectedPage;
 if(lastFrame!=ImGui::GetFrameCount()-1)capture.ResetListening();lastFrame=ImGui::GetFrameCount();
 const auto scope=model.selected>=0&&model.selected<(int)model.games.size()?model.games[model.selected].id:std::string();capture.Scope(scope);
 if(model.settings.empty()){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.select_a_compatible_game_executable_with_an_exis_0fa9f74f", "Select a compatible game executable with an existing OptiScaler.ini. Install NeuRotic first if this is a new game."));return;}
 ImGui::TextDisabled(Neurotic::UiLiteral("desktop.gamesettingsview.saved_preferences_for_this_game_applied_on_the_n_98f0129d", "Saved preferences for this game · applied on the next launch"));ImGui::Spacing();
 if(ImGui::BeginTabBar("##SavedGamePages",ImGuiTabBarFlags_FittingPolicyScroll)){
  for(int page=0;page<7;++page)if(ImGui::BeginTabItem(SavedSettingsPages[page])){
   ImGui::PushID(page);
   if(ImGui::BeginTabBar("##SavedGameChildren",ImGuiTabBarFlags_FittingPolicyScroll)){
    const auto& children=SavedSettingsChildren(page);
    for(int ordinal=0;ordinal<(int)children.size();++ordinal){
     const int child=SavedSettingsVisualChild(page,ordinal);
     const bool hasSavedFields=std::any_of(model.settings.begin(),model.settings.end(),[&](const auto& field){auto location=SavedSettingsPlacement(field.section,field.key);return location.page==page&&location.child==child;});
     if(!hasSavedFields)continue;
     const bool pilot=page==2&&(child==0||child==1||child==3||child==4);
     std::string tabLabel=children[child];
     if(pilot)ImGui::SetNextItemWidth(ImGui::CalcTextSize(tabLabel.c_str()).x+ImGui::GetStyle().FramePadding.x*2+Neurotic::Sleek::PilotReserve());
     const bool open=ImGui::BeginTabItem(tabLabel.c_str());
     if(pilot)Neurotic::Sleek::TabPilotLight(SavedSettingsPilotState(model,child));
     if(open){
     const auto selection=std::to_string(page)+"/"+std::to_string(child);if(selectedPage!=selection){selectedPage=selection;capture.ResetListening();}
     ImGui::PushID(child);ImGui::Spacing();
     if(page==2&&child==1){
      ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.enable_affects_processing_only_both_editors_rema_baccef31", "Enable affects processing only. Both editors remain available while Multi Pass is off."));
     }
     std::vector<std::string> groups;
     for(auto& field:model.settings){auto location=SavedSettingsPlacement(field.section,field.key);if(location.page==page&&location.child==child&&std::find(groups.begin(),groups.end(),location.section)==groups.end())groups.emplace_back(location.section);}
     const bool nrMain=page==2&&child==0;
     const auto composition=[](const SettingsField& field){return field.section==Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")&&SavedKeyIn(field.key,{"Intensity","TransferStrength","ColourStrength","LocalStructure","LocalTone","SkinStructure","AutoMask"});};
     if(nrMain){
      Neurotic::Sleek::Scope cardPresentation(model.reducedMotion);
      Neurotic::Sleek::CardColumns columns("##SavedNrMainCards");
      {Neurotic::Sleek::ContentCard card("##SavedNrRendering",Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering"),true);
       for(auto& field:model.settings){const auto location=SavedSettingsPlacement(field.section,field.key);
        if(location.page==2&&location.child==0&&location.section==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")&&!composition(field)){RenderSavedSetting(field,model,capture,scope,false,true);Neurotic::Sleek::ControlDivider();}}}
      columns.Next();
      {Neurotic::Sleek::ContentCard card("##SavedNrComposition",Neurotic::UiLiteral("desktop.gamesettingsview.model_composition_d53053f6", "Model & composition"),true);
       for(auto& field:model.settings)if(composition(field)){RenderSavedSetting(field,model,capture,scope,false,true);Neurotic::Sleek::ControlDivider();}}
     }
     for(const auto& group:groups){
      if(group.starts_with(Neurotic::UiLiteral("desktop.gamesettingsview.pass_8f5c7317", "Pass "))||(nrMain&&(group==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")||group==Neurotic::UiLiteral("desktop.gamesettingsview.local_structure_and_skin_d7adf3b8", "Local structure and skin"))))continue;
      const bool main=group==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")||group=="Multipass";
      const bool initiallyOpen=main||page!=2||child!=0;
      const bool colorCard=nrMain&&group=="Colour";
      if(colorCard)Neurotic::Sleek::BeginContentCard("##SavedNrColor",Neurotic::UiLiteral("desktop.gamesettingsview.color_5b442d92", "Color"),true);
      if(colorCard||group==Neurotic::UiLiteral("desktop.gamesettingsview.object_rules_bb34e3c8", "Object Rules")||group==Neurotic::UiLiteral("desktop.gamesettingsview.animations_3cd42432", "Animations")||ImGui::CollapsingHeader(group.c_str(),initiallyOpen?ImGuiTreeNodeFlags_DefaultOpen:0)){
       if(group==Neurotic::UiLiteral("desktop.gamesettingsview.upscale_ratio_overrides_4aad47ec", "Upscale Ratio Overrides"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.ratios_normally_range_from_1_to_3_enable_extende_33866c29", "Ratios normally range from 1 to 3. Enable Extended Limits under Advanced > Settings for the 0.1 to 6 range."));
       std::vector<SettingsField*> controls;
       for(auto& field:model.settings){const auto location=SavedSettingsPlacement(field.section,field.key);if(location.page==page&&location.child==child&&group==location.section)controls.push_back(&field);}
       if(group=="Basic")std::stable_sort(controls.begin(),controls.end(),[](const auto* a,const auto* b){
        const auto order=[](std::string_view key){return key==Neurotic::UiLiteral("desktop.gamesettingsview.advanced_0f9eefbe", "Advanced")?0:key=="MaximumPasses"?1:key=="Resolution"?2:3;};
        return order(a->key)<order(b->key);
       });
       for(auto* field:controls)RenderSavedSetting(*field,model,capture,scope);
      }
      if(colorCard)Neurotic::Sleek::EndContentCard();
     }
     if(page==2&&child==1&&ImGui::CollapsingHeader(Neurotic::UiLiteral("desktop.gamesettingsview.advanced_0f9eefbe", "Advanced"),ImGuiTreeNodeFlags_DefaultOpen)&&ImGui::BeginTabBar("##SavedPassProfiles",ImGuiTabBarFlags_FittingPolicyScroll)){
      for(const auto& group:groups)if(group.starts_with(Neurotic::UiLiteral("desktop.gamesettingsview.pass_8f5c7317", "Pass "))&&ImGui::BeginTabItem(group.c_str())){
       ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.this_saved_profile_is_used_only_when_advanced_mu_b8c1ac32", "This saved profile is used only when Advanced Multipass includes this pass. Automatic values inherit the preceding pass when the game loads them."));
       for(auto& field:model.settings){const auto location=SavedSettingsPlacement(field.section,field.key);if(location.page==page&&location.child==child&&group==location.section)RenderSavedSetting(field,model,capture,scope);}
       ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
     }
     ImGui::PopID();ImGui::EndTabItem();
     }
    }
    ImGui::EndTabBar();
   }
   ImGui::PopID();ImGui::EndTabItem();
  }
  ImGui::EndTabBar();
 }
 // Never hide a future schema field because its presentation grouping is newer.
 for(auto& field:model.settings)if(SavedSettingsPlacement(field.section,field.key).page==-1){ImGui::SeparatorText(Neurotic::UiLiteral("desktop.gamesettingsview.additional_saved_settings_df0294d0", "Additional saved settings"));RenderSavedSetting(field,model,capture,scope);}
 RenderSavedSettingsReview(model);
}
}
