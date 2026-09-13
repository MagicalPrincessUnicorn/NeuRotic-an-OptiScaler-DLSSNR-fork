"""Preservation gates for the approved child preview; no network or game required."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
base = '3a6f34d32d4d0728560014b65d42bd9bfc7a5c6e'
def original(path):
    return subprocess.check_output(['git', '-C', str(root), 'show', f'{base}:{path}'])

for path in ['OptiScaler.ini', 'integration/OptiScaler.ini',
             'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl',
             'OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp']:
    assert (root / path).read_bytes() == original(path), path

# DX11 adds independently sized guides and producer proof to the accepted Wilds observer.
# Its provider-frame selection and ordering gates remain intact across that extension.
guides = 'OptiScaler/dlssnr/DlssNr_PresentGuides.h'
old_guides = subprocess.check_output(
    ['git', '-C', str(root), 'show', '6c36057a8f107e45c2a1d8eaa14caa7e54e7c31b:' + guides]).decode()
new_guides = (root / guides).read_text()
for gate in ['metadata.providerFrame = providerFrame;', 'next.providerFrame = providerFrame;',
             'providerFrame ? slot.providerFrame != providerFrame : slot.backbuffer != backbuffer',
             'providerFrame ? selection.providerFrame != providerFrame : selection.backbuffer != backbuffer',
             'providerFrame ? GpuSafety::OrderBefore(slot.producer, queue)',
             'providerFrame ? GpuSafety::OrderBefore(selection.producer, queue)']:
    assert gate in old_guides and gate in new_guides, gate

old = original('OptiScaler/Config.cpp').decode()
new = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
def block(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0].replace('\r\n', '\n')
assert block(old, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config') == block(
    new, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config')
assert block(old, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"') == block(
    new, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"')
assert block(old, 'static inline bool isInteger', 'Config::Config()') == block(new, 'static inline bool isInteger', 'Config::Config()')
print('PASS: unchanged control INIs, composition shader and Vulkan renderer; Wilds provider-frame gates and legacy placement precedence retained. Native DX11 guide transport is an approved extension.')
