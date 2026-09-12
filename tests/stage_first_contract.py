"""Exact 0.9.5 preservation gates for the stage-first UI; no network or game required."""
from pathlib import Path
import subprocess

root = Path(__file__).resolve().parents[1]
base = '00dbd0bc5f60f84a759cfa9cd9dcc081327f96c7'
def original(path):
    return subprocess.check_output(['git', '-C', str(root), 'show', f'{base}:{path}'])

for path in ['OptiScaler.ini', 'integration/OptiScaler.ini',
             'OptiScaler/dlssnr/DlssNr_PresentResolution.h',
             'OptiScaler/dlssnr/DlssNr_Present.cpp',
             'OptiScaler/dlssnr/DlssNr_PresentGuides.h',
             'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp',
             'OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp']:
    assert (root / path).read_bytes() == original(path), path

old = original('OptiScaler/Config.cpp').decode()
new = (root / 'OptiScaler/Config.cpp').read_text(encoding='utf-8')
def block(text, start, end):
    return text.split(start, 1)[1].split(end, 1)[0].replace('\r\n', '\n')
assert block(old, 'if (auto route = readUInt("DlssNr", "Route"))', 'DlssNrPreDlaa.set_from_config') == block(
    new, 'if (auto route = readUInt("DlssNr", "Route"))', 'DlssNrPreDlaa.set_from_config')
assert block(old, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"') == block(
    new, 'const int renderingMode = std::clamp(Instance()', 'ini.SetValue("DlssNr", "PreDlaa"')
assert block(old, 'static inline bool isInteger', 'Config::Config()') == block(new, 'static inline bool isInteger', 'Config::Config()')
print('PASS: byte-identical control INIs, resolution math, guide capture and renderer implementations; exact legacy parsing, route/placement precedence and alias serialization retained')
