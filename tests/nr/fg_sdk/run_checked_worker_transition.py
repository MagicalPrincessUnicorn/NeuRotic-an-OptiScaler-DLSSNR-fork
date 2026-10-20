from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
OUT.mkdir(parents=True,exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'current'
command='cl /nologo /std:c++20 /EHsc /W4 /I"'+str(ROOT)+'" /I"'+str(ROOT/'external/FidelityFX-SDK/sdk/include')+'" "'+str(ROOT/'tests/nr/fg_sdk/checked_worker_transition.cpp')+'" /Fe"'+str(OUT/'checked_worker_transition.exe')+'" /link d3d12.lib dxgi.lib'
cmd=OUT/'worker-transition.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\n'+command+'\nexit /b %errorlevel%\n')
build=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,timeout=120,capture_output=True,text=True)
(OUT/(label+'-worker-transition-build.log')).write_text(build.stdout+'\n'+build.stderr);print(build.stdout,build.stderr)
result=None
if build.returncode==0:
 result=run([str(OUT/'checked_worker_transition.exe')],cwd=OUT,timeout=10,capture_output=True,text=True)
 (OUT/(label+'-worker-transition-run.log')).write_text(result.stdout+'\n'+result.stderr);print(result.stdout,result.stderr)
for obj in OUT.glob('*.obj'):obj.unlink()
(OUT/(label+'-worker-transition-receipt.json')).write_text(json.dumps({'build_exit':build.returncode,'run_exit':result.returncode if result else None,'scope':'CPU real Win32 event creation/publication and worker acknowledgment; no GPU/provider/game'},indent=2)+'\n')
raise SystemExit(build.returncode or (result.returncode if result else 1))
