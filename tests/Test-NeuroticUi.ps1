$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$menu = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Raw
$nr = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Menu.cpp') -Raw
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
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderDiagnosticsPage.*?RenderLoggingSettings\(ctx\);\s*RenderQuirksSettings\(ctx\);\s*RenderFpsOverlaySettings\(ctx\);') 'logging first in Diagnostics'
foreach ($label in @('Advanced Settings', 'Logging')) {
    Assert-Ui ($menu.Contains('ScopedCollapsingHeader("' + $label + '", ImGuiTreeNodeFlags_DefaultOpen)')) "$label starts expanded"
}
Assert-Ui ($nr.Contains('ScopedCollapsingHeader("DLSS Neural Rendering", ImGuiTreeNodeFlags_DefaultOpen)')) 'NR starts expanded and remains collapsible'
Assert-Ui (-not $nr.Contains('Can be toggled with a key -- bind it under Keybinds')) 'redundant NR keybind description removed'
Assert-Ui ($nr -match '(?s)SmallButton\("Reset##NrModelResolution"\).*?DlssNrWorkingScale = 1.0f;\s*pendingScale = -1;\s*scalePercent = 100;') 'model resolution reset restores 100 percent and cancels pending change'
Assert-Ui (-not $menu.Contains('BeginTabItem("Neural Rendering Multipass")') -and
           -not $menu.Contains('RenderNeuralRenderingMultipassPage')) 'Multipass is contained within Neural Rendering'
$multipass = $nr.Substring($nr.IndexOf('static void RenderMultipassMenu'))
Assert-Ui ($nr.Contains('RenderMultipassMenu(config, menuResScale);') -and
           $multipass.Contains('ScopedCollapsingHeader("Neural Rendering Multipass##DlssNrMultipassSection")')) 'Multipass is its own collapsible section beneath Neural Rendering'
Assert-Ui ($multipass.Contains('Checkbox("Enable NR Multipass"')) 'Multipass page uses the requested enable label'
Assert-Ui ($nr.Contains('static unsigned int RenderPassCountSelector(Config* config)') -and
           ([regex]::Matches($nr, 'RenderPassCountSelector\(config\)')).Count -eq 2 -and
           $nr.Contains('Combo("Passes"') -and $nr.Contains('"Standard (1 pass)"') -and
           $nr.Contains('"10 passes"')) 'one shared pass selector exposes the complete one-to-ten range in both Neural Rendering sections'
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
Assert-Ui ($multipass.Contains('sharedSlider("Model Resolution##AdditionalPassModelResolution"') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)') -and
           $nr.Contains('const std::string resetId = std::string("Reset##") + label;')) 'additional passes share an optional model-resolution slider without changing pass 1'
