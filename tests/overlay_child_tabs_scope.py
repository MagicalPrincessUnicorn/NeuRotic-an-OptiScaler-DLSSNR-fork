"""Requested child navigation and direct section content; no runtime setting changes."""
from pathlib import Path
r=Path(__file__).resolve().parents[1]
s=(r/'OptiScaler/menu/SleekShell.h').read_text(encoding='utf8')
m=(r/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf8')
n=(r/'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text(encoding='utf8')
assert 'inline void SectionNavigation(int page,int& selected)' in s
assert 'ChildPageCount(selected)' in m
assert 'if (ctx.childPage == 0)' in m
for label in ('Advanced Settings','V-Sync Settings','Anisotropic Filtering','Resource Barriers','Root Signatures'):
 assert f'ScopedCollapsingHeader("{label}"' not in m, label
colour=n[n.index('Colour##DlssNrColourSection'):n.index('// Logarithmic, because')]
assert 'ImGui::TextWrapped("The model was trained' in colour
assert 'TextDisabled' not in colour and r"frames. The upscaler's\n" not in colour
for name in ('Keybinds','Menu Theme and Color','Logging','FPS Overlay','Magnifier','Mipmap Bias','Comparison screenshots'):
 assert f'ScopedCollapsingHeader("{name}"' not in m+n, name
print('PASS: requested child navigation, exposed Advanced groups and full-width Colour introduction')

import subprocess,re,collections
old=subprocess.check_output(['git','-C',str(r),'show','c9ac9297:OptiScaler/menu/menu_common.cpp']).decode('utf8')
# The revised wording retains the same Escape toggle.
old=old.replace('Escape closes menu','Escape button closes menu')
# Reorganizing pages must retain every original control and settings-writing expression.
controls=r'ImGui::(?:Checkbox|SliderFloat|SliderInt|Combo|BeginCombo|Button|SmallButton|InputFloat|InputInt)\("[^"\n]*"'
expected_controls=collections.Counter(re.findall(controls,old))
for family in ('30','20'):
 expected_controls[f'ImGui::Checkbox("Experimental MFG unlock for RTX {family} series###ExperimentalMfgRTX{family}"'] += 1
assert expected_controls == collections.Counter(re.findall(controls,m)), 'existing controls or approved MFG additions changed'
writes=r'(?:ctx\.)?config->\w+\s*(?:=(?!=)[^;]+;|\.reset\(\);)'
expected_writes=collections.Counter(re.findall(writes,old))
for family in ('30','20'):
 expected_writes[f'config->FGDLSSGExperimentalUnlockRTX{family} = rtx{family};'] += 1
assert expected_writes == collections.Counter(re.findall(writes,m)), 'configuration writes or approved MFG additions changed'
assert m.index('SectionNavigation(selected,ctx.childPage)')<m.index('BeginChild("##MainMenuPageContent"')
assert 'else if (ChildPageCount(selected)>0) ImGui::PushID(ctx.childPage)' in m
print('PASS: original controls/configuration writes retained; child navigation pinned with independent identities')
