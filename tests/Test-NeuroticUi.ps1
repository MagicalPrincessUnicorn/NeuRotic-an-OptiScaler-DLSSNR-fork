$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$menu = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Raw
$nr = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Menu.cpp') -Raw
$config = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.cpp') -Raw
$header = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/Config.h') -Raw
function Assert-Ui([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}
Assert-Ui ($menu -match '(?s)RenderMainMenuBottomBar\(ctx\);.*?RenderMainMenuGraphs\(ctx\);.*?RenderMainMenuHeaderMessages\(ctx\);.*?RenderMainMenuTabs\(ctx\);.*?RenderMainMenuSupportLink\(\);') 'actions, graphs, status, tabs, bottom-right support order'
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
Assert-Ui ($menu.Contains('https://ko-fi.com/espiownage')) 'approved Ko-fi destination'
Assert-Ui ($menu -match '(?s)Button\("Open Wiki"\).*?ShowHelpMarker\(.*?BeginCombo\("Language"') 'language control follows Wiki button'
Assert-Ui (-not $menu.Contains('Sorry for bad translation.')) 'translation apology removed from UI'
Assert-Ui ($menu -match '(?s)Text\("%d", currentFeature->FrameCount\(\)\);.*?SameLine.*?Text\("GPU: %s", primaryGpu.name.c_str\(\)\);') 'GPU name shares resolution row'
Assert-Ui ($menu -match '(?s)void MenuCommon::RenderMainMenuSupportLink\(\).*?Enjoying NeuRotic\?.*?Buy Me a Coffee.*?GetContentRegionAvail.*?SetCursorPosX.*?TextUnformatted\(prompt\).*?Button\(button\)') 'compact support prompt right-aligned in final row'
Assert-Ui ($header.Contains('MenuLanguage { "en" }')) 'English default'
Assert-Ui ($config.Contains('readString("Menu", "Language", true)') -and $config.Contains('ini.SetValue("Menu", "Language", Instance()->MenuLanguage.value_or_default().c_str());')) 'language loads and saves in Menu section'
