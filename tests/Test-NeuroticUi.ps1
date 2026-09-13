$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$menu = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Raw
$nr = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Menu.cpp') -Raw
$nrControls = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_MenuControls.h') -Raw
$notes = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/NrToggleNotes.h') -Raw
$config = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.h') -Raw
function Assert-Ui([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}
Assert-Ui ($menu -match '(?s)RenderMainMenuBottomBar\(ctx\);.*?RenderMainMenuGraphs\(ctx\);.*?RenderMainMenuHeaderMessages\(ctx\);.*?RenderMainMenuTabs\(ctx\);.*?RenderMainMenuSupportLink\(\);') 'actions, graphs, status, tabs, bottom-right support order'
Assert-Ui ($menu.Contains('ScopedCollapsingHeader("Updates", ImGuiTreeNodeFlags_DefaultOpen)')) 'Updates section is default-open'
Assert-Ui ($menu.Contains('Open NeuRotic on GitHub') -and $menu.Contains('Could not check for updates') -and $menu.Contains('View patch notes')) 'Updates section contains repository, failure, and patch-note actions'
Assert-Ui ($menu.Contains('https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork')) 'NeuRotic update feed and repository link are configured'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderMainMenuTabs.*?DisplaySize\.y - 220\.0f \* ctx\.menuResScale.*?std::min\(720\.0f \* ctx\.menuResScale, viewportRemaining\)') 'tab page height uses a stable viewport reserve'
Assert-Ui (-not ($menu -match '(?s)void MenuCommon::RenderMainMenuTabs.*?ImGui::GetCursorScreenPos\(\).*?const auto renderPage')) 'tab page height does not depend on dragged window position'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderMainMenuTabs.*?BeginTabItem\("General"\).*?BeginTabItem\("Neural Rendering"\).*?BeginTabItem\("Upscaling"\).*?BeginTabItem\("Frame Generation"\).*?BeginTabItem\("Advanced"\).*?BeginTabItem\("Tools"\).*?BeginTabItem\("Diagnostics"\)') 'top-level pages follow the rendered pipeline order'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderDiagnosticsPage.*?RenderLoggingSettings\(ctx\);\s*RenderQuirksSettings\(ctx\);\s*RenderFpsOverlaySettings\(ctx\);') 'logging first in Diagnostics'
foreach ($label in @('Advanced Settings', 'Logging')) {
    Assert-Ui ($menu.Contains('ScopedCollapsingHeader("' + $label + '", ImGuiTreeNodeFlags_DefaultOpen)')) "$label starts expanded"
}
Assert-Ui (-not $nr.Contains('ImGuiTreeNodeFlags_DefaultOpen') -and
           $nr.Contains('DLSS Neural Rendering - ') -and $nr.Contains('Neural Rendering Multipass - ')) 'all Neural Rendering sections start collapsed and main headings show status'
Assert-Ui ($nr.Contains('ScopedCollapsingHeader("Neural Rendering Advisor")') -and
           $nr.IndexOf('ScopedCollapsingHeader("Neural Rendering Advisor"') -lt $nr.IndexOf('DLSS Neural Rendering - ')) 'Advisor is collapsed by default and immediately precedes Neural Rendering'
Assert-Ui ($nr.Contains('WHAT OPTISCALER SEES') -and $nr.Contains('RECOMMENDED SETUP') -and
           $nr.Contains('Graphics card') -and $menu.Contains('ctx.primaryGpu->name.c_str()')) 'Advisor shows the mock signal and recommendation panels with the detected GPU name'
Assert-Ui ($nr.Contains('Native Temporal') -and $nr.Contains('Present Compatibility') -and
           $nr.Contains('Present Enhanced') -and -not $nr.Contains('Prism Enhanced') -and -not $nr.Contains('Prism Compact')) 'Advisor uses the current Present route terminology'
Assert-Ui ($nr.Contains('std::string("##AdvisorRoute") + std::to_string(route)') -and
           $nr.Contains('std::string("Use this route###AdvisorApply")') -and
           $nr.Contains('ApplyAdvisorRoute(config, route);') -and
           -not $nr.Contains('Apply Recommendation')) 'all three route cards expose stable, route-specific apply buttons'
Assert-Ui ($nr.Contains('StartAdvisorAllRoutes') -and $nr.Contains('Analyze All Routes') -and
           $nr.Contains('BeginNextAdvisorRoute') -and $nr.Contains('StartAdvisorAnalysis(config, route)')) 'Advisor supports sequential all-route analysis and individual route tests'
