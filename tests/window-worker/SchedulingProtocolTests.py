"""Real worker wait diagnostics and idle command/EOF wakeups; no model entry."""
import json, subprocess, time, threading, queue
from pathlib import Path

root=Path(__file__).resolve().parents[2]
exe=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
p=subprocess.Popen([str(exe),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8')
events=queue.Queue()
def reader():
    for line in p.stdout: events.put(json.loads(line))
threading.Thread(target=reader,daemon=True).start()
try:
    initial=events.get(timeout=10)
    assert initial['capabilities'].get('eventDrivenScheduling'), 'worker still uses polling sleep instead of event-driven scheduling'
    assert initial['performance']['scheduling']['mode']=='capture-command-message-events'
    time.sleep(.15)
    p.stdin.write('{"command":"status"}\n');p.stdin.flush()
    status=events.get(timeout=3)
    assert status['event']=='status'
    timing=status['performance']['scheduling']
    assert timing['waitCalls']>0 and timing['waitMilliseconds']>0
    assert timing['waitCalls']<100, 'idle worker spins instead of waiting'
    p.stdin.write('{"command":"stop","revision":1}\n');p.stdin.flush()
    assert events.get(timeout=3)['event']=='stopped'
    p.stdin.close()
    assert events.get(timeout=3)['event']=='shutdown', 'EOF must wake idle worker'
    assert p.wait(timeout=3)==0
    print('PASS event-driven capability, idle waits, command wake, Stop and EOF shutdown')
finally:
    if p.poll() is None:p.kill();p.wait(timeout=3)
