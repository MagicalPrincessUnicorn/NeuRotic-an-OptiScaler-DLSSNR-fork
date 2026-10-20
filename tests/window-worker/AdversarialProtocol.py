"""Read-only audit probes against the delivered production controller; no NR invocation."""
import json,queue,subprocess,sys,threading,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
exe=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
receipt={}
for depth in (1000,10000,30000):
 text='{"command":"status","extra":'+'['*depth+'0'+']'*depth+'}\n{"command":"quit"}\n'
 p=subprocess.run([str(exe),'--control'],input=text,capture_output=True,text=True,encoding='utf-8',timeout=10)
 receipt.setdefault('deep_json',[]).append({'depth':depth,'bytes':len(text),'exit':p.returncode,'shutdown': '"event":"shutdown"' in p.stdout})
# Backpressure must not be confused with a provider failure. This deliberately
# stalls stdout, closes stdin, then resumes draining to establish causality.
p=subprocess.Popen([str(exe),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def flood():
 try:p.stdin.write(b'{"command":"status"}\n'*512);p.stdin.close()
 except (BrokenPipeError,OSError):pass
t=threading.Thread(target=flood,daemon=True);t.start();t.join(timeout=3)
time.sleep(.5);held=p.poll() is None
def drain():p.stdout.read()
reader=threading.Thread(target=drain,daemon=True);reader.start()
try:p.wait(timeout=5)
except subprocess.TimeoutExpired:p.terminate();p.wait(timeout=5)
reader.join(timeout=2)
receipt['stdout_backpressure']={'stdin_writer_finished':not t.is_alive(),'alive_after_eof_while_stdout_stalled':held,'exit_after_drain':p.returncode}
if len(sys.argv)>1:
 p=subprocess.Popen([str(exe),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
 q=queue.Queue();events=[]
 def consume():
  for line in p.stdout:
   try:q.put(json.loads(line))
   except Exception:pass
 threading.Thread(target=consume,daemon=True).start()
 def send(c):p.stdin.write(json.dumps(c)+'\n');p.stdin.flush()
 def wait(pred,seconds=5):
  end=time.monotonic()+seconds
  while time.monotonic()<end:
   try:e=q.get(timeout=.1)
   except queue.Empty:continue
   events.append(e)
   if pred(e):return e
  raise RuntimeError('audit event timeout')
 try:
  wait(lambda e:e.get('event')=='status')
  send({'command':'set-model','revision':1,'path':str(Path(sys.argv[1]).resolve())})
  model=wait(lambda e:e.get('event') in ('model','error'))
  if not model.get('model',{}).get('ready'):raise RuntimeError('Explicit canonical model prerequisite failed')
  # One admitted start, enough ordinary requests to fill the queue, then a cancel.
  batch=[{'command':'start','revision':2,'seconds':30}]+[{'command':'status'}]*512+[{'command':'cancel','revision':3}]
  p.stdin.write(''.join(json.dumps(c)+'\n' for c in batch));p.stdin.flush()
  time.sleep(.25)
  send({'command':'status'})
  state=wait(lambda e:e.get('event')=='status' and e.get('countdownRemainingMs',30000)<29700)
  receipt['cancel_under_load']={'phase':state.get('phase'),'remaining':state.get('countdownRemainingMs'),'requestedRevision':state.get('requestedRevision'),'cancel_lost':state.get('phase')=='Countdown','overflow_observed':any('Too many pending' in e.get('reason','') for e in events)}
  send({'command':'cancel','revision':4});wait(lambda e:e.get('event')=='stopped')
  send({'command':'quit'});p.stdin.close();p.wait(timeout=5)
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=5)
 receipt['cancel_events']=events
destination=root/'docs/implementation-evidence/window-nr-adversarial-20261003/protocol-probes.json'
destination.write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps({k:v for k,v in receipt.items() if k!='cancel_events'},indent=2))
