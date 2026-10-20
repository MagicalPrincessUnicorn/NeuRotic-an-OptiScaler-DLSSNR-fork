"""Keep the narrowed experiment's page/control arrangement identical to 4134edeb."""
from pathlib import Path
import subprocess,re
root=Path(__file__).resolve().parents[1]
def old(path):return subprocess.check_output(['git','-C',str(root),'show','4134edeb:'+path]).decode('utf8').replace('\r\n','\n')
def new(path):return (root/path).read_text(encoding='utf8')
def check(value,label):
    if not value:raise AssertionError(label)
    print('PASS:',label)
def method(text,name):
    start=text.index('void MenuCommon::'+name+'(')
    next_method=re.search(r'^void MenuCommon::',text[start+1:],re.M)
    return text[start:start+1+next_method.start()] if next_method else text[start:]
menu='OptiScaler/menu/menu_common.cpp'
# Later user-approved moves are covered by overlay_tabs_scope.py. The approved
# equal graph layout is exercised by menu_graph_compatibility_geometry.py.
shell='OptiScaler/menu/SleekShell.h'
stage_expected=old('OptiScaler/dlssnr/DlssNr_StageControls.h').replace('With Vulkan Ray Reconstruction, NR stays after reconstruction', 'With Ray Reconstruction, NR stays after reconstruction')
check(stage_expected==new('OptiScaler/dlssnr/DlssNr_StageControls.h'),'stage controls retained with approved shared RR guidance')
for path in ('OptiScaler.ini','integration/OptiScaler.ini'):
    expected=''.join(line for line in old(path).splitlines(keepends=True) if 'OptiClip' not in line)
    if path=='OptiScaler.ini':
        expected=expected.replace('[DLSSG]\n', '[DLSSG]\n; Experimental RTX 20/30 compatibility. Save and restart; does not enable FG.\nExperimentalUnlockRTX30=false\nExperimentalUnlockRTX20=false\n')
    check(expected==new(path),'template settings retained with OptiClip retirement and default-off MFG additions: '+path)
for path in ('OptiScaler/Config.h','OptiScaler/dlssnr/DlssNr_Menu.cpp',menu,shell):
    check('OptiClip::' not in new(path) and 'NoteNrUserToggle' not in new(path) and 'CustomOptional<bool> OptiClip' not in new(path),'no OptiClip activation path: '+path)
config=new('OptiScaler/Config.cpp')
check(config.count('Neurotic::MenuConfig::DiscardRetiredKeys(ini);')==2,'legacy key discarded on load and full save')
check('readBool("Menu", "OptiClip")' not in config and 'SetBoolValue("Menu", "OptiClip"' not in config,'retired value is never parsed or re-enabled')
for path in ('OptiScaler/menu/OptiClipAdvisor.cpp','OptiScaler/menu/OptiClipAdvisor.h','OptiScaler/menu/OptiClipController.h','OptiScaler/dlssnr/NrToggleNotes.h','OptiScaler/dlssnr/NrToggleBurst.h'):
    check(not (root/path).exists(),'retired runtime removed: '+path)
check('OptiClipAdvisor.cpp' not in new('OptiScaler/OptiScaler.vcxproj'),'retired runtime absent from build')
