"""Physical connected capture -> actual NR -> owned output; explicit local providers only."""
import ctypes,json,subprocess,sys,threading,queue,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)<2:
 print('BLOCKED: explicit canonical NR model path required');sys.exit(3)
model=Path(sys.argv[1]).resolve()
depth=Path(sys.argv[2]).resolve() if len(sys.argv)>2 else None
worker=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
forwarder=root/'builds/window-worker/nr/nvngx.dll_dlssnr.dll'
source=controller=None
events=[];q=queue.Queue();assertions=0;diagnostics=[];outcome='RUNNING'
class HardwareBlocked(Exception):pass
def check(ok,text):
 global assertions
 assertions+=1
 if not ok:raise AssertionError(text)
def send(command):
 controller.stdin.write(json.dumps(command)+'\n');controller.stdin.flush()
def wait_for(predicate,seconds=30):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:event=q.get(timeout=min(0.2,end-time.monotonic()))
  except queue.Empty:continue
  events.append(event)
  if event.get('phase')=='Error' or event.get('event')=='error':
   reason=event.get('reason','')
   if 'Installed NVIDIA NGX core path unavailable' in reason:raise HardwareBlocked(reason)
   raise AssertionError(reason)
  if predicate(event):return event
 raise AssertionError('timed out waiting for connected pipeline: '+json.dumps(events[-2:]))
try:
 source=subprocess.Popen([str(root/'builds/window-worker/fixtures/bin/SessionFixture.exe')],stdout=subprocess.PIPE,text=True,encoding='utf-8')
 source_id=json.loads(source.stdout.readline())
 controller=subprocess.Popen([str(worker),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',bufsize=1)
 def reader():
  for line in controller.stdout:
   try:q.put(json.loads(line))
   except Exception:q.put({'event':'invalid-output','reason':line})
 threading.Thread(target=reader,daemon=True).start()
 def diagnostic_reader():
  for line in controller.stderr:
   if len(diagnostics)<256:diagnostics.append(line.rstrip())
 threading.Thread(target=diagnostic_reader,daemon=True).start()
 check(wait_for(lambda e:e.get('event')=='status')['model']['ready'] is False,'initial locked state')
 send({'command':'catalog','search':'NeuRotic owned pipeline fixture'})
 catalog=wait_for(lambda e:e.get('event')=='catalog')
 matches=[w for w in catalog['windows'] if w['hwnd']==source_id['hwnd']]
 check(len(matches)==1,'actual source appears in valid candidate list')
 selected=matches[0]
 send({'command':'set-model','revision':1,'path':str(model)})
 check(wait_for(lambda e:e.get('event')=='model')['model']['ready'],'exact model admitted')
 base={'command':'start','mode':'selected','window':selected,'forwarder':str(forwarder),'cursor':False}
 send(dict(base,revision=2,guides='off'))
 first=wait_for(lambda e:e.get('nrCompleted',0)>=2)
 check(first['nrSubmitted']>=first['nrCompleted']>=2 and first['presentAccepted']>=2,'real completed NR outputs reach presenter')
 check(first['sourceWidth']==1280 and first['sourceHeight']==720,'source/capture geometry')
 check(first['workWidth']==1280 and first['workHeight']==720,'actual NR working geometry')
 check(not first['guidance']['consumed'],'baseline constants contain no derived guides')
 send({'command':'set-comparison','revision':3,'split':0.5})
 check(wait_for(lambda e:e.get('event')=='comparison')['comparisonSplit']==0.5,'same-frame wipe applied')
 ctypes.windll.user32.PostMessageW(ctypes.c_void_p(source_id['hwnd']),0x8001,(540<<16)|960,0)
 resized=wait_for(lambda e:e.get('sourceWidth')==960 and e.get('sourceHeight')==540 and e.get('nrCompleted',0)>first['nrCompleted'])
 check(resized['session']==first['session'],'resize keeps selected session and resets model history')
 send({'command':'stop','revision':4})
 check(wait_for(lambda e:e.get('event')=='stopped')['phase']=='Stopped','checked stop')
 # Fresh explicit selection uses catalog identity; content size may change independently.
 send({'command':'catalog','search':'NeuRotic owned pipeline fixture'})
 selected=next(w for w in wait_for(lambda e:e.get('event')=='catalog')['windows'] if w['hwnd']==source_id['hwnd'])
 ctypes.windll.user32.PostMessageW(ctypes.c_void_p(source_id['hwnd']),0x8001,(720<<16)|1280,0)
 if depth:
  send(dict(base,revision=5,window=selected,guides='experimental',depthBundle=str(depth)))
  enhanced=wait_for(lambda e:e.get('guidance',{}).get('consumed') and e.get('presentAccepted',0)>=2,seconds=60)
  check(enhanced['guidance']['depthReady'] and enhanced['guidance']['motionReady'] and not enhanced['guidance']['nativeQualified'],'actual estimated guides reach explicitly experimental NR')
  check(enhanced['guidance']['estimated'],'estimated provenance preserved')
 else:
  send(dict(base,revision=5,window=selected,guides='shadow'))
  shadow=wait_for(lambda e:e.get('presentAccepted',0)>=2)
  check(not shadow['guidance']['consumed'],'shadow outputs never alter NR')
 ctypes.windll.user32.PostMessageW(ctypes.c_void_p(source_id['hwnd']),0x10,0,0)
 check(wait_for(lambda e:e.get('event')=='stopped')['phase']=='Stopped','source closure retires connected worker')
 send({'command':'quit'})
 check(wait_for(lambda e:e.get('event')=='shutdown')['phase']=='Stopped','shutdown acknowledged')
 controller.stdin.close();controller.wait(timeout=15);source.wait(timeout=5)
 check(controller.returncode==0 and source.returncode==0,'owned processes exit cleanly')
 outcome='PASS';print(f'PASS connected session {assertions} physical assertions')
except HardwareBlocked as e:
 outcome='HARDWARE_BLOCKED';print('BLOCKED connected proprietary NR:',str(e));sys.exit(3)
except Exception as e:
 outcome='FAIL';print('FAIL connected session:',str(e));sys.exit(1)
finally:
 receipt=root/'docs/implementation-evidence/window-nr-20261002/session-events.json'
 receipt.write_text(json.dumps({'outcome':outcome,'assertions':assertions,'events':events,'diagnostics':diagnostics},indent=2),encoding='utf-8')
 for process in (controller,source):
  if process and process.poll() is None:
   if process is controller:
    try:send({'command':'quit'});controller.stdin.close()
    except (BrokenPipeError,OSError):pass
   if process is source and 'source_id' in globals():ctypes.windll.user32.PostMessageW(ctypes.c_void_p(source_id['hwnd']),0x10,0,0)
   try:process.wait(timeout=5)
   except subprocess.TimeoutExpired:process.terminate();process.wait(timeout=5)
