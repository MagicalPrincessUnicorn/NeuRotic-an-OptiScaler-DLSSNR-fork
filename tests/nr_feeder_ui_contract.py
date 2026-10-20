"""UI boundary guards for intake, diagnostics and the retained Basic mapping."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
menu = (root / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf-8')
stage = (root / 'OptiScaler/dlssnr/DlssNr_StageControls.h').read_text(encoding='utf-8')
intake = menu[menu.index('static void RenderIntakeMenu('):menu.index('void RenderMenu(Config*')]
assert 'ObservationRefresh::Update(refresh, IsVulkanInput(), check,' in intake
assert 'SetDlssNrEnabled' not in intake and 'StartAdvisor' not in intake
assert 'checkState.Request(refresh, completed ? completed->operation : 0)' in intake
assert 'latest ? latest->operation : 0, latest && latest->manual' in intake
assert 'requestedMenuPage = MenuPage::Overview' in intake
assert 'requestedMenuPage = MenuPage::Diagnostics' in intake
assert 'config->DlssNrPresentInputPolicy = inputPolicy;' in intake
assert stage.count('config.DlssNrPresentInputPolicy = inputPolicy;') == 2
assert 'const auto inputPolicy = config->DlssNrPresentInputPolicy;' in menu[menu.index('void RenderMenu(Config*'):]
assert 'BeginTooltip' not in menu and 'SetTooltip' not in menu
assert 'BeginTooltip' not in stage and 'SetTooltip' not in stage
assert 'RenderAdvisorRouteCard' not in menu and 'void RenderAdvisor(' not in menu
assert 'CancelAdvisorAnalysis(config, "Advisor archived; original settings restored.")' in menu
basic = menu[menu.index('if (!basic.advanced)'):menu.index('const unsigned int passCount = RenderPassCountSelector(config);')]
import subprocess
original = subprocess.check_output(['git', 'show', 'HEAD:OptiScaler/dlssnr/DlssNr_Menu.cpp'], cwd=root, text=True, encoding='utf-8')
original_basic = original[original.index('if (!basic.advanced)'):original.index('const unsigned int passCount = RenderPassCountSelector(config);')]
import re
maximum_row = re.compile(r'            static const char\* maximums\[\].*?            basic = config->DlssNrBasicMultipass.value_or_default\(\);\n', re.S)
# The latest placement request moves this complete row above Model Resolution.
# Preserve all remaining Basic code and every statement within the moved row.
assert maximum_row.findall(basic) == maximum_row.findall(original_basic)
assert maximum_row.sub('', basic) == maximum_row.sub('', original_basic), 'Basic mapping/IDs/commit semantics changed'
assert 'ImGui::BeginDisabled(!enabled)' not in menu
assert 'BeginNrCard("##NrAppearanceCard", "Model & composition")' in menu
print('PASS: real-owner intake, navigation, policy preservation, Diagnostics ownership and retained Basic mapping/IDs/commit semantics')
