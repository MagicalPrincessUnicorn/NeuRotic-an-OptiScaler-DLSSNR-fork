from pathlib import Path
import sys,json
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
label=sys.argv[1] if len(sys.argv)>1 else 'controlled-module'
before=sum(p.stat().st_size for p in OUT.glob('controlled-module.*') if p.is_file())
cmd=OUT/'controlled-module.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\ncl /nologo /std:c++20 /EHsc /W4 /I"'+str(ROOT)+'" "'+str(ROOT/'tests/nr/fg_sdk/controlled_module.cpp')+'" /Fo"'+str(OUT/'controlled-module.obj')+'" /Fe"'+str(OUT/'controlled-module.exe')+'" /link d3d12.lib dxgi.lib\nexit /b %errorlevel%\n')
b=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,capture_output=True,text=True,timeout=120)
(OUT/(label+'-build.log')).write_text(b.stdout+b.stderr);print(b.stdout+b.stderr)
r=None
if b.returncode==0:
 r=run([str(OUT/'controlled-module.exe')],cwd=OUT,capture_output=True,text=True,timeout=20)
 (OUT/(label+'-run.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
obj=OUT/'controlled-module.obj'
removed=obj.stat().st_size if obj.exists() else 0
obj.unlink(missing_ok=True)
after=sum(p.stat().st_size for p in OUT.glob('controlled-module.*') if p.is_file())
(OUT/(label+'-receipt.json')).write_text(json.dumps({'build':b.returncode,'run':None if r is None else r.returncode,'full_algorithm_execution':False,'storage_before':before,'storage_after':after,'intermediate_removed':removed},indent=2)+'\n')
raise SystemExit(b.returncode or (r.returncode if r else 1))
