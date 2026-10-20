"""Correlate rejected attempts without falsely advancing admitted revisions."""
import json, queue, subprocess, sys, threading
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2: raise SystemExit('Explicit compatible local model path required')
source=subprocess.Popen([str(root/'builds/hub-anything/SessionFixture.exe')],stdout=subprocess.PIPE,text=True,encoding='utf-8')
identity=json.loads(source.stdout.readline())
source.terminate();source.wait(timeout=3)  # the catalog identity is now definitively stale
worker=subprocess.Popen([str(root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,encoding='utf-8')
events=queue.Queue()
def drain():
    for line in worker.stdout: events.put(json.loads(line))
threading.Thread(target=drain,daemon=True).start()
def send(command):worker.stdin.write(json.dumps(command)+'\n');worker.stdin.flush()
def next_event(predicate):
    for _ in range(100):
        event=events.get(timeout=10)
        if predicate(event):return event
    raise AssertionError('Expected event not received')
try:
    next_event(lambda e:e.get('event')=='status')
    send({'command':'set-model','revision':1,'path':str(Path(sys.argv[1]).resolve())})
    assert next_event(lambda e:e.get('event') in ('model','error'))['model']['ready']
    send({'command':'start','revision':2,'mode':'selected','window':identity})
    rejected=next_event(lambda e:e.get('event')=='error')
    assert rejected['requestRevision']==2 and rejected['requestedRevision']==1, rejected
    send({'command':'status'})
    status=next_event(lambda e:e.get('event')=='status')
    assert status['session']==0 and status['nrSubmitted']==0, status
    print('PASS actual stale-window rejection identifies attempt 2, retains admitted revision 1, and enters no capture/NR session')
finally:
    if worker.poll() is None:
        send({'command':'quit'});worker.stdin.close()
        try:worker.wait(timeout=5)
        except subprocess.TimeoutExpired:worker.terminate();worker.wait(timeout=3)
