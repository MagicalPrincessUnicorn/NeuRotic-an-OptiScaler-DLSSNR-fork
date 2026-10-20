"""Acceptance checks for the explicitly requested overlay layout follow-up."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
menu=(r/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf8')
def body(name):
 start=menu.index('void MenuCommon::'+name+'(')
 end=menu.find('\nvoid MenuCommon::',start+1)
 return menu[start:end if end>=0 else len(menu)]
general=body('RenderGeneralPage');tools=body('RenderToolsPage');advanced=body('RenderAdvancedPage')
assert 'if (ctx.childPage == 1) RenderKeybindSettings' in general and 'if (ctx.childPage == 2)' in general
assert 'UI brightness' in general and 'UI brightness' not in tools
assert 'RenderVsyncSettings' not in general and 'RenderAnisotropicFilteringSettings' not in general
assert 'RenderVsyncSettings' in advanced and 'RenderAnisotropicFilteringSettings' in advanced
assert 'NeuralNavigation' in body('RenderMainMenuTabs')
assert body('RenderMainMenuTabs').index('NeuralNavigation')<body('RenderMainMenuTabs').index('BeginChild'), 'child tab row must be outside scroll content'
assert 'FooterControls' in body('RenderMainMenuSupportLink') and 'actions.scaleChanged' not in body('RenderMainMenuWindowActions')
assert 'ScopedCollapsingHeader' not in general and 'if (ctx.childPage == 0)' in general, 'General now uses direct child pages'
print('PASS: requested page assignments, control order, pinned sub-tabs and footer Scaling placement')

fg=body('RenderFrameGenerationSelection')
assert fg.index('BeginCombo("Override DLSSG Ratio"')<fg.index('Checkbox("Unlock DLSS MFG above 2X (RTX 40 series)"')
nr=(r/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf8')
assert 'if (page == MenuPage::Multipass)' in nr and 'if (page == MenuPage::Diagnostics)' in nr
assert 'if (BasicMultipass::Active(uiConfig)) BasicMultipass::Derive(uiConfig);' in nr
assert 'ScopedCollapsingHeader("Advanced Diagnostics & Information")' not in nr
assert 'multipassTitle.c_str()' not in nr
assert 'ScopedCollapsingHeader("Advanced Neural Rendering Features##NrFeatures")' in nr
print('PASS: MFG order, effective-profile diagnostics, direct child content and retained advanced dropdown')

shell=(r/'OptiScaler/menu/SleekShell.h').read_text(encoding='utf8')
nav=shell[shell.index('inline void NeuralNavigation'):shell.index('inline float FooterScaleWidth')]
assert nav.index('"Main"')<nav.index('"Multipass"')<nav.index('"Inspector"')<nav.index('"Diagnostics"')<nav.index('"Overrides"')
assert 'TextColored(titleColor' not in body('RenderNeuralRenderingExperimentalSettings')
print('PASS: General defaults, requested child order and no duplicate Overrides status')

assert 'if (i==0 || i==1 || i==4)' in nav and 'if (onState) ImGui::PushStyleColor(ImGuiCol_Text' in nav
main_tuning=nr[nr.index('BeginTable("##NrMainTuning"'):nr.index('bool autoMask = config->DlssNrAutoMask')]
assert 'BordersInnerV' not in main_tuning and 'NrTuningReset' not in main_tuning and 'TableSetColumnIndex(2)' not in main_tuning
assert 'ImGui::TextWrapped("Model resolution changes only' in nr
assert 'timingActive && !vulkan,nrTelemetry.totalGpuMs' in nr
assert 'const auto nativeTime=vulkan ? nrTelemetry.totalGpuMs : retainedTime;' in nr
assert 'nrTelemetry.workWidth,nrTelemetry.workHeight,nrTelemetry.gameResets' in nr
assert 'nrTelemetry.featureBuilds,nrTelemetry.featureRebuilds' in nr
assert 'InlineSliderWidth(label)' in nr
print('PASS: colored Main/status tabs, borderless tuning, visible explanation and bounded native timing binding')
