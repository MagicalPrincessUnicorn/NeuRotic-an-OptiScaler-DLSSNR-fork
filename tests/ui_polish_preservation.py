"""Guard save actions and non-UI source against scope drift."""
from pathlib import Path
import re, subprocess

root=Path(__file__).resolve().parents[1]
def git(*args):
    return subprocess.check_output(['git','-C',str(root),*args])
def old(path): return git('show','f9e62261:'+path).decode('utf-8').replace('\r\n','\n')
def current(path): return (root/path).read_text(encoding='utf-8')
def section(text,start,end): return text[text.index(start):text.index(end,text.index(start))]

path='OptiScaler/menu/menu_common.cpp'
before,after=old(path),current(path)
# The 2026-10-05 equal graph layout is user-approved. Actual production plot
# dimensions and missing-data states are checked by menu_graph_compatibility_geometry.py.
save_start='    const auto saveAllSettings = [&]()'
save_after=section(after,save_start,'    if (actions.saveClicked)')
save_after=re.sub(r'const bool saved=(DlssNr::ExperimentalPolicy::SaveDraft[\s\S]+?);\s*if \(saved\)',r'if (\1)',save_after)
save_after=save_after.replace('        Neurotic::Sleek::CompleteButtonFeedback(actions.saveId,saved);\n','')
assert re.sub(r'\s+',' ',section(before,save_start,'    if (ImGui::Button("Save Settings"))'))==re.sub(r'\s+',' ',save_after)
event_start='        DlssNr::ExperimentalPolicy::EnsureDraft(*config);'
old_event=section(before[before.index('    if (ImGui::Button("Save Settings"))'):],event_start,
    '    Neurotic::Sleek::ContinueRow(Neurotic::Sleek::ButtonWidth("Close"),6);')
new_event=section(after[after.index('    if (actions.saveClicked)'):],event_start,'    if(actions.closeClicked)')
new_event=re.sub(r'    if\(Neurotic::Sleek::ButtonFeedbackPending[\s\S]+?CancelButtonFeedback\(actions.saveId\);\n','',new_event)
assert re.sub(r'\s+',' ',old_event).strip()==re.sub(r'\s+',' ',new_event).strip()

allowed={'OptiScaler/Config.cpp','OptiScaler/Config.h','OptiScaler/OptiScaler.vcxproj','OptiScaler/OptiScaler.vcxproj.filters',
    'OptiScaler/menu/BrandImage.cpp','OptiScaler/menu/SleekBrand.h','OptiScaler/menu/brand/BrandPixels.inc',
    'OptiScaler/menu/brand/NeuRotic-header-v2.png','OptiScaler/dlssnr/DlssNrFeature_Dx12.h',
    'OptiScaler/dlssnr/DlssNr_Menu.cpp','OptiScaler/dlssnr/DlssNr_MenuStatus.h',
    'OptiScaler/dlssnr/DlssNr_StageControls.h','OptiScaler/dlssnr/DlssNr_MenuControls.h',
    'OptiScaler/dlssnr/NrStatusPanel.h','OptiScaler/dlssnr/NrPreflightCell.h','OptiScaler/include/imgui/imgui.cpp',
    'OptiScaler/include/imgui/imgui_widgets.cpp','OptiScaler/menu/SleekShell.h',
    'OptiScaler/menu/SleekUi.h','OptiScaler/menu/SleekSections.h','OptiScaler/menu/SleekMotion.h','OptiScaler/menu/SleekTheme.h','OptiScaler/menu/UiBrightness.h',
    'OptiScaler/menu/menu_common.cpp','OptiScaler/menu/menu_common.h',
    'OptiScaler/menu/OptiClipAdvisor.cpp','OptiScaler/menu/OptiClipAdvisor.h'}
changed=git('diff','--name-only','f9e62261','--','OptiScaler').decode().splitlines()
assert all(path in allowed or path.startswith('OptiScaler/menu/locales/') for path in changed), changed

for path in ('OptiScaler.ini','integration/OptiScaler.ini'):
    baseline=git('show','35326b84:'+path)
    actual=(root/path).read_bytes()
    if path=='OptiScaler.ini':
        baseline,actual=baseline.replace(b'\r\n',b'\n'),actual.replace(b'\r\n',b'\n')
    start=re.search(rb'(?m)^\[Menu\][^\r\n]*[\r\n]+',baseline).end()
    following=re.search(rb'(?m)^\[',baseline[start:])
    end=start+following.start() if following else len(baseline)
    menu=baseline[start:end]
    for key,value in ((b'Scale',b'auto'),(b'OptiClip',b'false')):
        menu,count=re.subn(rb'(?m)^'+key+rb'[ \t]*=[^\r\n]*',key+b' = '+value,menu)
        assert count==1
    assert baseline[:start]+menu+baseline[end:]==actual,path
print('PASS: save transaction/confirmation and close actions, renderer source and exact two INI defaults preserved')
