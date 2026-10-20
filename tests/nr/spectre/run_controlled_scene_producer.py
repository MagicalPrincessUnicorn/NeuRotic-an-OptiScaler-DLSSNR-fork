"""Supervised local WARP test; no provider or game. Reuses one compiler directory."""
from pathlib import Path
import os, sys, subprocess, json
from owned_process import run

root=Path(__file__).resolve().parents[3]
out=Path('C:/NeuRotic/builds/w03-controlled-producer');out.mkdir(exist_ok=True)
label=sys.argv[1] if len(sys.argv)>1 else 'verification'
before=sum(p.stat().st_size for p in out.iterdir() if p.is_file())
env={k.upper():v for k,v in os.environ.items()}
r=run(['cmd.exe','/d','/c','C:/NeuRotic/builds/w03-closure-r11/integration/environment.cmd'],env=env,capture_output=True,text=True,timeout=60)
assert r.returncode==0
for line in r.stdout.splitlines():
    if '=' in line and not line.startswith('='):
        k,v=line.split('=',1);env[k.upper()]=v
for k in ['CL','_CL_']:env.pop(k,None)
env['VSTEL_EWDK']='1'
compiler=str(Path(env['VCTOOLSINSTALLDIR'])/'bin/Hostx64/x64/cl.exe')
cmd=[compiler,'/nologo','/std:c++20','/EHsc','/MD','/W4','/bigobj','/Y-','/UNDEBUG','/I'+str(root/'OptiScaler'),'/I'+str(root/'OptiScaler/include'),str(root/'tests/nr/spectre/controlled_scene_producer.cpp'),'/Fe'+str(out/'controlled_scene_producer.exe'),'/link','/LIBPATH:'+str(root/'OptiScaler/library/detours'),'detours.lib','d3d12.lib','dxgi.lib','d3dcompiler.lib','ole32.lib','/STACK:8388608']
receipt={'provider':'NOT_RUN','game':'NOT_RUN','commands':[],'storage_before':before}
core=['nr/orchestration/StreamCoordinatorKernel.cpp','nr/lifecycle/NativeSessionLifetime.cpp','nr/protocol/NativeRouteAdmission.cpp']
cmd[cmd.index('/link'):cmd.index('/link')]=[str(root/'OptiScaler'/name) for name in core]
for name,command,timeout in [('compile',cmd,180),('test',[str(out/'controlled_scene_producer.exe')],60)]:
    with (out/(label+'-'+name+'.log')).open('wb') as log:r=run(command,cwd=out,env=env,stdout=log,stderr=subprocess.STDOUT,timeout=timeout)
    receipt['commands'].append(dict(name=name,exit_code=r.returncode,supervision=r.supervision))
    print(name,r.returncode,flush=True)
    if r.returncode:break
last=name
for object_name in ['controlled_scene_producer.obj']+[Path(name).stem+'.obj' for name in core]:(out/object_name).unlink(missing_ok=True)
receipt['storage_after']=sum(p.stat().st_size for p in out.iterdir() if p.is_file())
(out/(label+'-receipt.json')).write_text(json.dumps(receipt,indent=2)+'\n')
print((out/(label+'-'+last+'.log')).read_text(errors='replace')[-3000:])
sys.exit(r.returncode)
