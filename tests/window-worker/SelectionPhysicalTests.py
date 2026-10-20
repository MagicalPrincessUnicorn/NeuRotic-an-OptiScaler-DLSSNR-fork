"""Real worker countdown/foreground/cancel against an owned benign external window."""
import ctypes,json,subprocess,sys,threading,queue,time
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2:print('BLOCKED: explicit compatible model path required');sys.exit(3)
source=controller=None;events=[];checks=0;q=queue.Queue()
def send(c):controller.stdin.write(json.dumps(c)+'\n');controller.stdin.flush()
def wait(predicate,seconds=10):
 end=time.monotonic()+seconds
 while time.monotonic()<end:
  try:e=q.get(timeout=.1)
  except queue.Empty:continue
  events.append(e)
  if predicate(e):return e
 raise AssertionError('countdown/selection event timeout')
def check(ok,text):
 global checks
 if not ok:raise AssertionError(text)
 checks+=1
try:
 source=subprocess.Popen([str(root/'builds/window-worker/fixtures/bin/SessionFixture.exe')],stdout=subprocess.PIPE,text=True,encoding='utf-8')
 identity=json.loads(source.stdout.readline())
 controller=subprocess.Popen([str(root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.DEVNULL,text=True,encoding='utf-8')
 def reader():
  for line in controller.stdout:q.put(json.loads(line))
 threading.Thread(target=reader,daemon=True).start()
 wait(lambda e:e.get('event')=='status')
 send({'command':'set-model','revision':1,'path':str(Path(sys.argv[1]).resolve())})
 check(wait(lambda e:e.get('event') in ('model','error')).get('model',{}).get('ready'),'explicit model gate admitted')
 send({'command':'start','revision':2,'mode':'countdown','seconds':1})
 check(wait(lambda e:e.get('event')=='countdown')['countdownRemainingMs']>0,'real pending countdown')
 send({'command':'cancel','revision':3})
 check(wait(lambda e:e.get('event')=='stopped')['countdownRemainingMs']==0,'real cancel clears countdown')
 time.sleep(1.1)
 send({'command':'status'})
 check(wait(lambda e:e.get('event')=='status')['session']==0,'cancelled countdown never enters capture')
 hwnd=ctypes.c_void_p(identity['hwnd'])
 ctypes.windll.user32.PostMessageW(hwnd,0x8002,0,0)
 end=time.monotonic()+3
 ctypes.windll.user32.GetForegroundWindow.restype=ctypes.c_void_p
 while time.monotonic()<end and ctypes.windll.user32.GetForegroundWindow()!=identity['hwnd']:time.sleep(.02)
 check(ctypes.windll.user32.GetForegroundWindow()==identity['hwnd'],'owned fixture foreground selected by Windows')
 send({'command':'start','revision':4,'mode':'countdown','seconds':1})
 wait(lambda e:e.get('event')=='countdown')
 entered=wait(lambda e:e.get('event') in ('started','error'))
 check(entered.get('window',{}).get('hwnd')==identity['hwnd'] and entered.get('session')==1,'actual expiry enters the exact foreground identity')
 check(entered.get('event')=='started' or 'Installed NVIDIA NGX core path unavailable' in entered.get('reason',''),'capture enters safely; only explicit known hardware gap permitted')
 send({'command':'quit'});wait(lambda e:e.get('event')=='shutdown');controller.stdin.close();controller.wait(timeout=10)
 check(controller.returncode==0,'parent shutdown exits')
 print(f'PASS physical selection {checks} assertions; proprietary NR qualification separate')
except Exception as e:print('FAIL physical selection:',str(e));sys.exit(1)
finally:
 (root/'docs/implementation-evidence/window-nr-20261002/selection-events.json').write_text(json.dumps({'checks':checks,'events':events},indent=2),encoding='utf-8')
 if controller and controller.poll() is None:
  try:send({'command':'quit'});controller.stdin.close();controller.wait(timeout=5)
  except Exception:controller.terminate();controller.wait(timeout=5)
 if source and source.poll() is None:
  if 'identity' in globals():ctypes.windll.user32.PostMessageW(ctypes.c_void_p(identity['hwnd']),0x10,0,0)
  try:source.wait(timeout=5)
  except subprocess.TimeoutExpired:source.terminate();source.wait(timeout=5)
