from pathlib import Path
import hashlib,json,os,sys
ROOT=Path(__file__).resolve().parents[3];OUT=ROOT.parent/'builds/w03-fg-sdk-closure';WORK=OUT/'previous-use-wait';WORK.mkdir(exist_ok=True)
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run
label=sys.argv[1] if len(sys.argv)>1 else 'r27'
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def size():return sum(p.stat().st_size for p in WORK.iterdir() if p.is_file())
files=[ROOT/'external/FidelityFX-CheckedClosure/CheckedSdkRuntime.h',ROOT/'tests/nr/fg_sdk/checked_previous_use_wait.cpp',ROOT/'external/FidelityFX-SDK/sdk/src/backends/dx12/FrameInterpolationSwapchain/FrameInterpolationSwapchainDX12.cpp']
sources={str(p):sha(p) for p in files};before=size()
env={k.upper():v for k,v in os.environ.items()}
setup=run(['cmd.exe','/d','/c','C:/NeuRotic/builds/w03-closure-r11/integration/environment.cmd'],env=env,capture_output=True,text=True,timeout=60)
assert setup.returncode==0 and not setup.infrastructure_error
for line in setup.stdout.splitlines():
 if '=' in line and not line.startswith('='):
  k,v=line.split('=',1);env[k.upper()]=v
for k in ('CL','_CL_'):env.pop(k,None)
compiler='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64/cl.exe'
obj=WORK/'checked_previous_use_wait.obj';exe=WORK/'checked_previous_use_wait.exe'
command=[compiler,'/nologo','/std:c++20','/EHsc','/W4','/Gy','/I'+str(ROOT),str(files[1]),'/Fo'+str(obj),'/Fe'+str(exe),'/link','/OPT:REF','d3d12.lib','dxgi.lib','dxguid.lib']
b=run(command,cwd=WORK,env=env,capture_output=True,text=True,timeout=120)
(OUT/(label+'-previous-use-build.log')).write_text(b.stdout+b.stderr)
t=None
if b.returncode==0 and not b.infrastructure_error:
 t=run([str(exe)],cwd=WORK,capture_output=True,text=True,timeout=30)
 (OUT/(label+'-previous-use-test.log')).write_text(t.stdout+t.stderr)
removed=0
if obj.exists():removed=obj.stat().st_size;obj.unlink()
r=dict(compile_exit=b.returncode,test_exit=t.returncode if t else None,compile_supervision=b.supervision,test_supervision=t.supervision if t else None,command=command,source_hashes=sources,source_unchanged=all(sha(Path(p))==h for p,h in sources.items()),storage_before_bytes=before,storage_after_bytes=size(),object_bytes_reclaimed=removed,scope='WARP_SYNC_ONLY',provider_execution=False)
(OUT/(label+'-previous-use-receipt.json')).write_text(json.dumps(r,indent=2)+'\n')
print('compile',b.returncode,'test',t.returncode if t else None,flush=True);print(t.stdout+t.stderr if t else b.stdout+b.stderr,flush=True)
sys.exit(0 if b.returncode==0 and t and t.returncode==0 and not b.infrastructure_error and not t.infrastructure_error and r['source_unchanged'] else 1)
