#include <menu/ItemHelp.h>
#include <menu/Localization.h>
#pragma once
#include "HubViewModel.h"
#include "NumericSettings.h"
#include "KeybindCapture.h"
#include "ObjectRuleHost.h"
#include "SleekWidgets.h"
#include "SavedControlLayout.h"
#include "imgui_internal.h"
#include "../../../OptiScaler/menu/MenuLayoutControls.h"
#include "../../../OptiScaler/menu/SleekContentCard.h"
#include "../../../OptiScaler/menu/SleekPilotLight.h"
#include <string_view>
#include <menu/BoundedPopup.h>

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
inline int SavedSettingsVisualChild(int page,int ordinal){constexpr int nrOrder[]={0,1,3,4,2};return page==2&&ordinal>=0&&ordinal<5?nrOrder[ordinal]:ordinal;}
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
  if(key=="Corner"||key=="Scale")return {0,3,Neurotic::UiLiteral("desktop.menu-layout.heading", "Menu layout")};
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
 if(section=="FrameGen")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.setup","Setup")};
 if(section=="FSRFG"||section=="FSRFGInputs")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.replacement-fg","Replacement FG")};
 if(section=="XeFG")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.replacement-fg","Replacement FG")};
 if(section=="OptiFG")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.replacement-fg","Replacement FG")};
 if(section=="NvngxFG")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.replacement-fg","Replacement FG")};
 if(section=="Framerate"||section=="fakenvapi")return {3,1,Neurotic::UiLiteral("desktop.saved-settings.pacing-latency","Pacing & latency")};
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
 const auto nativeField=[&](const char* key){return std::find_if(model.settings.begin(),model.settings.end(),[&](const auto& item){return item.section=="DlssNr"&&item.key==key;});};
 const auto route=nativeField("Route"),resolution=nativeField("NativeResolutionChoice");
 const bool native=route!=model.settings.end()&&std::string_view(route->draft.data())=="0";
 const bool nativeResolution=field.section=="DlssNr"&&field.key=="NativeResolutionChoice";
 // Present retains its existing controls. Native stage and raster selection are
 // one semantic action; individual raw placement is never offered beside it.
 if(nativeResolution&&!native)return;
 if(native&&field.section=="DlssNr"&&field.key=="RenderingMode")return;
 if(native&&field.section=="DlssNr"&&field.key=="WorkingScale"&&
    (resolution==model.settings.end()||std::string_view(resolution->draft.data())!="manual"))return;
 const auto selectNativeResolution=[&](const std::string& choice){
  const auto scale=nativeField("WorkingScale");
  if(scale!=model.settings.end()){
   if(std::string_view(field.draft.data())=="manual")field.nativeManualScale=scale->draft.data();
   const auto value=choice=="manual"?field.nativeManualScale:std::string("1");
   strncpy_s(scale->draft.data(),scale->draft.size(),value.c_str(),_TRUNCATE);
  }
  strncpy_s(field.draft.data(),field.draft.size(),choice.c_str(),_TRUNCATE);
 };
 // Key capture polls raw input, so cancel listening as well as disabling widgets.
 if(model.installer.busy)capture.ResetListening();
 ImGui::PushID((field.section+"/"+field.key).c_str());ImGui::BeginDisabled(!field.available||model.installer.busy);
 if(field.section==Neurotic::UiLiteral("desktop.gamesettingsview.menu_81f4ef5d", "Menu")&&field.key==Neurotic::UiLiteral("desktop.hubshell.language_c1ba9987", "Language")){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.language_is_shared_across_games_manage_it_in_app_fc7f57e2", "Language is shared across games. Manage it in App Settings. The old INI value is preserved."));ImGui::EndDisabled();ImGui::PopID();return;}
 if(field.type=="objectrules"){
  DrawObjectRuleHost(field,model.settings);
  if(field.ruleEditor&&field.ruleEditor->dirty)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.apply_or_reset_the_unfinished_rule_under_neural__ec9e0dcc", "Apply or reset the unfinished rule under Neural Rendering > Object Rules (Experimental) before reviewing settings changes."));
  if(field.profileDraft!=field.original&&ImGui::SmallButton(Neurotic::UiLiteral("desktop.gamesettingsview.undo_profile_changes_49dfe1bf", "Undo profile changes"))){field.profileDraft=field.original;field.ruleStore.reset();field.ruleEditor.reset();}
  ImGui::EndDisabled();
  if(!field.available)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.the_existing_profile_is_preserved_resolve_duplic_feed7f2c", "The existing profile is preserved. Resolve duplicate or invalid Object Rules entries in OptiScaler.ini before editing."));
  ImGui::PopID();return;
 }
 auto label=field.label.empty()?field.key:field.label;
 std::string labelId=field.labelId;
 const auto caption=[&](const char* id,const char* text){label=text;labelId=id;};
 if(field.section=="Menu"){
  if(field.key=="ShortcutKey")caption("desktop.saved-settings.toggle-menu",Neurotic::UiLiteral("desktop.saved-settings.toggle-menu","Toggle menu"));
  else if(field.key=="EscapeClosesMenu")caption("desktop.saved-settings.escape-closes",Neurotic::UiLiteral("desktop.saved-settings.escape-closes","Esc closes menu"));
  else if(field.key=="AllowGameMouse")caption("desktop.saved-settings.mouse",Neurotic::UiLiteral("desktop.saved-settings.mouse","Mouse"));
  else if(field.key=="AllowGameKeyboard")caption("desktop.saved-settings.keyboard",Neurotic::UiLiteral("desktop.saved-settings.keyboard","Keyboard"));
  else if(field.key=="AllowGameController")caption("desktop.saved-settings.controller",Neurotic::UiLiteral("desktop.saved-settings.controller","Controller"));
 }
 Neurotic::ScopedUiLiteral labelBinding(labelId,label.c_str());Neurotic::ScopedUiLiteral descriptionBinding(field.descriptionId,field.description.c_str());
 const auto experimentalHelp=[&]{
  if(field.section!=Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")||!SavedKeyIn(field.key,{"ExperimentalMode","PreSrSoftReset","PreparedDepth"})||field.description.empty())return;
  ImGui::SameLine();ImGui::TextDisabled("(?)");
  if(Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::Translate(field.description.c_str()).c_str());
 };
 const auto hasChoice=[&](std::string_view value){return std::find(field.choices.begin(),field.choices.end(),value)!=field.choices.end();};
 const bool boolean=hasChoice("true")&&hasChoice("false");
 const bool triState=boolean&&hasChoice("auto")&&field.choices.size()==3;
 const bool knownBoolean=std::string_view(field.draft.data())=="true"||std::string_view(field.draft.data())=="false";
 const bool mfgBoolean=field.section=="DLSSG"&&SavedKeyIn(field.key,{"NativeMfgExperimental","ExperimentalUnlockRTX30","ExperimentalUnlockRTX20"});
 const bool directDmfg=field.section=="DLSSG"&&field.key=="FramerateTargetDMFG";
 const bool dropdown=!field.choices.empty()&&!mfgBoolean&&!(boolean&&field.choices.size()==2&&knownBoolean&&!compatibility);
 const bool reviewStrip=dropdown||mfgBoolean||directDmfg;
 const bool resetInStrip=reviewStrip;
 // Compatibility/tri-state Reset retains its existing explicit AUTO action.
 // Other dropdowns use verified package defaults, independently of Undo.
 const bool automaticReset=(compatibility||triState)&&hasChoice("auto");
 const bool resetKnown=mfgBoolean||automaticReset||field.resetKnown;
 const std::string resetValue=mfgBoolean?std::string("false"):automaticReset?std::string("auto"):field.resetValue;
 const bool dirty=field.original!=field.draft.data()||(nativeResolution&&field.nativeManualScale!=field.originalNativeManualScale);
 const auto resetLabel=Neurotic::UiLiteral("desktop.anythingview.reset_51fe8741", "Reset");
 const auto undoLabel=Neurotic::UiLiteral("desktop.provider.a8283ade3185", "Undo");
 const float rowWidth=ImGui::GetContentRegionAvail().x;
 const float gap=ImGui::GetStyle().ItemInnerSpacing.x;
 const auto resetAction=MeasureSavedReviewAction(resetLabel,(std::max)(1.f,rowWidth*.32f));
 const auto undoAction=MeasureSavedReviewAction(undoLabel,(std::max)(1.f,rowWidth*.26f));
 const float actionWidth=reviewStrip?undoAction.width+gap+(resetInStrip?resetAction.width+gap:0.f):0.f;
 const auto undo=[&]{
  capture.ResetListening();
  if(nativeResolution){field.nativeManualScale=field.originalNativeManualScale;const auto scale=nativeField("WorkingScale");if(scale!=model.settings.end())strncpy_s(scale->draft.data(),scale->draft.size(),scale->original.c_str(),_TRUNCATE);}
  strncpy_s(field.draft.data(),field.draft.size(),field.original.c_str(),_TRUNCATE);
 };
 bool mfgHovered=false;
 if(mfgBoolean){
  const float rowStart=ImGui::GetCursorPosX();
  const float labelWidth=(std::min)(ImGui::GetFontSize()*14.f,rowWidth*.30f);
  const bool row=rowWidth>=labelWidth+gap+ImGui::GetFrameHeight()+actionWidth;
  ImGui::AlignTextToFramePadding();if(row)ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+labelWidth);
  ImGui::TextWrapped("%s",Neurotic::Translate(label.c_str()).c_str());
  mfgHovered=Neurotic::ItemHelpHovered();
  if(row){ImGui::PopTextWrapPos();ImGui::SameLine(rowStart+labelWidth+gap);}
  bool enabled=std::string_view(field.draft.data())=="true";
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,2.f);
  ImGui::PushItemFlag(ImGuiItemFlags_MixedValue,!knownBoolean);
  if(ImGui::Checkbox("##Value",&enabled))strcpy_s(field.draft.data(),field.draft.size(),enabled?"true":"false");
  mfgHovered=Neurotic::ItemHelpHovered()||mfgHovered;
  ImGui::PopItemFlag();ImGui::PopStyleVar();
 }else if(boolean&&field.choices.size()==2&&knownBoolean&&!compatibility){
  bool enabled=std::string_view(field.draft.data())=="true";
  ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,2.f);
  if(Neurotic::WrappedPopupCheckbox(label.c_str(),&enabled))strcpy_s(field.draft.data(),field.draft.size(),enabled?"true":"false");
  ImGui::PopStyleVar();experimentalHelp();
 }else{
  const float rowStart=ImGui::GetCursorPosX();
  const float labelWidth=(std::min)(ImGui::GetFontSize()*14.f,rowWidth*.30f);
  const bool row=rowWidth>=labelWidth+gap+ImGui::GetFontSize()*8.f+actionWidth;
  ImGui::AlignTextToFramePadding();
  if(row)ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+labelWidth);
  ImGui::TextWrapped("%s",Neurotic::Translate(label.c_str()).c_str());
  if(row)ImGui::PopTextWrapPos();experimentalHelp();
  if(row)ImGui::SameLine(rowStart+labelWidth+gap);
  const float controlWidth=(std::max)(1.f,ImGui::GetContentRegionAvail().x-actionWidth);
  ImGui::SetNextItemWidth(dropdown?controlWidth:directDmfg?(std::min)(controlWidth,ImGui::GetFontSize()*20.f):((card||nativeResolution||row)?ImGui::GetContentRegionAvail().x:std::min(ImGui::GetFontSize()*20,ImGui::GetContentRegionAvail().x)));
  if(field.type=="keycode")RenderKeybindCapture(field,model.settings,capture,scope);
  else if(!field.choices.empty()){
   auto friendly=[&](const std::string& value){
    if(triState){
     if(value=="auto")return std::string(Neurotic::UiLiteral("desktop.settings.tristate.auto","AUTO"));
     if(value=="true")return std::string(Neurotic::UiLiteral("desktop.settings.tristate.true","TRUE"));
     if(value=="false")return std::string(Neurotic::UiLiteral("desktop.settings.tristate.false","FALSE"));
     return value;
    }
    const auto found=field.valueLabels.find(value);return found!=field.valueLabels.end()?found->second:value=="auto"?std::string(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_1573651c","Automatic")):value=="true"?std::string("Enabled"):value=="false"?std::string("Disabled"):value;
   };
   auto choiceId=[&](const std::string& value){
    if(triState&&(value=="auto"||value=="true"||value=="false"))return std::string("desktop.settings.tristate.")+value;
    const auto found=field.valueLabelIds.find(value);return found!=field.valueLabelIds.end()?found->second:std::string{};
   };
   auto preview=friendly(field.draft.data());Neurotic::ScopedUiLiteral previewBinding(choiceId(field.draft.data()),preview.c_str());
   if(field.section=="Menu"&&field.key=="Corner"&&field.choices==std::vector<std::string>{"0","1","2","3"}&&std::string_view(field.draft.data()).size()==1&&field.draft[0]>='0'&&field.draft[0]<='3'){
    std::array<std::string,4> captions;std::array<const char*,4> names;std::vector<std::unique_ptr<Neurotic::ScopedUiLiteral>> bindings;
    for(unsigned i=0;i<4;++i){const auto value=std::to_string(i);captions[i]=friendly(value);names[i]=captions[i].c_str();bindings.push_back(std::make_unique<Neurotic::ScopedUiLiteral>(choiceId(value),names[i]));}
    unsigned corner=unsigned(field.draft[0]-'0');if(Neurotic::MenuLayout::CornerPicker("##Value",names.data(),corner,controlWidth))strcpy_s(field.draft.data(),field.draft.size(),std::to_string(corner).c_str());
   }else if(nativeResolution){
    // Reuse the MenuLayout corner selector's wrapped preview pattern. Reserve
    // the longest choice's height so changing a value never shifts controls.
    const auto padding=ImGui::GetStyle().FramePadding;
    const float width=controlWidth;
    const float arrow=ImGui::GetFrameHeight();
    const float textWidth=(std::max)(1.f,width-arrow-2*padding.x);
    std::vector<std::string> captions;
    float height=ImGui::GetFontSize();
    for(const auto& choice:field.choices){
     auto text=friendly(choice);Neurotic::ScopedUiLiteral binding(choiceId(choice),text.c_str());
     captions.push_back(Neurotic::Translate(text.c_str()));
     height=(std::max)(height,ImGui::CalcTextSize(captions.back().c_str(),nullptr,false,textWidth).y);
    }
    const auto translatedPreview=Neurotic::Translate(preview.c_str());
    height=(std::max)(height,ImGui::CalcTextSize(translatedPreview.c_str(),nullptr,false,textWidth).y);
    const bool wrap=height>ImGui::GetFontSize()+.1f;
    if(wrap){
     ImGui::SetNextWindowSizeConstraints({width,0},{width,FLT_MAX});
     ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2{padding.x,padding.y+(height-ImGui::GetFontSize())*.5f});
    }
    const bool open=ImGui::BeginCombo("##Value",wrap?nullptr:preview.c_str(),
     wrap?ImGuiComboFlags_CustomPreview|ImGuiComboFlags_NoArrowButton:ImGuiComboFlags_None);
    if(wrap)ImGui::PopStyleVar();
    if(open){
     for(size_t i=0;i<field.choices.size();++i){
      const auto& choice=field.choices[i];auto text=friendly(choice);
      Neurotic::ScopedUiLiteral choiceBinding(choiceId(choice),text.c_str());ImGui::PushID(choice.c_str());
      bool selected=false;
      if(wrap){
       const float popupItemWidth=(std::max)(1.f,(std::min)(ImGui::GetContentRegionAvail().x,ImGui::GetCurrentWindow()->ClipRect.Max.x-ImGui::GetCursorScreenPos().x));
       const float popupTextWidth=(std::max)(1.f,popupItemWidth-2*padding.x);
       const float textHeight=ImGui::CalcTextSize(captions[i].c_str(),nullptr,false,popupTextWidth).y;
       // Keep the real label/ID and native Selectable behavior, drawing only
       // its wrapped caption instead of the clipped single-line text.
       ImGui::PushStyleColor(ImGuiCol_Text,ImVec4{0,0,0,0});
       selected=ImGui::Selectable(text.c_str(),choice==field.draft.data(),ImGuiSelectableFlags_NoPadWithHalfSpacing,{popupItemWidth,textHeight+2*padding.y});
       ImGui::PopStyleColor();
       Neurotic::MenuLayout::WrappedCaption(captions[i],ImGui::GetItemRectMin(),ImGui::GetItemRectMax(),padding.x);
      }else selected=ImGui::Selectable(text.c_str(),choice==field.draft.data());
      if(selected)selectNativeResolution(choice);
      ImGui::PopID();
     }
     ImGui::EndCombo();
    }
    const auto comboItem=GImGui->LastItemData;
    if(wrap&&ImGui::BeginComboPreview()){
     const auto bounds=GImGui->ComboPreviewData.PreviewRect;
     ImGui::PushTextWrapPos(ImGui::GetCursorPosX()+textWidth);
     ImGui::TextUnformatted(translatedPreview.c_str());ImGui::PopTextWrapPos();
     ImGui::RenderArrow(ImGui::GetWindowDrawList(),{bounds.Max.x-arrow*.75f,bounds.GetCenter().y-ImGui::GetFontSize()*.5f},ImGui::GetColorU32(ImGuiCol_Text),ImGuiDir_Down);
     ImGui::EndComboPreview();
     GImGui->LastItemData=comboItem;
    }
   }else{
    float widest=0;
    for(const auto& choice:field.choices){auto text=friendly(choice);Neurotic::ScopedUiLiteral binding(choiceId(choice),text.c_str());widest=(std::max)(widest,ImGui::CalcTextSize(Neurotic::Translate(text.c_str()).c_str()).x);}
    ImGui::SetNextItemWidth(MeasureSavedChoiceWidth(controlWidth,widest));
    if(ImGui::BeginCombo("##Value",preview.c_str())){for(auto& choice:field.choices){auto text=friendly(choice);Neurotic::ScopedUiLiteral choiceBinding(choiceId(choice),text.c_str());ImGui::PushID(choice.c_str());if(ImGui::Selectable(text.c_str(),choice==field.draft.data())){strncpy_s(field.draft.data(),field.draft.size(),choice.c_str(),_TRUNCATE);}ImGui::PopID();}ImGui::EndCombo();}
   }
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
 if(reviewStrip){
#ifdef NEUROTIC_SAVED_CONTROL_VALUE_PROBE
  NEUROTIC_SAVED_CONTROL_VALUE_PROBE(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
#endif
  if(resetInStrip){
   ImGui::SameLine(0,gap);ImGui::BeginDisabled(!resetKnown);
   if(SavedReviewButton(resetLabel,resetAction)){
    capture.ResetListening();
    if(nativeResolution){field.nativeManualScale=field.resetNativeManualScale;const auto scale=nativeField("WorkingScale");if(scale!=model.settings.end()){const auto value=resetValue=="manual"?field.nativeManualScale:std::string("1");strncpy_s(scale->draft.data(),scale->draft.size(),value.c_str(),_TRUNCATE);}}
    strncpy_s(field.draft.data(),field.draft.size(),resetValue.c_str(),_TRUNCATE);
   }
   ImGui::EndDisabled();
   if(!resetKnown&&Neurotic::ItemHelpHovered())ImGui::SetTooltip("%s",Neurotic::Translate(Neurotic::UiLiteral("desktop.saved-control.default-unavailable","Default unavailable. Refresh installation details or repair the local package.")).c_str());
  }
  ImGui::SameLine(0,gap);
  // Reserve Undo before drawing the value, including while the draft is clean.
  if(dirty){if(SavedReviewButton(undoLabel,undoAction))undo();}else ImGui::Dummy({undoAction.width,undoAction.height});
 }else{
  if(compatibility||triState){if(ImGui::SmallButton(resetLabel)){capture.ResetListening();strcpy_s(field.draft.data(),field.draft.size(),Neurotic::UiLiteral("desktop.gamesettingsview.auto_d1f397f2", "auto"));}}
  if(dirty&&ImGui::SmallButton(undoLabel))undo();
 }
 if((mfgBoolean&&field.original!="true"&&field.original!="false")||(dropdown&&!nativeResolution&&!hasChoice(field.original)))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.saved-control.unknown-saved-value","Saved value: %s. Choose a value to replace it."),field.original.c_str());
 if(mfgHovered&&!field.description.empty())ImGui::SetTooltip("%s",Neurotic::Translate(field.description.c_str()).c_str());
 if(nativeResolution&&std::string_view(field.draft.data()).starts_with("unknown:"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.settings.native_resolution_unknown","Unknown saved resolution. Select a known option to replace it."));
 ImGui::EndDisabled();if(!field.available)ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.this_entry_is_preserved_duplicate_keys_sections__376ab0f6", "This entry is preserved: duplicate keys/sections or a value that cannot be safely edited here. Check OptiScaler.ini before editing this setting."));
 ImGui::PopID();
}
// Keep review actions outside the field scroll region. Reserve the rule warning
// even when clear, so editing a rule does not move the actions or field viewport.
inline const char* SavedSettingsRuleWarning(){return Neurotic::UiLiteral("desktop.saved-settings.unfinished-rule","Apply or reset the unfinished Object Rule before saving.");}
inline float SavedSettingsReviewHeight(){
 const auto& style=ImGui::GetStyle();
 const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
 const auto warning=Neurotic::Translate(SavedSettingsRuleWarning());
 const auto save=MeasureSavedReviewAction(Neurotic::UiLiteral("desktop.gamesettingsview.save_changes_c134f04c", "Save Changes"),width);
 const auto restore=MeasureSavedReviewAction(Neurotic::UiLiteral("desktop.gamesettingsview.restore_defaults_32425ec5", "Restore Defaults"),width);
 const bool row=save.width+restore.width+style.ItemSpacing.x<=width;
 const float actions=row?(std::max)(save.height,restore.height):save.height+restore.height+style.ItemSpacing.y;
 return ImGui::CalcTextSize(warning.c_str(),nullptr,false,width).y+actions+style.ItemSpacing.y*2;
}
// Common budget for every outer destination: headers cannot move with the tab,
// current game, dirty warning or async result. Only pane/style/locale affect it.
inline float SavedSettingsWorkspaceHeight(){
 const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
 const auto game=Neurotic::Translate(Neurotic::UiLiteral("desktop.gamesettingsview.saved_preferences_for_this_game_applied_on_the_n_98f0129d", "Saved preferences for this game · applied on the next launch"));
 const auto ini=Neurotic::Translate(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_keeps_neurotic_s_existing_defaults_and_f81ebf82", "Automatic keeps NeuRotic's existing defaults and game-specific compatibility rules. Changes apply on the next launch."));
 const float notice=(std::max)(ImGui::CalcTextSize(game.c_str(),nullptr,false,width).y,ImGui::CalcTextSize(ini.c_str(),nullptr,false,width).y);
 // Two navigation rows plus at least three ordinary control rows.
 return notice+ImGui::GetStyle().ItemSpacing.y*2+ImGui::GetFrameHeightWithSpacing()*5+SavedSettingsReviewHeight();
}
inline void RenderSavedSettingsReview(HubModel& model,ImGuiID namespaceId=0){
 if(namespaceId)ImGui::PushOverrideID(namespaceId);
 ImGui::Separator();
 const bool unfinishedRule=std::any_of(model.settings.begin(),model.settings.end(),[](const auto& field){return field.ruleEditor&&field.ruleEditor->dirty;});
 const auto warning=Neurotic::Translate(SavedSettingsRuleWarning());
 const float width=(std::max)(1.f,ImGui::GetContentRegionAvail().x);
 const float warningHeight=ImGui::CalcTextSize(warning.c_str(),nullptr,false,width).y;
 if(unfinishedRule)ImGui::TextWrapped("%s",warning.c_str());else ImGui::Dummy({0,warningHeight});
 const auto* save=Neurotic::UiLiteral("desktop.gamesettingsview.save_changes_c134f04c", "Save Changes");
 const auto* restore=Neurotic::UiLiteral("desktop.gamesettingsview.restore_defaults_32425ec5", "Restore Defaults");
 const auto saveAction=MeasureSavedReviewAction(save,width),restoreAction=MeasureSavedReviewAction(restore,width);
 ImGui::BeginDisabled(unfinishedRule||model.installer.busy||model.InstallQueueBusy());
 if(SavedReviewButton(save,saveAction))model.PlanSettings();
 if(saveAction.width+restoreAction.width+ImGui::GetStyle().ItemSpacing.x<=width)ImGui::SameLine();
 if(SavedReviewButton(restore,restoreAction))model.RestoreSettingsDefaults();ImGui::EndDisabled();
 if(namespaceId)ImGui::PopID();
}
// Keep native child IDs in both layouts, and transfer the real editing anchor
// between the fixed-height field viewport and the compact common workspace.
// State belongs to this game's native parent/child namespace, not a model cache.
inline ImGuiID SavedFieldsLayoutKey(ImGuiID child,const char* name){return ImHashStr(name,0,child);}
inline ImGuiID SavedFieldsFinishKey(ImGuiWindow* parent,const char* name){return ImHashStr(name,0,parent->ID);}
inline bool BeginSavedSettingsFields(const char* id,float height,bool naturalFields){
 auto* parent=ImGui::GetCurrentWindow();auto& state=parent->StateStorage;const auto child=ImGui::GetID(id);
 const auto key=[&](const char* name){return SavedFieldsLayoutKey(child,name);};
 const bool known=state.GetBool(key("known"));const bool wasNatural=state.GetBool(key("natural"));
 if(known&&wasNatural!=naturalFields){
  // A saturated compact view cannot display the entire original normal anchor.
  // Keep that authentic anchor until native navigation supersedes it.
  if(!state.GetBool(key("retainAnchor")))state.SetFloat(key("anchor"),state.GetFloat(key(wasNatural?"naturalOffset":"normalOffset")));
  if(!wasNatural)state.SetBool(key("retainAnchor"),true);
  state.SetBool(key("pending"),true);
 }
 state.SetBool(key("known"),true);state.SetBool(key("natural"),naturalFields);
 const ImGuiChildFlags flags=ImGuiChildFlags_AlwaysUseWindowPadding|(naturalFields?(ImGuiChildFlags_AutoResizeY|ImGuiChildFlags_AlwaysAutoResize):ImGuiChildFlags_None);
 const ImGuiWindowFlags windowFlags=naturalFields?(ImGuiWindowFlags_NoScrollbar|ImGuiWindowFlags_NoScrollWithMouse):ImGuiWindowFlags_None;
 return ImGui::BeginChild(id,{0,naturalFields?0.f:height},flags,windowFlags);
}
inline void FinishSavedSettingsFieldsLayout(){
 auto* parent=ImGui::GetCurrentWindow();auto& state=parent->StateStorage;auto& context=*ImGui::GetCurrentContext();
 if(state.GetInt(SavedFieldsFinishKey(parent,"saved-fields.finish-frame"),-1)!=context.FrameCount)return;
 auto* fields=ImGui::FindWindowByID(static_cast<ImGuiID>(state.GetInt(SavedFieldsFinishKey(parent,"saved-fields.finish-child"))));
 const bool naturalFields=state.GetBool(SavedFieldsFinishKey(parent,"saved-fields.finish-natural"));
 state.SetInt(SavedFieldsFinishKey(parent,"saved-fields.finish-frame"),-1);
 if(!fields||!fields->Active||fields->ParentWindow!=parent)return;
 const auto key=[&](const char* name){return SavedFieldsLayoutKey(fields->ChildId,name);};
 // Deliberate native navigation replaces the saved transition anchor.
 const auto inside=[&](ImGuiWindow* window){for(;window;window=window->ParentWindow)if(window==parent)return true;return false;};
 const bool nativeKeys=inside(context.NavWindow)&&(ImGui::IsKeyPressed(ImGuiKey_Tab)||ImGui::IsKeyPressed(ImGuiKey_UpArrow)||ImGui::IsKeyPressed(ImGuiKey_DownArrow)||ImGui::IsKeyPressed(ImGuiKey_PageUp)||ImGui::IsKeyPressed(ImGuiKey_PageDown)||ImGui::IsKeyPressed(ImGuiKey_Home)||ImGui::IsKeyPressed(ImGuiKey_End));
 const bool nativeMouse=inside(context.HoveredWindow)&&ImGui::IsMouseClicked(ImGuiMouseButton_Left);
 const bool nativeWheel=(context.IO.MouseWheel!=0.f||context.IO.MouseWheelH!=0.f)&&inside(context.WheelingWindow);
 if(nativeKeys||nativeMouse||nativeWheel){state.SetBool(key("pending"),false);state.SetBool(key("retainAnchor"),false);}
 // Parent content/range is calculated at End while auto-height uses the prior
 // frame. Require two matching observed geometries before accepting saturation.
 const bool geometrySettled=std::abs(state.GetFloat(key("range"),-1.f)-parent->ScrollMax.y)<=1.f&&std::abs(state.GetFloat(key("fieldsHeight"),-1.f)-fields->Size.y)<=1.f&&std::abs(state.GetFloat(key("viewportHeight"),-1.f)-parent->InnerRect.GetHeight())<=1.f;
 state.SetFloat(key("range"),parent->ScrollMax.y);state.SetFloat(key("fieldsHeight"),fields->Size.y);state.SetFloat(key("viewportHeight"),parent->InnerRect.GetHeight());
 if(state.GetBool(key("pending"))){
  const float anchor=(std::max)(0.f,state.GetFloat(key("anchor")));
  const bool measured=!naturalFields||fields->Size.y+1.f>=fields->ContentSize.y+fields->WindowPadding.y*2;
  bool focused=context.NavId!=0&&context.NavIdIsAlive;
  bool descendant=false;for(auto* window=context.NavWindow;window;window=window->ParentWindow)descendant=descendant||window==fields;
  // The parent review and tabs have now submitted their real native items.
  // Visible keyboard focus there takes precedence over an older field anchor.
  focused=focused&&(descendant||(context.NavWindow==parent&&context.NavCursorVisible));
  if(!naturalFields)ImGui::SetScrollY(parent,0.f);
  if(focused){
   state.SetBool(key("retainAnchor"),false);
   // Use the real, live native item's rectangle. This requests scrolling only;
   // it never assigns a focus ID, tab selection, prior scroll or input state.
   const auto rect=ImGui::WindowRectRelToAbs(context.NavWindow,context.NavWindow->NavRectRel[context.NavLayer]);
   if(!context.NavWindow->InnerClipRect.Contains(rect))ImGui::ScrollToRectEx(context.NavWindow,rect,ImGuiScrollFlags_KeepVisibleEdgeY|(context.NavWindow==parent?ImGuiScrollFlags_NoScrollParent:0));
   else if(measured&&geometrySettled&&(naturalFields||parent->Scroll.y<=1.f))state.SetBool(key("pending"),false);
  }else if(naturalFields){
   if(anchor<=0.f)state.SetBool(key("pending"),false); // Keep a header view at the header.
   else{
    const float contentTop=fields->Pos.y-parent->Pos.y+parent->Scroll.y;
    const float wanted=contentTop+anchor;ImGui::SetScrollY(parent,wanted);
    // Auto-size and parent scroll bounds settle on successive native frames.
    // Do not consume the anchor while the old zero range would clamp it away.
    if(measured&&geometrySettled&&std::abs(parent->Scroll.y-(std::min)(wanted,parent->ScrollMax.y))<=1.f)state.SetBool(key("pending"),false);
   }
  }else{
   ImGui::SetScrollY(fields,anchor);
   if(parent->Scroll.y<=1.f&&std::abs(fields->Scroll.y-(std::min)(anchor,fields->ScrollMax.y))<=1.f){state.SetBool(key("pending"),false);state.SetBool(key("retainAnchor"),false);}
  }
 }
 if(naturalFields)state.SetFloat(key("naturalOffset"),(std::max)(0.f,parent->Pos.y-fields->Pos.y));
 else state.SetFloat(key("normalOffset"),fields->Scroll.y);
}
inline void EndSavedSettingsFields(bool naturalFields,bool deferLayout=false){
 auto* fields=ImGui::GetCurrentWindow();auto* parent=fields->ParentWindow;auto& state=parent->StateStorage;
 state.SetInt(SavedFieldsFinishKey(parent,"saved-fields.finish-frame"),ImGui::GetFrameCount());
 state.SetInt(SavedFieldsFinishKey(parent,"saved-fields.finish-child"),static_cast<int>(fields->ID));
 state.SetBool(SavedFieldsFinishKey(parent,"saved-fields.finish-natural"),naturalFields);
 ImGui::EndChild();if(!deferLayout)FinishSavedSettingsFieldsLayout();
}
inline void RenderSavedGameCompatibility(HubModel& model,bool reviewDocked=false,bool naturalFields=false,float reservedReviewHeight=0,bool deferLayout=false){
 if(model.settings.empty()){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.select_a_compatible_game_executable_with_an_exis_0fa9f74f", "Select a compatible game executable with an existing OptiScaler.ini. Install NeuRotic first if this is a new game."));return;}
 KeybindCaptureState capture;
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.automatic_keeps_neurotic_s_existing_defaults_and_f81ebf82", "Automatic keeps NeuRotic's existing defaults and game-specific compatibility rules. Changes apply on the next launch."));
 const float bodyHeight=(std::max)(1.f,ImGui::GetContentRegionAvail().y-(reviewDocked?reservedReviewHeight:SavedSettingsReviewHeight()));
 if(BeginSavedSettingsFields("##SavedCompatibilityFields",bodyHeight,naturalFields)){
  const char* sections[]={"Plugins","Spoofing","Hooks","Hotfix","OptiFG"};
  const char* headings[]={
   Neurotic::UiLiteral("desktop.saved-settings.compatibility","Compatibility"),
   Neurotic::UiLiteral("desktop.saved-settings.gpu-identity","GPU identity"),
   Neurotic::UiLiteral("desktop.saved-settings.startup-hooks","Startup hooks"),
   Neurotic::UiLiteral("desktop.saved-settings.state-restoration","State restoration"),
   Neurotic::UiLiteral("desktop.saved-settings.fg-hud","FG HUD options")};
  for(int i=0;i<5;++i){
   ImGui::PushID(sections[i]);ImGui::SeparatorText(headings[i]);
   for(auto& field:model.settings)if(field.section==sections[i]&&IsInstallationCompatibilitySetting(field.section,field.key)){RenderSavedSetting(field,model,capture,{},true);Neurotic::Sleek::ControlDivider();}
   ImGui::PopID();
  }
 }
 EndSavedSettingsFields(naturalFields,deferLayout);if(!reviewDocked)RenderSavedSettingsReview(model);
}
// These lamps represent explicit saved requests, never live service health.
inline Neurotic::Sleek::PilotState SavedSettingsPilotState(const HubModel& model,int child){
 const char* section=child==4?"CharacterInspector":Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr");
 const char* key=child==1?"MultipassEnabled":child==3?"ExperimentalMode":"Enabled";
 const auto field=std::find_if(model.settings.begin(),model.settings.end(),[&](const auto& item){return item.section==section&&item.key==key;});
 return field!=model.settings.end()&&std::string_view(field->draft.data())==Neurotic::UiLiteral("desktop.gamesettingsview.true_3cd12a50", "true")?Neurotic::Sleek::PilotState::On:Neurotic::Sleek::PilotState::Off;
}
inline void RenderSavedGameSettings(HubModel& model,bool reviewDocked=false,bool naturalFields=false,float reservedReviewHeight=0,bool deferLayout=false){
 static KeybindCaptureState capture;static int lastFrame=-2;static std::string selectedPage;
 if(lastFrame!=ImGui::GetFrameCount()-1)capture.ResetListening();lastFrame=ImGui::GetFrameCount();
 const auto scope=model.selected>=0&&model.selected<(int)model.games.size()?model.games[model.selected].id:std::string();capture.Scope(scope);
 if(model.settings.empty()){ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.select_a_compatible_game_executable_with_an_exis_0fa9f74f", "Select a compatible game executable with an existing OptiScaler.ini. Install NeuRotic first if this is a new game."));return;}
 ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.saved_preferences_for_this_game_applied_on_the_n_98f0129d", "Saved preferences for this game · applied on the next launch"));ImGui::Spacing();
 const float bodyHeight=(std::max)(1.f,ImGui::GetContentRegionAvail().y-(reviewDocked?reservedReviewHeight:SavedSettingsReviewHeight()));
 if(BeginSavedSettingsFields("##SavedGameFields",bodyHeight,naturalFields)){
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
     if(page==0&&child==2)ImGui::SeparatorText(Neurotic::UiLiteral("desktop.saved-settings.allow-game-input","Allow game input while menu is open"));
 std::vector<std::string> groups;
     for(auto& field:model.settings){auto location=SavedSettingsPlacement(field.section,field.key);if(location.page==page&&location.child==child&&std::find(groups.begin(),groups.end(),location.section)==groups.end())groups.emplace_back(location.section);}
     const bool nrMain=page==2&&child==0;
     const auto composition=[](const SettingsField& field){return field.section==Neurotic::UiLiteral("desktop.objectrulehost.dlssnr_e4e7ac7d", "DlssNr")&&SavedKeyIn(field.key,{"Intensity","TransferStrength","ColourStrength","LocalStructure","LocalTone","SkinStructure","AutoMask"});};
     if(nrMain){
      Neurotic::Sleek::Scope cardPresentation(model.reducedMotion);
      const bool twoColumns=ImGui::GetContentRegionAvail().x>=ImGui::GetFontSize()*60.f&&ImGui::BeginTable("##SavedNrMainCards",2,ImGuiTableFlags_SizingStretchSame|ImGuiTableFlags_NoPadOuterX);
 if(twoColumns)ImGui::TableNextColumn();
      {Neurotic::Sleek::ContentCard card("##SavedNrRendering",Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering"),true);
       for(auto& field:model.settings){const auto location=SavedSettingsPlacement(field.section,field.key);
        if(location.page==2&&location.child==0&&location.section==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")&&!composition(field)){RenderSavedSetting(field,model,capture,scope,false,true);Neurotic::Sleek::ControlDivider();}}}
      if(twoColumns)ImGui::TableNextColumn();else ImGui::Spacing();
      {Neurotic::Sleek::ContentCard card("##SavedNrComposition",Neurotic::UiLiteral("desktop.gamesettingsview.model_composition_d53053f6", "Model & composition"),true);
       for(auto& field:model.settings)if(composition(field)){RenderSavedSetting(field,model,capture,scope,false,true);Neurotic::Sleek::ControlDivider();}}
 if(twoColumns)ImGui::EndTable();
     }
     for(const auto& group:groups){
      if(group.starts_with(Neurotic::UiLiteral("desktop.gamesettingsview.pass_8f5c7317", "Pass "))||(nrMain&&(group==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")||group==Neurotic::UiLiteral("desktop.gamesettingsview.local_structure_and_skin_d7adf3b8", "Local structure and skin"))))continue;
      const bool main=group==Neurotic::UiLiteral("desktop.gamesettingsview.rendering_2c41105c", "Rendering")||group=="Multipass";
      const bool initiallyOpen=main||page!=2||child!=0;
      const bool colorCard=nrMain&&group=="Colour";
      const bool menuLayout=group==Neurotic::UiLiteral("desktop.menu-layout.heading","Menu layout");
      if(colorCard)Neurotic::Sleek::BeginContentCard("##SavedNrColor",Neurotic::UiLiteral("desktop.gamesettingsview.color_5b442d92", "Color"),true);
      if(colorCard||group==Neurotic::UiLiteral("desktop.gamesettingsview.object_rules_bb34e3c8", "Object Rules")||group==Neurotic::UiLiteral("desktop.gamesettingsview.animations_3cd42432", "Animations")||(menuLayout?Neurotic::MenuLayout::WrappedHeader(Neurotic::UiLiteral("desktop.menu-layout.heading","Menu layout"),initiallyOpen?ImGuiTreeNodeFlags_DefaultOpen:0):ImGui::CollapsingHeader(group.c_str(),initiallyOpen?ImGuiTreeNodeFlags_DefaultOpen:0))){
       if(group==Neurotic::UiLiteral("desktop.gamesettingsview.upscale_ratio_overrides_4aad47ec", "Upscale Ratio Overrides"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.gamesettingsview.ratios_normally_range_from_1_to_3_enable_extende_33866c29", "Ratios normally range from 1 to 3. Enable Extended Limits under Advanced > Settings for the 0.1 to 6 range."));
       if(group==Neurotic::UiLiteral("desktop.gamesettingsview.native_mfg_600c59ca", "Native MFG"))ImGui::TextWrapped(Neurotic::UiLiteral("desktop.settings.mfg-restart","Experimental. Save, then restart the game. Enable FG in the game."));
       std::vector<SettingsField*> controls;
       for(auto& field:model.settings){const auto location=SavedSettingsPlacement(field.section,field.key);if(location.page==page&&location.child==child&&group==location.section)controls.push_back(&field);}
       if(group=="Basic")std::stable_sort(controls.begin(),controls.end(),[](const auto* a,const auto* b){
        const auto order=[](std::string_view key){return key==Neurotic::UiLiteral("desktop.gamesettingsview.advanced_0f9eefbe", "Advanced")?0:key=="MaximumPasses"?1:key=="Resolution"?2:3;};
        return order(a->key)<order(b->key);
       });
       if(group==Neurotic::UiLiteral("desktop.gamesettingsview.key_binds_2fb9566d", "Key Binds"))std::stable_sort(controls.begin(),controls.end(),[](const auto* a,const auto* b){
        const auto escape=[](const auto* field){return field->section=="Menu"&&field->key=="EscapeClosesMenu";};
        return escape(a)&&!escape(b);
       });
       if(group==Neurotic::UiLiteral("desktop.menu-layout.heading", "Menu layout")) {
        ImGui::TextWrapped(Neurotic::UiLiteral("desktop.menu-layout.restart", "In-game menu. Save, then restart game."));
        ImGui::BeginDisabled(model.installer.busy || std::any_of(controls.begin(),controls.end(),[](const auto* field){return !field->available;}));
        const auto* resetLabel=Neurotic::UiLiteral("desktop.menu-layout.reset","Reset layout");
        const bool resetFits=ImGui::CalcTextSize(Neurotic::Translate(resetLabel).c_str()).x+2*ImGui::GetStyle().FramePadding.x<=ImGui::GetContentRegionAvail().x;
        if(resetFits?ImGui::SmallButton(resetLabel):Neurotic::MenuLayout::ResetLayoutButton(resetLabel)) {
         capture.ResetListening();
         for(auto* field:controls)if(field->available && (field->key=="Corner"||field->key=="Scale"))
          strcpy_s(field->draft.data(),field->draft.size(),field->key=="Corner"?"0":"auto");
        }
        ImGui::EndDisabled();
       }
       for(auto* field:controls){RenderSavedSetting(*field,model,capture,scope);Neurotic::Sleek::ControlDivider();}
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
 }
 EndSavedSettingsFields(naturalFields,deferLayout);if(!reviewDocked)RenderSavedSettingsReview(model);
}
}
