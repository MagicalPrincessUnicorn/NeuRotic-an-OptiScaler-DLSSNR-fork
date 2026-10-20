"""Production WorkerMain refuses retired depth before capture/model execution."""
import json,os,queue,subprocess,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[3]
env=dict(os.environ,NRW_CONTROL_FIXTURE_CASE='live-depth-preview')
p=subprocess.Popen([str(root/'builds/window-worker/lifecycle/bin/ControlLifecycleFixture.exe'),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',env=env)
events=queue.Queue()
def read():
    for line in p.stdout:events.put(json.loads(line))
threading.Thread(target=read,daemon=True).start()
def wait(predicate):
    end=time.monotonic()+10
    while time.monotonic()<end:
        try:e=events.get(timeout=.1)
        except queue.Empty:continue
        if predicate(e):return e
    raise AssertionError('preview status timeout')
def send(c):p.stdin.write(json.dumps(c)+'\n');p.stdin.flush()
try:
    initial=wait(lambda e:e.get('event')=='status')
    assert not initial['model']['ready']
    assert not initial['capabilities']['depthAnythingV2'] and not initial['capabilities']['depthPreviewWithoutNr']
    send({'command':'start','revision':1,'mode':'selected','window':{'hwnd':1,'pid':1,'processCreation':1,'windowClass':'ControlFixture','executable':'test-only'},'guides':'off','depthProvider':'dav2','depthProfile':'reference','depthHz':30,'depthPreview':True})
    refused=wait(lambda e:e.get('event')=='error')
    assert 'unavailable' in refused['reason']
    send({'command':'status'})
    done=wait(lambda e:e.get('event')=='status')
    assert not done['model']['ready'] and done['nrCompleted']==0 and done['captured']==0
    send({'command':'quit'});p.stdin.close();p.wait(timeout=5)
    assert p.returncode==0
    print('PASS retired depth unavailable in status and refused before capture/provider execution')
finally:
    if p.poll() is None:p.terminate();p.wait(timeout=5)