Assert-Ui ($nr.Contains('Target native framerate') -and $nr.Contains('Optimization goal') -and
           $nr.Contains('Prioritize quality') -and $nr.Contains('Balance quality and performance') -and
           $nr.Contains('Prioritize performance')) 'Advisor exposes the approved frame target and optimization goals'
Assert-Ui ($nr.Contains('config.DlssNrApplyModel = false;') -and
           $nr.Contains('config.DlssNrMultipassEnabled = false;') -and
           $nr.Contains('config.DlssNrPasses = 1u;') -and
           $nr.Contains('StageUi::SelectResolutionChoice(config, Advisor().resolutionPreference)')) 'route analysis hides the effect and tests only the selected resolution preference at one pass'
Assert-Ui ($nr.Contains('Analyze temporarily turns Neural Rendering on and tests supported routes sequentially.') -and
           $nr.Contains('your current settings are restored when analysis ends or is cancelled.')) 'Analyze visibly discloses temporary NR activation and restoration before the action'
Assert-Ui ($nr.Contains('advisor.sampling.Consume(present.cadence)') -and
           (Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Present.cpp')).Contains('g_present.telemetry.frameIntervalMs = sample.frameIntervalMs;')) 'Advisor scores each route from its current live frame interval rather than a stale completed route window'
Assert-Ui ($nr.Contains('NR route GPU %.2f ms') -and $nr.Contains('Frame %.2f ms') -and
           $nr.Contains('present.presentGpuRoute == expectedRoute') -and
           (Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Present.cpp')).Contains('g_present.telemetry.presentGpuRoute = slot.pacing.route;')) 'Advisor separates frame cadence from route-tagged Present GPU timing'
Assert-Ui ($nr -match '(?s)else // performance.*?advisor\.routes\[route\]\.succeeded && advisor\.routes\[route\]\.fps > fastest' -and
           $nr.Contains('Best of tested routes: ')) 'Performance and missed-target fallbacks select the fastest measured route with incomplete coverage disclosed'
Assert-Ui ($nr.Contains('BeginTable("##AdvisorPreferences", 3') -and
           $nr.IndexOf('Target native framerate') -lt $nr.IndexOf('Combo("##AdvisorTargetFps"') -and
           $nr.IndexOf('Optimization goal') -lt $nr.IndexOf('Combo("##AdvisorGoal"') -and $nr.Contains('##AdvisorResolution')) 'target, goal and resolution share a responsive three-column row'
Assert-Ui ($nr.Contains('guides.evaluated > advisor.startGuideEvaluations') -and
           $nr.Contains('advisor.startGuideEvaluations = guides.evaluated;')) 'Present Enhanced requires guide evaluations produced during its own test'
Assert-Ui ($nr.Contains('RestoreAdvisorSettings') -and $menu.Contains('CancelAdvisorAnalysis(config') -and
           $menu.Contains('CancelAdvisorAnalysis(Config::Instance()') -and
           $nr.Contains('Output size changed; analysis stopped')) 'save, close, shutdown, cancel, and resize paths restore captured settings'
Assert-Ui ($nr.Contains('Applied the tested route and resolution preference.') -and
           $nr.Contains('StageUi::ResolutionScale(*advisor.routes[route].testedSettings)')) 'Apply uses the measured route and remembered tested resolution'
Assert-Ui ($nr.Contains('BeginTable("##NrStylePresetRow", 2') -and
           $nr.IndexOf('ImGui::TableNextColumn(); renderStyle();') -lt $nr.IndexOf('ImGui::TableNextColumn(); renderModelPreset();')) 'Style and Model preset share a responsive row with Style first'
Assert-Ui (-not $nr.Contains('Can be toggled with a key -- bind it under Keybinds')) 'redundant NR keybind description removed'
Assert-Ui ($nr.Contains('DeferredNrSlider("##NrManualScale"') -and $nr.Contains('0.25f, 2.0f, 1.0f, "%d%%", true)')) 'manual model resolution retains bounded release-to-commit and 100 percent reset'
Assert-Ui (-not $menu.Contains('BeginTabItem("Neural Rendering Multipass")') -and
           -not $menu.Contains('RenderNeuralRenderingMultipassPage')) 'Multipass is contained within Neural Rendering'
$multipass = $nr.Substring($nr.LastIndexOf('static void RenderMultipassMenu'))
Assert-Ui ($nr.Contains('RenderMultipassMenu(config, menuResScale);') -and
           $multipass.Contains('multipassTitle.c_str()')) 'Multipass is its own status-bearing collapsible section beneath Neural Rendering'
Assert-Ui ($multipass.Contains('EmphasizedCheckbox("Enable NR Multipass"')) 'Multipass page uses the emphasized enable control'
Assert-Ui ($nr.Contains('static unsigned int RenderPassCountSelector(Config* config)') -and
           ([regex]::Matches($nr, 'RenderPassCountSelector\(config\)')).Count -eq 1 -and
           $nr.Contains('Combo("Passes"') -and $nr.Contains('"Standard (1 pass)"') -and
           $nr.Contains('"10 passes"')) 'Advanced pass selector exposes one-to-ten range only in Multipass'
Assert-Ui (-not $nr.Contains('##DlssNrMultipassInactiveWarning') -and
           $nr.Contains('Additional passes require Enable NR Multipass on a compatible route.') -and
           $multipass -notmatch 'BeginDisabled\(\);\s*if \(ImGui::BeginTabBar\("NrMultipassLayers"') 'pass profiles remain configurable before Multipass is enabled'
Assert-Ui ($multipass.Contains('BeginTabBar("NrMultipassLayers"') -and
           $multipass.Contains('"Pass %u"')) 'pass count drives numbered pass tabs'
Assert-Ui ($multipass.Contains('Button("Reset All")') -and
           $multipass.Contains('BeginPopupModal("Reset all multipass settings?"') -and
           $multipass.Contains('Button("Confirm")') -and $multipass.Contains('Button("Cancel")')) 'Reset All requires confirm or cancel'
Assert-Ui ($multipass.Contains('for (unsigned int pass = 1; pass < 10; ++pass)') -and
           -not $multipass.Contains('DlssNrPasses = 1u') -and
           $multipass.Contains('CancelNrEdits();')) 'Reset All restores all additional profiles, cancels pending edits, and preserves baseline Pass 1 and the shared pass count'
Assert-Ui ($multipass.Contains('Button("Reset this pass")') -and
           $multipass.Contains('Reset Pass %u profile?##pass%u') -and
           $multipass -match '(?s)ResetPassOptions\(pass\);\s*CancelNrEdits\(\);') 'each selected pass has a confirmed profile reset that cancels pending edits'
Assert-Ui ($multipass.Contains('Copy Pass %u settings') -and
           $multipass -match '(?s)CopyPassOptions\(PassOptions\(config, index - 1\), pass\);\s*CancelNrEdits\(\);') 'later passes can copy the preceding profile without a stale pending edit overwriting it'
Assert-Ui ($multipass.Contains('sharedSlider("Global Pass Resolution (Passes 2–N)###Model Resolution##AdditionalPassModelResolution"') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)') -and
           $nr.Contains('std::string("Reset###Reset##") + (stableLabel + 3)')) 'additional passes share an optional model-resolution slider without changing pass 1 or stable reset identifiers'
Assert-Ui ($multipass -match '(?s)sharedSlider\("Global Pass Resolution \(Passes 2–N\)###.*?sharedSlider\("Global Pass Model Strength \(Passes 2–N\)###.*?sharedSlider\("Global Pass Detail Strength \(Passes 2–N\)###.*?BeginTabBar\("NrMultipassLayers"') 'truthful global Passes 2–N controls precede child tabs'
Assert-Ui ($multipass.Contains('&PassOptionRefs::intensity, 0.0f, 2.0f') -and $multipass.Contains('&PassOptionRefs::transferStrength, 0.0f, 2.0f')) 'shared strengths target independent existing options at zero to two'
Assert-Ui ($multipass.Contains('Changes the Model resolution for every additional pass at once: Pass 2 through the selected final pass.') -and
           $multipass.Contains('releasing commits that percentage to all additional passes and rebuilds them once.')) 'shared model-resolution tooltip clearly describes its all-additional-pass scope and release behavior'
Assert-Ui ($multipass.Contains('if (passCount == 1)') -and
           $multipass.Contains('Pass 1 is configured in the main Neural Rendering section.') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)')) 'baseline Pass 1 has one home and Multipass exposes only additional passes'
foreach ($label in @('Model resolution##pass%u', 'Downscaler##pass%u', 'Model preset##pass%u',
    'Style##pass%u', 'Enlargement##pass%u', 'Detail strength##pass%u',
    'Colour strength##pass%u', 'Highlight guard##pass%u', 'Model Strength##pass%u',
    'Local structure##pass%u', 'Local tone##pass%u', 'Skin structure##pass%u',
    'Auto skin mask##pass%u', 'Proxy composition##pass%u', 'Apply the model##pass%u')) {
    Assert-Ui ($multipass.Contains($label)) "$label is rendered for every pass tab"
}
Assert-Ui (([regex]::Matches($multipass, 'HelpMarker\(')).Count -ge 18) 'every Multipass setting carries a hover hint'
Assert-Ui ($multipass.Contains('DeferredNrSlider(scaleLabel') -and
           $multipass.Contains('Reset##pass%u-auto-mask') -and
           $multipass.Contains('Reset##pass%u-apply')) 'every Multipass setting family exposes an individual reset'
Assert-Ui (-not $nr.Contains('renderSecondLayerControls') -and
           -not $nr.Contains('Enable second neural-rendering layer') -and
           ([regex]::Matches($nr, 'DlssNrMultipassSection')).Count -eq 1) 'legacy two-layer editor is removed and the Multipass header has one stable ImGui ID'
Assert-Ui ($nr -match '(?s)DlssNrSecondLayer\s*=\s*config->DlssNrMultipassEnabled\.value_or_default\(\)\s*&&\s*passCountIndex >= 1;' -and
           $multipass -match '(?s)DlssNrSecondLayer\s*=\s*enabled\s*&&\s*config->DlssNrPasses\.value_or_default\(\) > 1;') 'legacy second-layer compatibility state follows both the Multipass switch and selected pass count'
Assert-Ui ($menu.Contains('https://ko-fi.com/espiownage')) 'approved Ko-fi destination'
Assert-Ui ($menu.Contains('c[ImGuiCol_TabSelected] = AccentStrong();') -and
           $menu.Contains('c[ImGuiCol_TabDimmedSelected] = AccentMed(0.90f);') -and
           $menu.Contains('BeginTabBar("MainMenuPages"') -and
           $multipass.Contains('BeginTabBar("NrMultipassLayers"')) 'selected parent and Multipass tabs use the brighter shared active-tab palette'
Assert-Ui ($menu -match '(?s)Button\("Open Wiki"\).*?ShowHelpMarker\(.*?BeginCombo\("Language"') 'language control follows Wiki button'
Assert-Ui (-not $menu.Contains('Sorry for bad translation.')) 'translation apology removed from UI'
Assert-Ui ($menu -match '(?s)Text\("%d", currentFeature->FrameCount\(\)\);.*?SameLine.*?Text\("GPU: %s", primaryGpu.name.c_str\(\)\);') 'GPU name shares resolution row'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderMainMenuSupportLink\(\).*?Enjoying NeuRotic\?.*?Send Coffee.*?GetContentRegionAvail.*?SetCursorPosX.*?TextUnformatted\(prompt\).*?Button\(button\)') 'compact Send Coffee prompt right-aligned in final row'
Assert-Ui (-not $menu.Contains('constexpr const char* button = "Buy Me a Coffee"')) 'old support-button label is no longer rendered'
Assert-Ui ($nr.Contains('NoteNrUserToggle(OptiClip::ToggleOrigin::Checkbox)')) 'NR checkbox contributes to the OptiClip toggle tracker'
Assert-Ui ($menu.Contains('NoteNrUserToggle(OptiClip::ToggleOrigin::Hotkey)')) 'NR hotkey contributes to the same tracker'
$burst = [regex]::Match($notes, '(?s)ToggleBurstMessages\s*=\s*\{(.*?)\};').Groups[1].Value
Assert-Ui (([regex]::Matches($burst, '(?m)^\s*"')).Count -eq 27) 'accepted OptiClip toggle dialogue pool is present'
Assert-Ui ($nr.Contains('"%s is active."') -and $nr.Contains('"Image unchanged. %s"') -and -not $nr.Contains('Use Native Temporal when it is available.')) 'Present active and actionable safe-fallback guidance without stale recommendation'
$stage = Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_StageControls.h')
$adapter = Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_StageUi.h')
Assert-Ui ($stage.IndexOf('SentenceCombo("##NrStage"') -lt $stage.IndexOf('SentenceCombo("##NrMethod"') -and $stage.IndexOf('SentenceCombo("##NrMethod"') -lt $stage.IndexOf('BeginCombo("##NrResolution"')) 'stage, method and guarded resolution retain fixed order'
Assert-Ui ($stage.Contains('ImGui::TextDisabled("(?)")') -and $stage.Contains('Always Full Output uses the final game output dimensions.')) 'NR resolution explanation is contained in its adjacent tooltip'
Assert-Ui ($menu.Contains('Save Input Settings') -and $menu.Contains('SaveMenuInputSettings') -and
           $menu.Contains('Pending changes apply only after saving.')) 'gameplay input choices remain drafts until targeted or global save succeeds'
Assert-Ui ($menu.Contains('Experimental Mode - Active') -and $menu.Contains('Enable Experimental Mode?') -and
           -not $menu.Contains('Override All Guardrails')) 'experimental settings use a confirmed master switch without an override-all control'
Assert-Ui ($nr.Contains('NR deactivated due to this rendering combination being untested') -and
           $nr.Contains('presentStatus.policyBlocked')) 'guarded Multipass combinations show the requested red explanation'
Assert-Ui ($stage.Contains('"Neural Rendering Injection", "upscaling"') -and
           -not $stage.Contains('"Neural Rendering Injection", "Upscaling"') -and
           $stage.Contains('stageHelp, ImGui::GetFontSize() * 8.0f')) 'stage sentence uses lowercase upscaling and a scale-aware wider selector'
Assert-Ui ($stage -match '(?s)const bool changed = ImGui::Combo.*?if \(hasHelp\).*?SentenceHelpMarker\(help\);.*?if \(\*suffix\)') 'sentence help is rendered directly after its selector and before the suffix'
Assert-Ui ($stage.Contains('Before runs Neural Rendering on the game''s render input before upscaling') -and
           $stage.Contains('Your last selected After method is remembered.')) 'stage help explains both placements and remembered After method'
Assert-Ui ($stage.Contains('Present Compatibility processes the') -and
           $stage.Contains('unavailable or invalid guides preserve the original image.')) 'method help accurately explains Native and both Present routes'
Assert-Ui ($adapter.Contains('DlssNrUiEnhancedManualScale') -and $adapter.Contains('DlssNrUiPresentManualScale') -and $stage.Contains('ResolutionRefusal(snapshot, choice)')) 'Present methods remember independent Manual choices and guard incompatible policies'
Assert-Ui ($nr.Contains('Present history: %s | uninterrupted output frames %llu') -and $nr.Contains('Reset reason: %s | last interruption: %s')) 'Present history diagnostics are visible'
Assert-Ui ($nr.IndexOf('EmphasizedCheckbox("Enable Neural Rendering", &enabled)') -lt $nr.IndexOf('StageUi::RenderControls(*config,')) 'enable lives above the NR injection sentence'
Assert-Ui ($nr.IndexOf('Checkbox("Apply the model", &applyModel)') -lt $nr.IndexOf('StageUi::RenderControls(*config,') -and
           $nr.IndexOf('Checkbox("Apply the model", &applyModel)') -lt $nr.IndexOf('RenderLiveReadouts(config,')) 'Apply remains fixed beside or below Enable and precedes changing status text'
Assert-Ui ($nr.IndexOf('RenderLiveReadouts(config,') -lt $nr.IndexOf('StageUi::RenderControls(*config,') -and
           $nr.Contains('NR processing: %.2f ms per frame') -and
           $nr.Contains('Native Temporal is active.')) 'essential timing and route state render directly above the injection sentence'
Assert-Ui ($nrControls.Contains('enabled ? ImVec4(0.25f, 0.90f, 0.38f, 1.0f)') -and
           $nrControls.Contains('ImVec4(0.95f, 0.25f, 0.22f, 1.0f)') -and
           $nrControls.Contains('return ImVec4(1.0f, 0.72f, 0.18f, 1.0f);') -and
           $nr.Contains('EmphasizedCheckbox("Enable NR Multipass", &enabled)')) 'both enable controls share red off, green on and gold hover or focus states'
Assert-Ui ($nr.Contains('if (StageUi::ResolutionSelection(uiConfig) == 1)') -and
           $stage.Contains('stage == 0 ? 1 : 3, methodHelp, 0.0f, stage == 0')) 'manual slider covers every method and Before retains its fixed visible method'
Assert-Ui ($nr.Contains('I have no idea what this does. Seems like nothing.')) 'Model preset tooltip includes the approved plain-language observation'
Assert-Ui ($nr -match '(?s)TextUnformatted\("Manual resolution"\);\s*HelpMarker\("Sets the Neural Rendering working resolution.*?Reset restores 100%') 'Manual resolution carries an adjacent stage-relative cost and supersampling explanation'
Assert-Ui ($stage.Contains('440.0f * menuScale') -and
           ([regex]::Matches($nr, 'SetNextItemWidth\(mainTuningSliderWidth\(\)\)').Count -eq 6)) 'six main tuning sliders use the responsive doubled preferred width'
foreach ($stableLabel in @('Detail Strength###Detail strength', 'Colour Strength###Colour strength',
    'Local Structure###Local structure', 'Local Tone###Local tone', 'Skin Structure###Skin structure')) {
    Assert-Ui ($nr.Contains($stableLabel)) "$stableLabel displays title case while preserving its prior ImGui identifier"
}
Assert-Ui ($nr.Contains('Shared Model Resolution (All Passes)') -and
           $nr.Contains('for Pass 1 and every active additional pass')) 'Basic resolution truthfully describes its all-pass ownership'
Assert-Ui ($nr -match '(?s)TextUnformatted\(title\);\s*if \(hint\) HelpMarker\(hint\);.*?ResponsiveBasicResolutionWidth' -and
           $nrControls.Contains('preferredWidth * menuScale') -and
           $nr.Contains('"saved main settings.", 320.0f);')) 'Basic resolution help stays beside its title and its slider uses the shorter responsive width'
Assert-Ui ($nrControls.Contains('CumulativeStrengthWidthFraction') -and
           $nrControls.Contains('ResponsiveCumulativeStrengthWidth') -and
           $nr.Contains('DrawCumulativePassSegments') -and
           $nr.Contains('cumulativePasses > 0 ? &sliderMin') -and
           $nr.Contains('Five to ten passes keep the full width')) 'Basic cumulative sliders grow through four passes and show denser coloured pass segments through ten'
Assert-Ui ($nr.Contains('std::string("Reset###Reset##") + (stableLabel + 3)')) 'capitalized deferred sliders preserve their reset identifiers'
$diagnostics = $nr.IndexOf('ScopedCollapsingHeader("Advanced Settings / Diagnostics##NrAdvanced")')
Assert-Ui ($diagnostics -gt $nr.IndexOf('RenderLiveReadouts(config,') -and
           $diagnostics -gt $nr.IndexOf('Checkbox("Apply the model"') -and
           $diagnostics -lt $nr.IndexOf('renderReadouts(true);') -and
           -not $nr.Contains('renderReadouts(false);') -and
           $nr.Contains('StageUi::DimensionText(actualW, actualH, outputW, outputH)')) 'live actual dimensions precede controls while detailed history remains in Diagnostics'
Assert-Ui ($nr.Contains('observation.Fresh(selection,') -and $stage.Contains('NR: unavailable | Output: unavailable')) 'new selection waits for fresh telemetry; unknown sizes are explicit'
Assert-Ui ($header.Contains('DlssNrRoute { 2 }') -and $header.Contains('DlssNrEnhancedResolution { 0 }') -and
           $header.Contains('DlssNrEnabled { false }') -and $header.Contains('LogToFile { false }')) 'fresh Match Game Render defaults do not enable NR or file logging'
Assert-Ui ($menu.Contains('bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile)') -and
           $menu.Contains('config->LogToFile = toFile;') -and $config.Contains('LogToFile.set_from_config(readBool("Log", "LogToFile"))')) 'existing file logging checkbox uses effective config and keeps deliberate changes'
foreach ($iniPath in @('OptiScaler.ini', 'integration/OptiScaler.ini')) {
    $ini = Get-Content -Raw -LiteralPath (Join-Path $root $iniPath)
    Assert-Ui ($ini -match '(?m)^LogToFile\s*=\s*false\s*$') "$iniPath explicitly disables file logging"
}
Assert-Ui ($header.Contains('MenuLanguage { "en" }')) 'English default'
Assert-Ui ($config.Contains('readString("Menu", "Language", true)') -and $config.Contains('ini.SetValue("Menu", "Language", Instance()->MenuLanguage.value_or_default().c_str());')) 'language loads and saves in Menu section'
