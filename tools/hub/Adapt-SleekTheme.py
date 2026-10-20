"""Adapt the exact accepted pure theme leaf; retain its palette and attribution."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[2]
source=subprocess.check_output(['git','-C','C:/NeuRotic/nr-experimental','show','40d010688961a4f661b4bea8904a2b92d0741c0b:OptiScaler/menu/SleekTheme.h']).decode('utf-8')
source=source.replace('#pragma once\n#include "SleekShell.h"','#include "imgui.h"\n#include <algorithm>')
source=source.replace('namespace Neurotic::Sleek','namespace nh').replace('template<class C> void ApplyTheme(C* conf)','void ApplySharedTheme(bool lightTheme)')
source=source.replace('    bool lightTheme = conf->LightTheme.value_or_default();','    style = ImGuiStyle{};\n    style.WindowRounding=16;style.ChildRounding=12;style.FrameRounding=8;style.PopupRounding=12;\n    style.TabRounding=7;style.ScrollbarRounding=12;style.GrabRounding=12;\n    style.WindowPadding={16,16};style.FramePadding={12,6};style.ItemSpacing={10,9};style.CellPadding={10,8};\n    style.WindowTitleAlign={0,.5f};style.WindowBorderSize=style.ChildBorderSize=style.PopupBorderSize=1;\n    style.FrameBorderSize=0;style.ScrollbarSize=9;style.GrabMinSize=12;')
for prefix,values in [('MenuAccentColor',[.36,.62,.98]),('MenuBGColor',[0,0,0])]:
    for channel,value in zip('RGB',values):
        source=source.replace(f'conf->{prefix}{channel}.value_or_default()',f'{value:.2f}f').replace(f'conf->{prefix}{channel}.has_value()','false')
source=source.replace('conf->MenuBGColorA.value_or_default()','1.0f')
assert 'conf->' not in source
source='// Adapted from accepted sleek UI 40d010688961a4f661b4bea8904a2b92d0741c0b.\n// Pure desktop theme; original Moonlight/Madam-Herta attribution retained.\n'+source
(root/'apps/NeuRoticHub/ui/SharedUiAdapter.cpp').write_text(source,encoding='utf-8')
