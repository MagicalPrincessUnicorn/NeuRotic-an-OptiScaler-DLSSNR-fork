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
Assert-Ui ($nr.Contains('Combo("Passes"') -and $nr.Contains('"Standard (1 pass)"') -and $nr.Contains('"10 passes"')) 'shared pass selector exposes the complete one-to-ten range'
Assert-Ui ($nr.Contains('More than one pass selected. Enable NR Multipass for multiple passes to be applied.') -and
           $nr.Contains('BeginChild("##DlssNrMultipassInactiveWarning"') -and
           $nr.Contains('ImGuiCol_ChildBg') -and $nr.Contains('ImGuiCol_Border') -and
           $multipass -notmatch 'BeginDisabled\(\);\s*if \(ImGui::BeginTabBar\("NrMultipassLayers"') 'pass profiles remain configurable before Multipass is enabled'
Assert-Ui ($multipass.Contains('BeginTabBar("NrMultipassLayers"') -and
           $multipass.Contains('"Pass %u"')) 'pass count drives numbered pass tabs'
Assert-Ui ($multipass.Contains('Button("Reset All")') -and
           $multipass.Contains('BeginPopupModal("Reset all multipass settings?"') -and
           $multipass.Contains('Button("Confirm")') -and $multipass.Contains('Button("Cancel")')) 'Reset All requires confirm or cancel'
Assert-Ui ($multipass.Contains('for (unsigned int pass = 1; pass < 10; ++pass)') -and
           -not $multipass.Contains('DlssNrPasses = 1u') -and
           $multipass.Contains('pendingAdditionalPassScale = -1;') -and
           $multipass.Contains('pendingScales.clear();')) 'Reset All restores all additional profiles, cancels pending resolution edits, and preserves baseline Pass 1 and the shared pass count'
Assert-Ui ($multipass.Contains('Button("Reset this pass")') -and
           $multipass.Contains('Reset Pass %u profile?##pass%u') -and
           $multipass -match '(?s)ResetPassOptions\(pass\);\s*pendingScales\.erase\(scaleId\);') 'each selected pass has a confirmed profile reset that cancels its pending resolution edit'
Assert-Ui ($multipass.Contains('Copy Pass %u settings') -and
           $multipass -match '(?s)CopyPassOptions\(PassOptions\(config, index - 1\), pass\);\s*pendingScales\.erase\(scaleId\);') 'later passes can copy the preceding profile without a stale pending resolution edit overwriting it'
Assert-Ui ($multipass.Contains('SliderInt("Additional pass model resolution"') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)') -and
           $multipass.Contains('Reset##AdditionalPassModelResolution')) 'additional passes share an optional model-resolution slider without changing pass 1'
Assert-Ui ($multipass.Contains('if (passCount == 1)') -and
           $multipass.Contains('Pass 1 is configured in the main Neural Rendering section.') -and
           $multipass.Contains('for (unsigned int index = 1; index < passCount; ++index)')) 'baseline Pass 1 has one home and Multipass exposes only additional passes'
foreach ($label in @('Model resolution##pass%u', 'Downscaler##pass%u', 'Model preset##pass%u',
    'Style##pass%u', 'Enlargement##pass%u', 'Detail strength##pass%u',
    'Colour strength##pass%u', 'Highlight guard##pass%u', 'Intensity##pass%u',
    'Local structure##pass%u', 'Local tone##pass%u', 'Skin structure##pass%u',
    'Auto skin mask##pass%u', 'Proxy composition##pass%u', 'Apply the model##pass%u')) {
    Assert-Ui ($multipass.Contains($label)) "$label is rendered for every pass tab"
}
Assert-Ui (([regex]::Matches($multipass, 'HelpMarker\(')).Count -ge 18) 'every Multipass setting carries a hover hint'
Assert-Ui ($multipass.Contains('Reset##pass%u-resolution') -and
           $multipass.Contains('Reset##pass%u-auto-mask') -and
           $multipass.Contains('Reset##pass%u-apply')) 'every Multipass setting family exposes an individual reset'
Assert-Ui (-not $nr.Contains('renderSecondLayerControls') -and
           -not $nr.Contains('Enable second neural-rendering layer') -and
           ([regex]::Matches($nr, 'DlssNrMultipassSection')).Count -eq 1) 'legacy two-layer editor is removed and the Multipass header has one stable ImGui ID'
Assert-Ui ($nr -match '(?s)DlssNrSecondLayer\s*=\s*config->DlssNrMultipassEnabled\.value_or_default\(\)\s*&&\s*passCountIndex >= 1;' -and
           $multipass -match '(?s)DlssNrSecondLayer\s*=\s*enabled\s*&&\s*config->DlssNrPasses\.value_or_default\(\) > 1;') 'legacy second-layer compatibility state follows both the Multipass switch and selected pass count'
Assert-Ui ($menu.Contains('https://ko-fi.com/espiownage')) 'approved Ko-fi destination'
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
Assert-Ui ($nr.Contains('Present Image-Only is active. NR is processing the final image before it reaches the display.') -and $nr.Contains('This game’s present target is not supported yet. Your image is unchanged.') -and $nr.Contains('Use Native Temporal when it is available.')) 'Present compatibility active, safe-fallback, and recommendation messages are visible'
Assert-Ui ($nr.IndexOf('ImGui::Combo("Present workload"') -gt $nr.IndexOf('ImGui::Combo("NR route"') -and $nr.IndexOf('ImGui::Combo("Present workload"') -lt $nr.IndexOf('ImGui::Combo("Rendering mode"')) 'Present workload is directly below NR route selection'
Assert-Ui ($nr.Contains('Present history: %s | uninterrupted output frames %llu') -and $nr.Contains('Reset reason: %s | last interruption: %s')) 'Present history diagnostics are visible'
Assert-Ui ($header.Contains('MenuLanguage { "en" }')) 'English default'
Assert-Ui ($config.Contains('readString("Menu", "Language", true)') -and $config.Contains('ini.SetValue("Menu", "Language", Instance()->MenuLanguage.value_or_default().c_str());')) 'language loads and saves in Menu section'
