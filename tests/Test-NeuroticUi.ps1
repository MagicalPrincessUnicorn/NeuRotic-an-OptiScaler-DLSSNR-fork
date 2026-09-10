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
Assert-Ui ($nr -match '(?s)renderSecondLayerControls\(\);\s*ImGui::PopItemWidth\(\);\s*ImGui::PopTextWrapPos\(\);') 'second-layer controls last in NR section'
$layer2Match = [regex]::Match($nr, '(?s)const auto renderSecondLayerControls = \[&\]\(\)\s*\{(.*?)\n\s*\};')
Assert-Ui ($layer2Match.Success) 'second-layer control block is present'
$layer2 = $layer2Match.Groups[1].Value
Assert-Ui (([regex]::Matches($nr, 'ScopedCollapsingHeader\("Multipass##DlssNrMultipassSection"\)')).Count -eq 1) 'one default-collapsed Multipass parent is present'
Assert-Ui (-not $nr.Contains('Second-pass settings (experimental)')) 'obsolete nested second-pass panel is absent'
Assert-Ui ($layer2 -match '(?s)^\s*if \(auto panel = ScopedCollapsingHeader\("Multipass##DlssNrMultipassSection"\).*?Checkbox\("Enable second neural-rendering layer".*?Independent layer-2 tuning\..*?Model resolution##layer2.*?Apply the model##layer2') 'Multipass contains the toggle, warning, and every second-pass control'
Assert-Ui ($layer2 -match '(?s)const bool disableSettings = !secondLayer \|\| !d3d12;\s*if \(disableSettings\)\s*ImGui::BeginDisabled\(\);.*?if \(disableSettings\)\s*ImGui::EndDisabled\(\);') 'subordinate Multipass settings are disabled while off or unsupported'
Assert-Ui ($layer2.IndexOf('Checkbox("Enable second neural-rendering layer"') -lt $layer2.IndexOf('const bool disableSettings')) 'D3D12 toggle remains outside subordinate disabled state'
Assert-Ui ($layer2 -match '(?s)SliderInt\("Model resolution##layer2".*?\)\s*pendingLayer2Scale = scale;\s*if \(ImGui::IsItemDeactivatedAfterEdit\(\) && pendingLayer2Scale >= 0\)\s*\{\s*config->DlssNrSecondLayerWorkingScale =\s*std::clamp\(pendingLayer2Scale, 25, 200\) / 100\.0f;\s*pendingLayer2Scale = -1;') 'layer-2 model resolution commits only after slider deactivation'
Assert-Ui (([regex]::Matches($layer2, 'config->DlssNrSecondLayerWorkingScale\s*=')).Count -eq 2) 'layer-2 model resolution has only release and Reset writes'
Assert-Ui ($layer2 -match '(?s)SmallButton\("Reset##NrLayer2ModelResolution"\).*?DlssNrSecondLayerWorkingScale = 1\.0f;\s*pendingLayer2Scale = -1;\s*scale = 100;') 'layer-2 model resolution reset restores 100 percent and clears pending state'
foreach ($reset in @(
    @{ Id = 'Reset##layer2-detail'; Field = 'DlssNrSecondLayerTransferStrength'; Value = '1.0f' },
    @{ Id = 'Reset##layer2-colour'; Field = 'DlssNrSecondLayerColourStrength'; Value = '1.0f' },
    @{ Id = 'Reset##layer2-guard'; Field = 'DlssNrSecondLayerMaxRatio'; Value = '2.0f' }
)) {
    $pattern = 'SmallButton\("' + [regex]::Escape($reset.Id) + '"\)\)\s*config->' + $reset.Field + ' = ' + [regex]::Escape($reset.Value) + ';'
    Assert-Ui ($layer2 -match $pattern) "$($reset.Id) restores $($reset.Value)"
}
$layer2Labels = @('Model resolution##layer2', 'Downscaler##layer2', 'Enlargement##layer2', 'Model preset##layer2', 'Style##layer2', 'Detail strength##layer2', 'Colour strength##layer2', 'Highlight guard##layer2', 'Intensity##layer2', 'Local structure##layer2', 'Local tone##layer2', 'Skin structure##layer2', 'Auto skin mask##layer2', 'Apply the model##layer2')
foreach ($label in $layer2Labels) {
    Assert-Ui ($layer2.Contains($label)) "$label is inside Multipass"
}
$layer2Tooltips = @(
    'The working resolution of layer 2 only.',
    "The filter that averages only layer 2's above-native model answer",
    "The second model session's preset.",
    'The processing profile used by layer 2 only.',
    "How layer 2's edit returns to full size",
    "How far the composed frame moves toward layer 2's picture.",
    "How much of layer 2's colour accompanies its lighting edit.",
    'The maximum brightness change layer 2 may apply',
    "Layer 2's internal model strength.",
    "Layer 2's internal local-structure strength.",
    "Layer 2's internal local-tone strength.",
    "Layer 2's skin-structure strength.",
    'Lets the second model session identify skin',
    'Off keeps layer 2 evaluating but hides only its edit'
)
foreach ($tooltip in $layer2Tooltips) {
    Assert-Ui ($layer2.Contains($tooltip)) "layer-2 tooltip is present: $tooltip"
}
$firstPassFields = @('DlssNrWorkingScale', 'DlssNrScalingDownscaler', 'DlssNrPreset', 'DlssNrStyle', 'DlssNrTransfer', 'DlssNrTransferStrength', 'DlssNrColourStrength', 'DlssNrMaxRatio', 'DlssNrIntensity', 'DlssNrLocalStructure', 'DlssNrLocalTone', 'DlssNrSkinStructure', 'DlssNrAutoMask', 'DlssNrApplyModel')
foreach ($field in $firstPassFields) {
    Assert-Ui (-not $layer2.Contains('config->' + $field)) "Multipass never accesses first-pass field $field"
}
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
