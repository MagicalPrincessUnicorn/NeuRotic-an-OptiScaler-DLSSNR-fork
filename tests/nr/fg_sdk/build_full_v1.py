from pathlib import Path
import sys,json,hashlib,os,re
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tests/nr/spectre'))
from owned_process import run,STDOUT
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
BUILD=OUT/'full-v1'
BUILD.mkdir(exist_ok=True)
CMAKE=Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')
def size():
 return sum(p.stat().st_size for p in BUILD.rglob('*') if p.is_file() and not p.is_symlink())
before=size()
build_env=dict(os.environ);build_env["MSBUILDDISABLENODEREUSE"]="1"
if '--build-only' not in sys.argv:
 with (OUT/'full-v1-configure.log').open('w') as log:
  configured=run([str(CMAKE),'-S',str(ROOT/'external/FidelityFX-CheckedClosure/full-v1'),'-B',str(BUILD),'-G','Visual Studio 18 2026','-A','x64','-T','v145','--fresh'],cwd=OUT,env=build_env,timeout=45,stdout=log,stderr=STDOUT,text=True)
 (OUT/'full-v1-configure-supervision.json').write_text(json.dumps(configured.supervision,indent=2)+'\n')
 print('configure',configured.returncode,flush=True)
 if configured.returncode:
  print((OUT/'full-v1-configure.log').read_text()[-6000:]);raise SystemExit(configured.returncode)
configuration=json.loads((OUT/'full-v1-configure-supervision.json').read_text())
if not configuration['group_empty_observed'] or configuration['numeric_exit_code'] != 0:
 raise SystemExit('No successful CMake process with resolved owned-child cleanup')
# Generated build commands only: bound shader compiler threads, without changing permutations.
patched=0
for p in BUILD.rglob('*.vcxproj'):
 s=p.read_text(encoding='utf-8-sig')
 t=re.sub(r'(FidelityFX_SC\.exe(?:&quot;|\")?)(\s+)(?!-num-threads=)',r'\1 -num-threads=2\2',s)
 if t!=s:p.write_text(t,encoding='utf-8');patched+=1
print('thread-capped shader projects',patched,flush=True)
with (OUT/'full-v1-build.log').open('w') as log:
 built=run([str(CMAKE),'--build',str(BUILD),'--config','Release','--target','amd_fidelityfx_dx12','--parallel','2','--','/nr:false','/p:MultiToolTaskMaxProcesses=2','/p:EnforceProcessCountAcrossBuilds=true'],cwd=OUT,env=build_env,timeout=900,stdout=log,stderr=STDOUT,text=True)
(OUT/'full-v1-build-supervision.json').write_text(json.dumps(built.supervision,indent=2)+'\n')
print('build',built.returncode,flush=True)
print((OUT/'full-v1-build.log').read_text(errors='replace')[-7000:])
receipt={'configure_process_exit':configuration['numeric_exit_code'],'configure_supervisor_timed_out':configuration['timed_out'],'build_exit':built.returncode,'before_bytes':before,'after_bytes':size(),'permutation_headers':len(list(BUILD.rglob('*_permutations.h'))),'provider_game_execution':False,'public_checked_queries':'UNSUPPORTED','artifacts':[]}
for p in (BUILD/'bin').glob('*'):
 if p.is_file():receipt['artifacts'].append({'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()})
(OUT/'full-v1-build-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
raise SystemExit(built.returncode)
