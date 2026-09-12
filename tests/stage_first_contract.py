"""Preservation gates for the approved child preview; no network or game required."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
base = '3a6f34d32d4d0728560014b65d42bd9bfc7a5c6e'
def original(path):
    return subprocess.check_output(['git', '-C', str(root), 'show', f'{base}:{path}'])

for path in ['OptiScaler.ini', 'integration/OptiScaler.ini',
             'OptiScaler/dlssnr/DlssNr_PresentGuides.h',
             'OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl',
             'OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp']:
    assert (root / path).read_bytes() == original(path), path

old = original('OptiScaler/Config.cpp').decode()
new = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
def block(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0].replace('\r\n', '\n')
assert block(old, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config') == block(
    new, 'auto performanceMode =', 'DlssNrPreDlaa.set_from_config')
assert block(old, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"') == block(
    new, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"')
assert block(old, 'static inline bool isInteger', 'Config::Config()') == block(new, 'static inline bool isInteger', 'Config::Config()')
print('PASS: unchanged control INIs, guide capture, composition shader and Vulkan renderer; legacy placement precedence and parsing retained. Present resolution and DX12 multipass are approved experimental changes.')
