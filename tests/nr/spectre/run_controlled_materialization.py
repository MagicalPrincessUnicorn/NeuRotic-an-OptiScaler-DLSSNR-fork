from pathlib import Path
import sys,json,subprocess
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
OUT=ROOT.parent/'builds/w03-hostreturn-admission'
label='controlled-materialization'
receipt=json.loads((OUT/'connected-green-receipt.json').read_text())
args=receipt['results'][0]['command']
args=[('/Fe'+str(OUT/(label+'.exe'))) if a.startswith('/Fe') else a for a in args]
args.insert(args.index('/link'),'/Fd'+str(OUT/(label+'.pdb')))
cmd=OUT/(label+'.cmd')
cmd.write_text('@echo off\ncall "C:\\Program Files\\Microsoft Visual Studio\\18\\Community\\VC\\Auxiliary\\Build\\vcvarsall.bat" x64 -vcvars_ver=14.44 >nul\n'+subprocess.list2cmdline(args)+'\nexit /b %errorlevel%\n')
objects=[OUT/n for n in ['native_runtime_attachment.obj','StreamCoordinatorKernel.obj','NativeSessionLifetime.obj','NativeRouteAdmission.obj',label+'.ilk']]
before=sum(p.stat().st_size for p in objects if p.exists())
b=run(['cmd.exe','/d','/c',str(cmd)],cwd=OUT,capture_output=True,text=True,timeout=240)
(OUT/(label+'-build.log')).write_text(b.stdout+b.stderr);print(b.stdout+b.stderr)
r=None
if b.returncode==0:
 r=run([str(OUT/(label+'.exe'))],cwd=OUT,capture_output=True,text=True,timeout=60)
 (OUT/(label+'-run.log')).write_text(r.stdout+r.stderr);print(r.stdout+r.stderr)
removed=0
for p in objects:
 if p.exists():removed+=p.stat().st_size;p.unlink()
(OUT/(label+'-receipt.json')).write_text(json.dumps({'command':args,'build':b.returncode,'run':None if r is None else r.returncode,'build_supervision':b.supervision,'run_supervision':None if r is None else r.supervision,'provider_execution':False,'warp_fixture':True,'storage_intermediates_before':before,'storage_intermediates_removed':removed,'storage_intermediates_after':0},indent=2)+'\n')
raise SystemExit(b.returncode or (r.returncode if r else 1))