Assert-Ui ($multipass -match '(?s)sharedSlider\("Model Resolution##.*?sharedSlider\("Model Strength##.*?sharedSlider\("Detail Strength##.*?BeginTabBar\("NrMultipassLayers"') 'shared resolution, model strength and detail strength precede child tabs'
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
Assert-Ui ($nr -match '(?s)Checkbox\("Enable Neural Rendering".*?NoteNrUserToggle\(\)') 'NR checkbox contributes to the shared user-toggle burst'
Assert-Ui ($menu -match '(?s)inputDlssNr.*?SetDlssNrEnabled\(enabled\);\s*DlssNr::NoteNrUserToggle\(\);') 'NR hotkey contributes to the same user-toggle burst'
Assert-Ui (($nr | Select-String -Pattern 'NoteNrUserToggle\(\)' -AllMatches).Matches.Count -eq 2) 'shared tracker has one definition and one checkbox call'
Assert-Ui (($menu | Select-String -Pattern 'NoteNrUserToggle\(\)' -AllMatches).Matches.Count -eq 1) 'hotkey is the only shared-tracker call outside the NR menu'
$burst = [regex]::Match($notes, '(?s)ToggleBurstMessages\s*=\s*\{(.*?)\};').Groups[1].Value
Assert-Ui (([regex]::Matches($burst, '(?m)^\s*"')).Count -eq 41 -and $burst.Contains('ZZZZZZZzzzzzzzzzzzz')) 'all 41 approved burst notes are present'
Assert-Ui ($nr.Contains('"%s is active."') -and $nr.Contains('"Image unchanged. %s"') -and -not $nr.Contains('Use Native Temporal when it is available.')) 'Present active and actionable safe-fallback guidance without stale recommendation'
Assert-Ui ($nr.IndexOf('ImGui::Combo("NR resolution"') -gt $nr.IndexOf('ImGui::Combo("NR route"') -and $nr.IndexOf('ImGui::Combo("NR resolution"') -lt $nr.IndexOf('ImGui::Combo("Rendering mode"') -and -not $nr.Contains('ImGui::Combo("Present workload"')) 'NR resolution replaces workload below route selection'
Assert-Ui ($nr.Contains('if (resolution == PresentResolution::Custom)') -and $nr.Contains('"Present Enhanced"') -and $nr.Contains('DlssNrEnhancedCustomScale')) 'three routes and conditional independent custom scale are present'
Assert-Ui ($nr.Contains('Present history: %s | uninterrupted output frames %llu') -and $nr.Contains('Reset reason: %s | last interruption: %s')) 'Present history diagnostics are visible'
Assert-Ui ($nr.IndexOf('Checkbox("Enable Neural Rendering"') -lt $nr.IndexOf('Combo("NR route"')) 'enable is the first NR control'
Assert-Ui ($nr -match '(?s)if \(!presentRoute\)\s*\{\s*if \(ImGui::Combo\("Rendering mode".*?HelpMarker\("Quality keeps NR.*?\n\s*\}') 'Native rendering mode and help hidden on both Present routes'
$diagnostics = $nr.IndexOf('ScopedCollapsingHeader("Advanced Data / Diagnostics##NrDiagnostics")')
Assert-Ui ($diagnostics -gt $nr.IndexOf('"Image unchanged. %s"') -and
           $diagnostics -lt $nr.IndexOf('"Native capture calls %llu"') -and
           $diagnostics -lt $nr.IndexOf('"Present history:')) 'useful status before collapsed diagnostics; counters and history inside'
Assert-Ui ($nr.Contains('observation.Fresh(selection,') -and $nr.Contains('NR: unavailable | Output: unavailable')) 'new selection waits for fresh telemetry; unknown sizes are explicit'
Assert-Ui ($header.Contains('DlssNrRoute { 2 }') -and $header.Contains('DlssNrEnhancedResolution { 0 }') -and
           $header.Contains('DlssNrEnabled { false }') -and $header.Contains('LogToFile { false }')) 'Enhanced Follow defaults do not enable NR or file logging'
Assert-Ui ($menu.Contains('bool toFile = config->LogToFile.value_or_default(); ImGui::Checkbox("To File", &toFile)') -and
           $menu.Contains('config->LogToFile = toFile;') -and $config.Contains('LogToFile.set_from_config(readBool("Log", "LogToFile"))')) 'existing file logging checkbox uses effective config and keeps deliberate changes'
foreach ($iniPath in @('OptiScaler.ini', 'integration/OptiScaler.ini')) {
    $ini = Get-Content -Raw -LiteralPath (Join-Path $root $iniPath)
    Assert-Ui ($ini -match '(?m)^LogToFile\s*=\s*false\s*$') "$iniPath explicitly disables file logging"
}
Assert-Ui ($header.Contains('MenuLanguage { "en" }')) 'English default'
Assert-Ui ($config.Contains('readString("Menu", "Language", true)') -and $config.Contains('ini.SetValue("Menu", "Language", Instance()->MenuLanguage.value_or_default().c_str());')) 'language loads and saves in Menu section'
