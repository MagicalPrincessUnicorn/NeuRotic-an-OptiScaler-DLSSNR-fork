"""Focused verification runner with separate blocked hardware outcomes."""
import argparse,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
p=argparse.ArgumentParser()
p.add_argument('--model',type=Path)
p.add_argument('--depth-bundle',type=Path)
p.add_argument('--physical',action='store_true',help='Requires built native fixtures and Windows capture services')
a=p.parse_args();cases=[]
def run(name,command,blocked=False):
 result=subprocess.run(list(map(str,command)),cwd=root,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=120)
 outcome='PASS' if result.returncode==0 else 'BLOCKED' if blocked and result.returncode==3 else 'FAIL'
 cases.append({'name':name,'outcome':outcome,'exitCode':result.returncode,'stdout':result.stdout,'stderr':result.stderr})
 print(name+': '+outcome);print(result.stdout.rstrip())
 if outcome=='FAIL':print(result.stderr.rstrip())
run('control',[root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe','--test-control'])
run('protocol',[sys.executable,root/'tests/window-worker/ProtocolTests.py'])
if a.model:
 run('model-transaction',[root/'builds/window-worker/import-directory/ImportDirectoryTests.exe'])
 run('model-storage',[sys.executable,root/'tests/window-worker/ModelTests.py',a.model.resolve()])
 run('control-lifecycle-substitutes',[sys.executable,root/'tests/window-worker/ControlLifecycleTests.py',a.model.resolve()])
if a.physical:
 run('real-WGC',[root/'builds/window-worker/capture/bin/CaptureTests.exe'])
 run('NR-policy',[root/'builds/window-worker/nr/NrPolicyTests.exe'])
 run('owned-GPU-composition',[root/'builds/window-worker/nr/NrNativeTests.exe','--gpu-only'])
 guidance=[root/'builds/window-worker/guidance/GuidanceTests.exe']
 if a.depth_bundle:guidance.append(a.depth_bundle.resolve())
 run('guidance',guidance)
 if a.model:
  run('real-countdown-selection',[sys.executable,root/'tests/window-worker/SelectionPhysicalTests.py',a.model.resolve()])
  session=[sys.executable,root/'tests/window-worker/SessionTests.py',a.model.resolve()]
  if a.depth_bundle:session.append(a.depth_bundle.resolve())
  run('proprietary-connected-session',session,blocked=True)
receipt=root/'docs/implementation-evidence/window-nr-20261002/verification-final.json'
receipt.write_text(json.dumps({'cases':cases,'allAttemptedChecksPassed':all(c['outcome']=='PASS' for c in cases),'hardwareBlocked':any(c['outcome']=='BLOCKED' for c in cases)},indent=2),encoding='utf-8')
sys.exit(1 if any(c['outcome']=='FAIL' for c in cases) else 3 if any(c['outcome']=='BLOCKED' for c in cases) else 0)
