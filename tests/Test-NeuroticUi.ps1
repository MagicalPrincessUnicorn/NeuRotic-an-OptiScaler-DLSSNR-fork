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
Assert-Ui ($menu.Contains('BeginTabItem("Neural Rendering Multipass")') -and
           $menu.Contains('RenderNeuralRenderingMultipassPage')) 'Multipass is its own top-level tab'
$multipass = $nr.Substring($nr.IndexOf('void RenderMultipassMenu'))
Assert-Ui ($multipass.Contains('ScopedCollapsingHeader("Neural Rendering Multipass"')) 'Multipass page uses the requested full title'
Assert-Ui ($multipass.Contains('Checkbox("Enable NR Multipass"')) 'Multipass page uses the requested enable label'
Assert-Ui ($multipass.Contains('Combo("Pass count"') -and $multipass.Contains('"10 passes"')) 'pass count exposes the complete one-to-ten range'
Assert-Ui ($multipass.Contains('BeginTabBar("NrMultipassLayers"') -and
           $multipass.Contains('"Pass %u"')) 'pass count drives numbered pass tabs'
Assert-Ui ($multipass.Contains('Button("Reset All")') -and
           $multipass.Contains('BeginPopupModal("Reset all multipass settings?"') -and
           $multipass.Contains('Button("Confirm")') -and $multipass.Contains('Button("Cancel")')) 'Reset All requires confirm or cancel'
Assert-Ui ($multipass.Contains('for (unsigned int pass = 0; pass < 10; ++pass)') -and
           $multipass.Contains('DlssNrPasses = 1u')) 'Reset All restores all ten passes and the count'
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
Assert-Ui (-not ($nr -match 'renderSecondLayerControls\(\);')) 'legacy nested Multipass panel is no longer rendered in DLSS Neural Rendering'
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
