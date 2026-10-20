"""Real worker child-process protocol tests. No target window/provider is chosen."""
import json, subprocess, sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
exe=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
commands=[
 {'command':'status'},
 {'command':'start','revision':1,'mode':'countdown','seconds':5},
 {'command':'set-model','revision':2,'path':str(root/'builds/window-worker/no-model/nvngx_dlssnr.dll')},
 {'command':'status'},
 {'command':'set-comparison','revision':1,'split':0.5},
 {'command':'stop','revision':3},
 {'command':'quit'},
]
try:
 p=subprocess.run([str(exe),'--control'],input='\n'.join(json.dumps(c) for c in commands)+'\n',capture_output=True,text=True,encoding='utf-8',timeout=20)
 events=[json.loads(line) for line in p.stdout.splitlines() if line.strip()]
 assert events and events[0]['model']['ready'] is False,'initial model prerequisite is enforced'
 assert any(e.get('reason')=='Please provide your DLSS NR file.' for e in events),'start is refused with CTA'
 assert any('Revision must be newer' in e.get('reason','') for e in events),'stale command is rejected'
 assert any(e.get('event')=='stopped' for e in events),'stop acknowledged'
 assert events[-1].get('event')=='shutdown','quit acknowledged after drain'
 assert not any(e.get('nrCompleted',0) for e in events),'no provider execution without model'
 assert p.returncode==0,(p.returncode,p.stderr)
 bad=subprocess.run([str(exe),'--control'],input='x'*65537+'\n'+json.dumps({'command':'quit'})+'\n',capture_output=True,text=True,encoding='utf-8',timeout=20)
 assert any('65536' in json.loads(s).get('reason','') for s in bad.stdout.splitlines()),'oversize is rejected at stream boundary'
 print('PASS protocol 8 child-process assertions')
except Exception as e:
 print('FAIL protocol:',str(e));sys.exit(1)
