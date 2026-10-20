"""Compile the actual in-game Keybind class and screenshot table helper unmodified."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
out=root/'builds/hub/hotkey-chord'
out.mkdir(parents=True,exist_ok=True)
source=(root/'OptiScaler/menu/menu_common.cpp').read_text(encoding='utf-8')
def body(signature):
    start=source.index(signature);opening=source.index('{',start);depth,end=1,opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    return source[start:end]
(out/'in-game-keybind.inc').write_text(body('class Keybind')+';\n'+body('void MenuCommon::RenderScreenshotKeybind(Config* config)'),encoding='utf-8')
subprocess.run(['cmd','/c',str(root/'tests/hub/Run-InGameKeybind.cmd')],cwd=root,check=True)
