$ErrorActionPreference = 'Stop'
$PSDefaultParameterValues['Get-Content:Encoding'] = 'UTF8'
$root = Split-Path $PSScriptRoot -Parent
$menu = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Raw
$theme = Get-Content -Raw (Join-Path $root 'OptiScaler/menu/SleekTheme.h')
$shell = Get-Content -Raw (Join-Path $root 'OptiScaler/menu/SleekShell.h')
$menuHeader = (Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.h') -Raw) + (Get-Content -Raw (Join-Path $root 'OptiScaler/menu/SleekSections.h'))
$nr = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Menu.cpp') -Raw
$nrControls = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_MenuControls.h') -Raw
$config = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.h') -Raw
$session = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/NrExperimentalSession.h') -Raw
$escapeUi = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/input/MenuEscapeClose.h') -Raw
$pilot = Get-Content -Raw (Join-Path $root 'OptiScaler/menu/SleekPilotLight.h')
$mfgWarning = Get-Content -Raw (Join-Path $root 'OptiScaler/menu/MfgRestartWarning.h')
$mfgControls = Get-Content -Raw (Join-Path $root 'OptiScaler/menu/ExperimentalMfgControls.h')
# These assertions inspect control structure and persistence. A stable-ID text
# binding keeps the original C++ literal; remove only that presentation adapter
# in the inspected copy. Runtime pack/binding tests verify translation separately.
function Remove-UiTextBindings([string]$source) {
    [regex]::Replace($source, 'Neurotic::Ui(?:Literal|Message|Options)\("(?:[^"\\]|\\.)*",\s*("(?:[^"\\]|\\.)*"(?:\s*"(?:[^"\\]|\\.)*")*)\)', '$1')
}
$menu = Remove-UiTextBindings $menu
$shell = Remove-UiTextBindings $shell
$menuHeader = Remove-UiTextBindings $menuHeader
$nr = Remove-UiTextBindings $nr
$nrControls = Remove-UiTextBindings $nrControls
$mfgControls = Remove-UiTextBindings $mfgControls
$mfgWarning = Remove-UiTextBindings $mfgWarning
function Assert-Ui([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}
$fgMenu = $menu.Substring($menu.IndexOf('void MenuCommon::RenderFrameGenerationSelection'))
Assert-Ui ($fgMenu.IndexOf('Neurotic::Sleek::ExperimentalMfgControls(') -ge 0 -and
           $fgMenu.IndexOf('Neurotic::Sleek::ExperimentalMfgControls(') -lt $fgMenu.IndexOf('if (ctx.childPage == 1 && state.activeFgInput != FGInput::ForceXeLL)') -and
           $mfgControls.Contains('Native MFG unlock for RTX 40 series')) 'native unlock stays in its child page before replacement route gates'
Assert-Ui ($fgMenu.Contains('nativeCard.emplace("##NativeMfgCompatibility")') -and
           $fgMenu.IndexOf('nativeCard.emplace("##NativeMfgCompatibility")') -lt $fgMenu.IndexOf('Neurotic::Sleek::DrawMfgRestartWarning(false)') -and
           $fgMenu.Contains('SetCursorScreenPos(compatibilityHeader)') -and $fgMenu.Contains('MfgRestartRequired(') -and
           $mfgWarning.Contains('ImGui::Dummy({width, height})') -and
           $mfgWarning.Contains('SAVE SETTINGS AND RESTART THE GAME')) 'restart warning shares the permanently reserved GPU compatibility title row and retains runtime restart policy'
Assert-Ui ($fgMenu.Contains('ResolveMfgSetup') -and $fgMenu.Contains('MfgSelectableGeneratedMax') -and -not $fgMenu.Contains('publication.maxGenerated : 5u')) 'native setup has truthful failure status and qualified high-ratio bounds'
Assert-Ui ($fgMenu -match 'if \(state.dlssgGameDMFGSupported && !dlssgInputOrOutput\)\s*\{\s*ImGui::BeginDisabled\(nativeSelected\);') 'native selection disables dynamic MFG and its controls'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderFakenvapiSettings.*?ImGui::BeginDisabled\(nativeMfgSelected\);.*?Checkbox\("Force XeLL".*?ImGui::EndDisabled\(\);') 'native selection disables Force XeLL after staging'
Assert-Ui ($menu -match '(?s)RenderMainMenuWindowActions\(ctx\);.*?RenderMainMenuBottomBar\(ctx\);.*?RenderMainMenuGraphs\(ctx\);.*?RenderMainMenuHeaderMessages\(ctx\);.*?RenderMainMenuTabs\(ctx\);.*?RenderMainMenuSupportLink\(ctx\);') 'header actions, toolbar, graphs, status, tabs and footer order'
Assert-Ui ($shell.Contains('general[]={"Updates","Key Binds"') -and $menu.Contains('int childPage = 0;')) 'Updates is the initial General child tab'
Assert-Ui ($menu.Contains('Neurotic::Sleek::ApplyTheme(config, !drawingLight)') -and
           $shell.Contains('style.WindowRounding = 16;') -and
           $theme.Contains('conf->MenuAccentColorR.has_value()') -and
           $theme.Contains('conf->MenuBGColorR.has_value()') -and
           $menu -match 'config->LightTheme\s*=\s*lightTheme;') 'sleek presentation preserves configured accents, background tint and light/dark themes'

Assert-Ui ($menuHeader.Contains('std::string displayed(label, labelEnd);') -and
           $menuHeader.Contains('Neurotic::Translate(displayed.c_str())')) 'custom heading draw hides ImGui ID suffixes'
Assert-Ui ($menu.Contains('Open NeuRotic on GitHub') -and $menu.Contains('Update status unknown') -and $menu.Contains('View patch notes')) 'Updates section contains repository, truthful unknown update state, and patch-note actions'
Assert-Ui ($menu.Contains('color = toneMapColor(ImVec4(.35f,.9f,.5f,1))') -and
           $menu.IndexOf('Installed Version: %s') -lt $menu.IndexOf('DlssNr::StatusPanel::Line("##UpdateStatus"') -and
           $menu.IndexOf('DlssNr::StatusPanel::Line("##UpdateStatus"') -lt $menu.IndexOf('ImGui::TextLinkOpenURL("Open NeuRotic on GitHub"')) 'installed version and green update status precede the GitHub link'
Assert-Ui ($menu.Contains('BeginTable("##KeybindSettings", 5') -and
           $menu.Contains('TableSetupColumn("Action"') -and
           $menu.Contains('TableSetupColumn("Modifier"') -and
           $menu.Contains('TableSetupColumn("##Plus"') -and
           $menu.Contains('TableSetupColumn("Keybind"') -and
           $menu.Contains('TableSetupColumn("##Reset"') -and
           $menu.Contains('ImGui::Button("Reset")') -and
           -not $menu.Contains('ImGui::Button("R")')) 'keybind actions, modifier, plus, key and labeled resets align in a five-column table'
Assert-Ui ($shell.Contains('"Brightness","Animations"') -and $header.Contains('MenuReduceMotion { false }') -and
           $config.Contains('readBool("Menu", "ReduceMotion")') -and
           $config.Contains('GetBoolValue(Instance()->MenuReduceMotion.value_for_config())') -and
           $menu.Contains('Checkbox("Turn animations off.", &off)') -and
           $menu.Contains('reduceMenuMotion = config->MenuReduceMotion.value_or_default()')) 'final Animations tab persists the checkbox and synchronizes presentation motion'
Assert-Ui ($header.Contains('MenuBGColorA { 0.950f }')) 'background alpha defaults to 0.950'
Assert-Ui ($menu.Contains('https://github.com/MagicalPrincessUnicorn/NeuRotic-an-OptiScaler-DLSSNR-fork')) 'NeuRotic update feed and repository link are configured'
Assert-Ui ($menu.Contains('ImGui::GetContentRegionAvail().y-footerReserve') -and
           $menu.Contains('Neurotic::Sleek::WindowPosition(position') -and
           $menu.Contains('menuHeight.Size(mainViewport->Size,menuResScale)') -and
           $menu.Contains('menuHeight.RowHeight() + ImGui::GetStyle().ItemSpacing.y * 2') -and
           $menu.Contains('menuHeight.Draw(ImGui::GetMainViewport()->Size, ctx.menuResScale, height)')) 'page scroll area reserves the vertical grip and complete window fits the viewport after scale changes'
Assert-Ui ($shell -match '(?s)Pages\[\].*?General.*?Upscaling.*?Neural Rendering.*?Frame Generation.*?Advanced.*?Tools.*?Diagnostics' -and
           $menu.Contains('Navigation(selected,pageHeight,wide,status)') -and
           $menu.Contains('case 6: RenderDiagnosticsPage(ctx)')) 'all seven pages remain available in pipeline order in responsive top navigation'

Assert-Ui ($menu -match '(?s)void MenuCommon::RenderDiagnosticsPage.*?RenderLoggingSettings\(ctx\);\s*if \(ctx.childPage == 1\) RenderQuirksSettings\(ctx\);\s*if \(ctx.childPage == 2\) RenderFpsOverlaySettings\(ctx\);') 'logging first in Diagnostics'
Assert-Ui ($shell.Contains('case 4: count=5;return advanced;') -and -not $menu.Contains('ScopedCollapsingHeader("Advanced Settings"')) 'Advanced uses exposed child pages'
Assert-Ui (-not $nr.Contains('ScopedCollapsingHeader("Neural Preflight"') -and
           -not $nr.Contains('###DlssNrMainSection') -and $nr.Contains('page == MenuPage::Multipass')) 'main NR controls render directly on their page'
$nrDiagnostics = $nr.Substring($nr.IndexOf('if (page == MenuPage::Diagnostics)'))
$nrDiagnostics = $nrDiagnostics.Substring(0, $nrDiagnostics.IndexOf('return;'))
Assert-Ui (-not $nr.Contains('ImGui::CollapsingHeader("Signal Status###NrPreflight")') -and
           $nrDiagnostics.IndexOf('RenderPreflight(config, PreflightView::Signals);') -ge 0 -and
           $nrDiagnostics.IndexOf('RenderPreflight(config, PreflightView::Signals);') -lt $nrDiagnostics.IndexOf('RenderPreflight(config, PreflightView::Details);') -and
           $nr.Contains('Canonical input readiness') -and $nr.Contains('Image quality') -and
           $nr.Contains('Motion & history') -and $nr.Contains('Not observed') -and
           $nr.Contains('SelectedNativeTelemetry()') -and $nr.Contains('DlssNr::PresentTelemetry()')) 'always-visible Signal Status precedes detailed observations in Diagnostics'
Assert-Ui (-not $nr.Contains('##PreflightKey') -and $nr.Contains('##PreflightLamps') -and
           $nr.Contains('const std::array<Lamp, 17> lamps') -and
           $nr.Contains('{"Graphics input", host.currentFeature ? State::Ready : State::Inactive') -and
           $nr.Contains('width >= ImGui::GetFontSize() * 40.0f ? 4') -and
           $nr.Contains('page == MenuPage::Diagnostics')) 'signal board remains responsive and detailed diagnostics has one home'
Assert-Ui ($nr.Contains('AddText(pos, ImGui::GetColorU32(color(lamp.state))') -and
           $nr.Contains('draw->AddLine({ marker.x - radius') -and
           -not $nr.Contains('ImGui::SetTooltip')) 'signal labels and vector marks retain state without hover-only explanations'
Assert-Ui ($nr.Contains('ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersOuter | ImGuiTableFlags_BordersInnerV') -and
           -not $nr.Contains('ImGuiStyleVar_CellPadding')) 'Signal Status readouts keep their outline without a key row'
Assert-Ui ($nrDiagnostics.Contains('RenderPreflight(config, PreflightView::Signals);') -and
           $nr.Contains('RenderPreflight(config, PreflightView::Details);')) 'detailed input explanations remain available in Diagnostics'
Assert-Ui ($nr.IndexOf('renderReadouts = [&]()') -lt $nr.IndexOf('RenderPreflight(config, PreflightView::Signals);') -and
           $nr.IndexOf('renderReadouts();') -gt $nr.IndexOf('page == MenuPage::Diagnostics') -and
           -not $nr.Contains('if (!detailed)')) 'detailed runtime readouts remain available with the unused duplicate summary removed'
# Historical three-route Advisor assertions remain below for its dormant source only.
# The active menu contract now requires cancellation/restoration and no render/tick entry.
Assert-Ui (-not $nr.Contains('RenderAdvisor(config, menuResScale);') -and
           -not $menu.Contains('DlssNr::TickAdvisor(ctx.config);') -and
           $menu.Contains('CancelAdvisorAnalysis(ctx.config, "Advisor archived; original settings restored.")')) 'Advisor is archived and active analysis restores settings'
Assert-Ui (-not $nr.Contains('void RenderAdvisor(') -and
           -not $nr.Contains('void RenderAdvisorRouteCard(') -and
           -not $nr.Contains('Analyze All Routes')) 'Advisor UI and active analysis buttons are removed'
Assert-Ui ($nr.Contains('config.DlssNrApplyModel = false;') -and
           $nr.Contains('config.DlssNrMultipassEnabled = false;') -and
           $nr.Contains('config.DlssNrPasses = 1u;') -and
           $nr.Contains('StageUi::SelectResolutionChoice(config, Advisor().resolutionPreference)')) 'route analysis hides the effect and tests only the selected resolution preference at one pass'
Assert-Ui ($nr.Contains('result.fps = 1000.0 / result.frameMs') -and
           $nr.Contains('present.presentGpuRoute == expectedRoute') -and
           (Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Present.cpp')).Contains('g_present.telemetry.presentGpuRoute = slot.pacing.route;')) 'Advisor separates frame cadence from route-tagged Present GPU timing'
Assert-Ui ($nr -match '(?s)else // performance.*?advisor\.routes\[route\]\.succeeded && advisor\.routes\[route\]\.fps > fastest' -and
           $nr.Contains('Best of tested routes: ')) 'Performance and missed-target fallbacks select the fastest measured route with incomplete coverage disclosed'
Assert-Ui ($nr.Contains('guides.evaluated > advisor.startGuideEvaluations') -and
           $nr.Contains('advisor.startGuideEvaluations = guides.evaluated;')) 'Present Enhanced requires guide evaluations produced during its own test'
Assert-Ui ($nr.Contains('RestoreAdvisorSettings') -and $menu.Contains('CancelAdvisorAnalysis(config') -and
           $menu.Contains('CancelAdvisorAnalysis(Config::Instance()') -and
           $nr.Contains('Output size changed; analysis stopped')) 'save, close, shutdown, cancel, and resize paths restore captured settings'
Assert-Ui ($nr.Contains('Applied the tested route and resolution preference.') -and
           $nr.Contains('StageUi::ResolutionScale(*advisor.routes[route].testedSettings)')) 'Apply uses the measured route and remembered tested resolution'
Assert-Ui (-not $nr.Contains('BeginTable("##NrStylePresetRow", 2') -and
           $nr.IndexOf('renderStyle();') -lt $nr.IndexOf('renderModelPreset();')) 'Style and Preset stack with Style first'
Assert-Ui (-not $nr.Contains('Can be toggled with a key -- bind it under Keybinds')) 'redundant NR keybind description removed'
Assert-Ui ($nr.Contains('DeferredNrSlider("##NrManualScale"') -and $nr.Contains('0.25f, 2.0f, 1.0f, "%d%%", true)')) 'manual model resolution retains bounded release-to-commit and 100 percent reset'
Assert-Ui (-not $menu.Contains('BeginTabItem("Neural Rendering Multipass")') -and
           -not $menu.Contains('RenderNeuralRenderingMultipassPage')) 'Multipass is contained within Neural Rendering'
$multipass = $nr.Substring($nr.LastIndexOf('static void RenderMultipassMenu'))
Assert-Ui ($nr.Contains('RenderMultipassMenu(config, menuResScale);') -and
           $nr.Contains('page == MenuPage::Multipass')) 'Multipass owns its child page beneath Neural Rendering'
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
Assert-Ui ($multipass.Contains('sharedSlider("Model Resolution###Model Resolution##AdditionalPassModelResolution"') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)') -and
           $nr.Contains('std::string("Reset###Reset##") + (stableLabel + 3)')) 'additional passes share an optional model-resolution slider without changing pass 1 or stable reset identifiers'
Assert-Ui ($multipass -match '(?s)sharedSlider\("Model Resolution###.*?sharedSlider\("Model Strength###.*?sharedSlider\("Detail Strength###.*?BeginTabBar\("NrMultipassLayers"') 'truthful global Passes 2–N controls precede child tabs'
Assert-Ui ($multipass.Contains('&PassOptionRefs::intensity, 0.0f, 2.0f') -and $multipass.Contains('&PassOptionRefs::transferStrength, 0.0f, 2.0f')) 'shared strengths target independent existing options at zero to two'
Assert-Ui ($multipass.Contains('Changes the Model resolution for every additional pass at once: Pass 2 through the selected final pass.') -and
           $multipass.Contains('releasing commits that percentage to all additional passes and rebuilds them once.')) 'shared model-resolution tooltip clearly describes its all-additional-pass scope and release behavior'
Assert-Ui ($multipass.Contains('if (passCount == 1)') -and
           $multipass.Contains('Pass 1 is configured in NR Settings.') -and
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
           $shell.Contains('"###NrMultipass"')) 'legacy two-layer editor is removed and the Multipass header has one stable ImGui ID'
Assert-Ui ($nr -match '(?s)DlssNrSecondLayer\s*=\s*config->DlssNrMultipassEnabled\.value_or_default\(\)\s*&&\s*passCountIndex >= 1;' -and
           $multipass -match '(?s)DlssNrSecondLayer\s*=\s*enabled\s*&&\s*config->DlssNrPasses\.value_or_default\(\) > 1;') 'legacy second-layer compatibility state follows both the Multipass switch and selected pass count'
Assert-Ui ($menu.Contains('https://ko-fi.com/espiownage')) 'approved Ko-fi destination'
Assert-Ui ($theme.Contains('c[ImGuiCol_TabSelected] = AccentSoft();') -and
           $theme.Contains('c[ImGuiCol_TabDimmedSelected] = AccentMed(0.90f);') -and
           $menu.Contains('Navigation(selected,pageHeight,wide,status)') -and
           $multipass.Contains('BeginTabBar("NrMultipassLayers"')) 'selected navigation and Multipass use the shared accent palette'
Assert-Ui (-not $menu.Contains('ImGui::Button("Open Wiki")') -and $shell.Contains('##FooterLanguage') -and $menu.Contains('FooterControls(language,_selectedScale,autoText.c_str())')) 'Wiki removed and language lives beside support in footer'
Assert-Ui (-not $menu.Contains('Sorry for bad translation.')) 'translation apology removed from UI'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderMainMenuSupportLink.*?feature->RenderWidth\(\).*?feature->FrameCount\(\).*?ctx.primaryGpu->name.*?FooterReadout') 'original resolution, frame and GPU facts share the compact footer readout'
Assert-Ui ($menu.Contains('Neurotic::Sleek::FooterControls(language,_selectedScale,autoText.c_str())') -and $shell.Contains('result.supportClicked=SupportButton();')) 'compact Ko-fi support action connects to the persistent footer'
Assert-Ui (-not $menu.Contains('constexpr const char* button = "Buy Me a Coffee"')) 'old support-button label is no longer rendered'
Assert-Ui ($nr.Contains('"%s is active."') -and $nr.Contains('"Output may have changed. %s"') -and $nr.Contains('"Image unchanged. %s"') -and -not $nr.Contains('Use Native Temporal when it is available.')) 'Present active and truthful partial-effect guidance without stale recommendation'
$stage = Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_StageControls.h')
$adapter = Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNr_StageUi.h')
$stage = Remove-UiTextBindings $stage
$adapter = Remove-UiTextBindings $adapter
Assert-Ui ($stage.Contains('SentenceCombo("##NrMethod", "", "NR Mode"') -and
           $stage.Contains('ImGui::BeginCombo("##NrResolution"') -and
           $adapter.Contains('"Match Native"') -and $adapter.Contains('"Full output"') -and
           $adapter.Contains('"Manual"') -and
           $stage.Contains('const int resolutionDisplayOrder[] = { 1, 0, 2 };')) 'dropdown-first mode and resolution retain their choices and order'
Assert-Ui (-not $stage.Contains('SentenceCombo("##NrStage"') -and
           $stage.IndexOf('SentenceCombo("##NrMethod"') -lt $stage.IndexOf('BeginCombo("##NrResolution"') -and
           $stage.Contains('SelectUnifiedMethod(config, method)') -and
           $stage.Contains('SelectUnifiedResolution(config, choice)')) 'two-method selector precedes resolution without a visible placement toggle'
Assert-Ui (-not $stage.Contains('SetTooltip') -and
           $stage.Contains('NativePlacementRefusal(snapshot, nativeAfterOnly)') -and
           $stage.Contains('if (refusal) ImGui::TextWrapped("%s",Neurotic::Translate(refusal).c_str());') -and
           $nr.Contains('StageUi::ResolutionChoiceSelection(uiConfig)')) 'NR resolution keeps placement checks and visible refusal explanations without tooltips'
Assert-Ui ($menu.Contains('Save Input Settings') -and $menu.Contains('SaveMenuInputSettings') -and
           $menu.Contains('Pending changes apply only after saving.')) 'gameplay input choices remain drafts until targeted or global save succeeds'
Assert-Ui (-not $menu.Contains('Checkbox("Unlock Experimental Mode"') -and
           -not $menu.Contains('BeginDisabled(!draft.active)') -and
           -not $menu.Contains('PendingExperimentalEnable') -and
           $menu.Contains('Confirm experimental settings##Dedicated') -and
           $menu.Contains('Confirm experimental settings##Global') -and
           $menu.Contains('NewChoicesRequested(*ctx.config, draft)') -and
           $menu.Contains('ExperimentalPolicy::SaveDraft(*config,') -and
           $menu.Contains('option("Preserve NR During Camera Cuts"') -and
           $menu.Contains('draft.dirty = true;') -and
           $menu.Contains('SaveExperimentalSettings(false, false,') -and
           -not $menu.Contains('option("Override HDR Guardrails (Experimental)"') -and
           -not $menu.Contains('option("Override FG Guardrails (Experimental)"')) 'save-time confirmation covers retained experimental choices while HDR and FG use normal paths'
Assert-Ui ($menu -match '(?s)RenderNeuralRenderingPage.*?ctx.neuralPage == 4.*?RenderNeuralRenderingExperimentalSettings' -and
           $menu.Contains('option("Preserve NR During Camera Cuts"')) 'experimental settings own the final Neural Rendering child tab'
Assert-Ui ($session.Contains('MarkerOwnerAlive') -and $session.Contains('recoveredUnclean') -and
           $session.Contains('SaveExperimentalSettings(false, false, false, false)') -and
           $session.Contains('DlssNrPreSrSoftReset = false') -and
           $menu.Contains('Last session ended unexpectedly. Experimental options have been disabled.')) 'an unclean active experimental session clears the master and child overrides and reports it after startup'
Assert-Ui ($session.Contains('markerUnavailable') -and $session.Contains('if (!Detail::WriteMarker(true))') -and
           $session.Contains('DisableInMemory(config)') -and
           $menu.Contains('Experimental options could not be activated because the crash-recovery marker could not be written.')) 'experimental activation fails closed when its crash-recovery marker cannot be written'
Assert-Ui (-not $menu.Contains('option("Override Multipass NR Guardrails (Experimental)"') -and -not $nr.Contains('const bool blockedMultipass') -and -not $nr.Contains('NR Multipass still uses its Experimental override')) 'Multipass uses the normal route controls without a separate combination override'
Assert-Ui ($stage.Contains('"NR Mode"') -and
           $stage.Contains('"NR Resolution"') -and
           $stage.Contains('const float wrappedWidth = hasHelp ?')) 'stage selectors preserve their labels and responsive width'
Assert-Ui ($stage.Contains('qualified depth and motion guides when available') -and
           $stage.Contains('image-only input otherwise') -and
           $adapter.Contains('UnifiedResolutionRefusal') -and
           $adapter.Contains('DlssNrPresentInputPolicy = 2u')) 'Present Auto and Native placement controls expose the unified contract'
Assert-Ui ($stage.Contains('bool stackedLabels = false') -and $stage.Contains('"NR Resolution"') -and $nr.Contains('ControlLabel("Style")') -and $nr.Contains('ControlLabel("Preset")') -and -not $stage.Contains('StatusPanel::Line("##NrPlacement", {}')) 'mode guidance and selector labels reflect the requested layout without an empty placement row'
Assert-Ui ($adapter.Contains('DlssNrUiEnhancedManualScale') -and $adapter.Contains('DlssNrUiPresentManualScale') -and
           $stage.Contains('UnifiedResolutionRefusal(snapshot, choice, nativeAfterOnly)')) 'legacy Present Manual memories remain and Native after-only placement is guarded'
Assert-Ui ($nr.Contains('Present history: %s | uninterrupted output frames %llu') -and $nr.Contains('Reset reason: %s | last interruption: %s')) 'Present history diagnostics are visible'
$enablePosition = $nr.IndexOf('Checkbox("Enable Neural Rendering", &enabled)')
$stagePosition = $nr.IndexOf('StageUi::RenderControls(*config,')
Assert-Ui ($enablePosition -ge 0 -and $stagePosition -ge 0 -and $enablePosition -lt $stagePosition) 'enable lives above the NR injection sentence'
Assert-Ui ($nr.IndexOf('Checkbox("Apply the model", &applyModel)') -gt $nr.IndexOf('Checkbox("Enable Neural Rendering", &enabled)') -and $nr.IndexOf('Checkbox("Apply the model", &applyModel)') -lt $nr.IndexOf('StageUi::RenderControls(*config,')) 'Apply remains fixed beside or below Enable and precedes changing status text'
Assert-Ui ($nr.Contains('BeginNrCard("##NrRenderingCard", "Rendering")') -and $nr.Contains('mainCards.emplace("##NrMainCards")') -and $nr.Contains('NrReadoutGroup::Activity') -and $nr.Contains('NrReadoutGroup::Output') -and $nr.Contains('NR processing: %.2f ms per frame')) 'Main uses paired cards and retains live activity/output readouts'
Assert-Ui ($nrControls.Contains('enabled ? ImVec4(0.25f, 0.90f, 0.38f, 1.0f)') -and
           $nrControls.Contains('ImVec4(0.95f, 0.25f, 0.22f, 1.0f)') -and
           $nrControls.Contains('ImVec4(1.0f, 0.72f, 0.18f, 1.0f)') -and
           $nrControls.Contains('ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)') -and
           $nrControls.Contains('ImGui::GetStyleColorVec4(ImGuiCol_CheckMark)') -and
           $nr.Contains('EmphasizedCheckbox("Enable NR Multipass", &enabled)')) 'both enable controls share accent on, neutral off and native keyboard focus; classic presentation remains available'
Assert-Ui ($nr.Contains('if (StageUi::ResolutionSelection(uiConfig) == 1)') -and
           $stage.Contains('2, methodHelp')) 'manual slider covers both visible methods'
Assert-Ui ($nr.IndexOf('StageUi::RenderControls(*config,') -lt $nr.IndexOf('TextUnformatted("Manual resolution")') -and
           $nr.IndexOf('TextUnformatted("Manual resolution")') -lt $nr.IndexOf('NrReadoutGroup::Output, status);') -and
           $nr.Contains('220.0f * menuResScale')) 'manual slider follows resolution in the wider controls column'
Assert-Ui ($nr.IndexOf('ControlLabel("Style")') -lt $nr.IndexOf('ImGui::Combo("##NrStyle"') -and $nr.IndexOf('ControlLabel("Preset")') -lt $nr.IndexOf('ImGui::Combo("##NrModelPreset"') -and $nr.Contains('mainCards->Next()')) 'Main pairs Rendering with composition and places selector labels above fields'
Assert-Ui ($nr.Contains('BeginNrCard("##NrDiagnosticRuntime", "Runtime & completion")') -and $nr.Contains('RenderPreflight(config, PreflightView::Details);') -and $nr.Contains('page == MenuPage::Diagnostics') -and $nr.Contains('ScopedCollapsingHeader("Advanced resampling##NrResampling")')) 'diagnostics render directly and advanced resampling remains available in the scoped cards'
Assert-Ui ($menu.Contains('Neurotic::Sleek::Brand(state.gameExe.c_str(),') -and
           $shell.Contains('Neurotic::Brand::Draw(ImGui::GetWindowDrawList(),pos') -and
           $shell.Contains('draw->AddLine') -and
           -not $menu.Contains('RenderNeuroticBanner(ctx.menuResScale);')) 'all pages share the official NeuRotic header logo and retain the navigation glyphs'
Assert-Ui ($shell.Contains('Show Graphs###HeaderGraphs') -and
           $menu.Contains('HeaderActions(lightTheme,nullptr,&_showMainMenuGraphs)') -and
           $menu.Contains('ImGui::GetContentRegionAvail().x * 0.5f') -and
           $menu.Contains('BeginTable("plots", 2, ImGuiTableFlags_SizingStretchSame') -and
           $menu.Contains('"##FrameTimePlot"') -and $menu.Contains('"##UpscalerTimePlot"') -and
           $menuHeader.Contains('_showMainMenuGraphs = false') -and
           -not $menu.Contains('GPU Stage Timings') -and -not $menu.Contains('GPU stage timings')) 'Show Graphs shares the top row, starts off, and gives both plots equal half-width areas with a fixed readout row'
foreach ($resetKey in @('MagnifierSize','MagnifierZoomFactor','MagnifierBorderSize','MagnifierStaticPosX','MagnifierStaticPosY','MagnifierCursorOffsetX','MagnifierCursorOffsetY')) {
    Assert-Ui ($menu.Contains('Button("Reset##'+$resetKey+'")') -and $menu.Contains('config->'+$resetKey)) ($resetKey+' exposes its individual Reset')
}
Assert-Ui ($menu -match '(?s)Button\("Reset##MipmapBias"\).*?config->MipmapBiasOverride\.reset\(\);\s*_mipBias = 0.0f;\s*state.lastMipBias = 100.0f;\s*state.lastMipBiasMax = -100.0f;') 'Mipmap Reset clears the saved override, staged bias and sampler caches'
Assert-Ui ($menu -match 'float defaultRatio = 1.5f;' -and
           $menu -match 'if \(ImGui::Button\("Reset##OutputScalingRatio"\)\) _ssRatio = defaultRatio;' -and
           $menu -match '(?s)Button\("Apply Change"\).*?config->OutputScalingMultiplier = _ssRatio;.*?MARK_ALL_BACKENDS_CHANGED\(\);') 'Output Scaling Ratio Reset changes only the staged 1.5 ratio while Apply retains config and backend reinitialization ownership'
Assert-Ui (-not $menu.Contains('menuControlNotes') -and -not $nr.Contains('nrControlNotes') -and $menu.Contains('void MenuCommon::ShowTooltip(const char*) {}')) 'Tooltip text is not collected into exposed control notes'
Assert-Ui ($nr -match '(?s)TextUnformatted\("Manual resolution"\);\s*HelpMarker\("Sets the Neural Rendering working resolution.*?Reset restores 100%') 'Manual resolution carries an adjacent stage-relative cost and supersampling explanation'
Assert-Ui ($nr.Contains('BeginNrCard("##NrAppearanceCard", "Model & composition")') -and $nr.Contains('Neurotic::Sleek::CardSliderFloat') -and $nr.Contains('ControlLabel(label)') -and $nr.Contains('ImGui::SmallButton(resetId)')) 'Composition controls use compact rails with labels and resets'
foreach ($stableLabel in @('###Detail strength', '###Colour strength',
    'Local Structure###Local structure', 'Local Tone###Local tone', 'Skin Structure###Skin structure')) {
    Assert-Ui ($nr.Contains($stableLabel)) "$stableLabel preserves its prior ImGui identifier"
}
Assert-Ui ($nr.Contains('for Pass 1 and every active additional pass')) 'Basic resolution truthfully describes its all-pass ownership'
Assert-Ui ($nr.Contains('SetNextItemWidth(basicWidth)') -and
           $nr.Contains('SetNextItemWidth(globalWidth)') -and
           $nrControls.Contains('HalfWidthSliderGroup')) 'Basic and global sliders use equal half-width tracks with inline resets and help'
Assert-Ui ($nr.Contains('DrawCumulativePassSegments') -and
           $nr.Contains('cumulativePasses > 0 ? &sliderMin') -and
           $nr.Contains('Cumulative totals fill passes from left to right.')) 'Basic cumulative sliders retain coloured pass segments without varying track lengths'
Assert-Ui ($nr.Contains('std::string("Reset###Reset##") + (stableLabel + 3)')) 'capitalized deferred sliders preserve their reset identifiers'
$features = $nr.IndexOf('mainCards.emplace("##NrMainCards")')
$diagnostics = $nr.IndexOf('page == MenuPage::Diagnostics')
Assert-Ui ($features -ge 0 -and $nr.Contains('Checkbox("Auto skin mask"') -and
           $features -lt $nr.IndexOf('renderResampling();') -and
           $nr.Contains('Combo("Reversible proxy"') -and
           $features -lt $nr.IndexOf('BeginNrCard("Color##DlssNrColourSection", "Color")') -and
           $features -lt $nr.IndexOf('ScopedCollapsingHeader("Compare##DlssNrCompareSection")') -and
           $nrDiagnostics.IndexOf('RenderPreflight(config, PreflightView::Details);') -lt $nrDiagnostics.IndexOf('renderReadouts();') -and
           $diagnostics -lt $nr.IndexOf('renderReadouts();') -and
           $diagnostics -lt $nr.IndexOf('RenderPreflight(config, PreflightView::Details);') -and
           -not $nr.Contains('renderReadouts(false);') -and
           $nr.Contains('StageUi::DimensionText(actualW, actualH, outputW, outputH)')) 'feature controls remain together and detailed status follows the signal board in Diagnostics'
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
Assert-Ui ($header.Contains('EscapeClosesMenu { false }') -and
           $config.Contains('readBool("Menu", "EscapeClosesMenu")') -and
           $config.Contains('ini.SetBoolValue("Menu", "EscapeClosesMenu", Instance()->EscapeClosesMenu.value_or_default())') -and
           $menu.Contains('Checkbox("Esc closes menu###Escape button closes menu", &escapeClosesMenu)') -and
           $menu.Contains('OptiInput::HandleEscapeClose(escapeClose, config->EscapeClosesMenu.value_or_default(),') -and
           $escapeUi.Contains('!capturingKey') -and $escapeUi.Contains('ImGui::IsAnyItemActive()') -and
           $escapeUi.Contains('ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopupId)')) 'optional Escape close persists and preserves key capture, text editing and popups'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderToolsPage.*?RenderScreenshotMenu\(ctx.config\)' -and
           ([regex]::Matches($menu, 'RenderScreenshotMenu\(ctx.config\)').Count -eq 1)) 'comparison screenshots have one home in Tools'
Assert-Ui ($menu.Contains('DlssNr::ExperimentalPolicy::Capture(*ctx.config).active') -and
           $shell.Contains('inline void NeuralNavigation(int& selected,bool main=false,bool multipass=false,bool overrides=false,bool inspector=false,') -and
           $shell.Contains('PilotReserve()') -and
           $shell.Contains('TabPilotLight(i==6?preflight:onState?PilotState::On:PilotState::Off)') -and
           $shell.Contains('TabPilotLight(preflight)') -and
           $pilot.Contains('PilotState::Degraded') -and $pilot.Contains('PilotState::Blocked') -and
           -not $shell.Contains('label=active?on:off') -and
           -not $menu.Contains('ImGui::TextColored(titleColor,')) 'feature and preflight tabs show reserved pilot lights with truthful active and unavailable states'
Assert-Ui ($header.Contains('MenuBrightness { 1.0f }') -and $menu.Contains('void MenuCommon::FinalizeFrame()') -and
           $config.Contains('readFloat("Menu", "Brightness")') -and $config.Contains('MenuBrightness.set_from_config(Neurotic::UiBrightness::Clamp(*brightness))') -and
           $menu.Contains('SliderFloat("UI Brightness", &brightness, Neurotic::UiBrightness::Minimum,') -and
           $menu.Contains('ctx.config->MenuBrightness = Neurotic::UiBrightness::Clamp(brightness);')) 'UI brightness is neutral by default, persistent through shared bounds, and applied only when finalizing UI draw colors'
Assert-Ui ($config.Contains('readString("Menu", "Language", true)') -and $config.Contains('ini.SetValue("Menu", "Language", Instance()->MenuLanguage.value_or_default().c_str());')) 'language loads and saves in Menu section'

Assert-Ui (-not $header.Contains('CustomOptional<bool> OptiClip') -and -not $menu.Contains('OptiClip::') -and -not $nr.Contains('OptiClip::') -and $config.Contains('Neurotic::MenuConfig::DiscardRetiredKeys(ini);')) 'OptiClip runtime is absent and legacy key retirement is wired into configuration'
