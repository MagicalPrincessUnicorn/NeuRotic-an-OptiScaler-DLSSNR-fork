"""Resolution requests must be validated before model/provider entry."""
from pathlib import Path
import json, subprocess

root=Path(__file__).resolve().parents[2]
exe=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
cases=[{'nrScalePercent':v} for v in [0,24,101,-1,67.5,True,'67']]
cases += [{'nrScalePercent':67,'workWidth':1280,'workHeight':720}]
for options in cases:
    command=dict(command='start',revision=1,mode='countdown',**options)
    run=subprocess.run([str(exe),'--control'],input=json.dumps(command)+'\n{"command":"quit"}\n',text=True,capture_output=True,timeout=15)
    records=[json.loads(line) for line in run.stdout.splitlines()]
    errors=[r for r in records if r.get('event')=='error']
    assert run.returncode==0 and errors and all(r.get('reason')!='Please provide your DLSS NR file.' for r in errors), options
for scale in [25,50,67,75,100]:
    command=dict(command='start',revision=1,mode='countdown',nrScalePercent=scale)
    run=subprocess.run([str(exe),'--control'],input=json.dumps(command)+'\n{"command":"quit"}\n',text=True,capture_output=True,timeout=15)
    assert run.returncode==0 and any(json.loads(line).get('reason')=='Please provide your DLSS NR file.' for line in run.stdout.splitlines()), scale
print('PASS resolution protocol: eight invalid requests refused before model entry; five valid scales reach model gate')
