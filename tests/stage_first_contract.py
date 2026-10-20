"""Preservation gates for the approved child preview; no network or game required."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
base = 'b77ed7dd3d0812a7c48b768faf7b9b9232b29692'
def original(path):
    return subprocess.check_output(['git', '-C', str(root), 'show', f'{base}:{path}'])

for path in ['OptiScaler.ini', 'integration/OptiScaler.ini',
             'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl',
             'OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp']:
    assert (root / path).read_text(encoding='utf-8').replace('\r\n', '\n') == \
        original(path).decode('utf-8').replace('\r\n', '\n'), path

# DX11 adds independently sized guides and producer proof to the accepted Wilds observer.
# Its provider-frame selection and ordering gates remain intact across that extension.
guides = 'OptiScaler/dlssnr/DlssNr_PresentGuides.h'
new_guides = (root / guides).read_text()
for gate in ['metadata.providerFrame = providerFrame;', 'next.providerFrame = providerFrame;',
             '((slot.providerFrame || providerFrame) && slot.providerFrame != providerFrame)',
             '((selection.providerFrame || providerFrame) && selection.providerFrame != providerFrame)',
             'providerFrame ? GpuSafety::OrderBefore(slot.producer, queue)',
             'providerFrame ? GpuSafety::OrderBefore(selection.producer, queue)']:
    assert gate in new_guides, gate

old = original('OptiScaler/Config.cpp').decode()
new = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
def block(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0].replace('\r\n', '\n')
assert block(old, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config') == block(
    new, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config')
assert block(old, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"') == block(
    new, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"')
assert block(old, 'static inline bool isInteger', 'Config::Config()') == block(new, 'static inline bool isInteger', 'Config::Config()')
stage = (root / 'OptiScaler/dlssnr/DlssNr_StageControls.h').read_text()
adapter = (root / 'OptiScaler/dlssnr/DlssNr_StageUi.h').read_text()
assert 'SentenceCombo("##NrStage"' not in stage
assert 'SelectUnifiedMethod(config, method)' in stage
assert 'SelectUnifiedResolution(config, choice)' in stage
assert 'SelectUnifiedResolution(C& c, int choice)' in adapter
print('PASS: unchanged control INIs, composition shader and Vulkan renderer; provider-frame gates, legacy config precedence, and two-method UI projection retained.')
