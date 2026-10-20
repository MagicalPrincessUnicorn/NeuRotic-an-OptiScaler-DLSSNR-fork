from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
label=sys.argv[1] if len(sys.argv)>1 else 'algorithm-owner'
cmd=OUT/'algorithm-owner.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /I"'+str(ROOT)+'" /I"'+str(ROOT/'external/FidelityFX-SDK/sdk/include')+'" "'+str(ROOT/'tests/nr/fg_sdk/checked_algorithm_owner.cpp')+'" /Fe"'+str(OUT/'algorithm-owner.exe')+'" /link d3d12.lib dxgi.lib\nexit /b %errorlevel%\n')
b=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,capture_output=True,text=True,timeout=120)
(OUT/(label+'-build.log')).write_text(b.stdout+b.stderr);print(b.stdout+b.stderr)
r=None
if b.returncode==0:
 r=run([str(OUT/'algorithm-owner.exe')],cwd=OUT,capture_output=True,text=True,timeout=20)
 (OUT/(label+'-run.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
for p in OUT.glob('checked_algorithm_owner.obj'):p.unlink()
(OUT/(label+'-receipt.json')).write_text(json.dumps({'build':b.returncode,'run':None if r is None else r.returncode,'full_algorithm_execution':False},indent=2)+'\n')
raise SystemExit(b.returncode or (r.returncode if r else 1))
