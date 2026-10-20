"""Exercise real built effect destruction using CPU callback fixtures only."""
from pathlib import Path
import sys,json,hashlib
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
label=sys.argv[1] if len(sys.argv)>1 else 'effect-cleanup'
libs=[OUT/'full-v1/bin'/n for n in ('ffx_frameinterpolation_x64.lib','ffx_opticalflow_x64.lib')]
pins={str(p):hashlib.sha256(p.read_bytes()).hexdigest() for p in libs}
cmd=OUT/'effect-cleanup.cmd'
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.51 >nul\ncl /nologo /std:c++20 /EHsc /MD /W4 /O2 /I"'+str(ROOT)+'" /I"'+str(ROOT/'external/FidelityFX-SDK/sdk/include')+'" "'+str(ROOT/'tests/nr/fg_sdk/checked_effect_cleanup.cpp')+'" /Fe"'+str(OUT/'effect-cleanup.exe')+'" /link '+ ' '.join('"'+str(p)+'"' for p in libs)+'\nexit /b %errorlevel%\n')
b=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,capture_output=True,text=True,timeout=120)
(OUT/(label+'-build.log')).write_text(b.stdout+b.stderr);print(b.stdout+b.stderr)
r=None
if b.returncode==0:
    r=run([str(OUT/'effect-cleanup.exe')],cwd=OUT,capture_output=True,text=True,timeout=20)
    (OUT/(label+'-run.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
for p in OUT.glob('checked_effect_cleanup.obj'):p.unlink()
(OUT/(label+'-receipt.json')).write_text(json.dumps(dict(build=b.returncode,run=None if r is None else r.returncode,build_supervision=b.supervision,run_supervision=None if r is None else r.supervision,libraries=pins,provider_execution=False,gpu_execution=False),indent=2)+'\n')
raise SystemExit(b.returncode or (r.returncode if r else 1))
