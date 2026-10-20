"""Control-only regressions against production WorkerMain and test-only host substitutes."""
import os,json,subprocess,threading,queue,time,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2:
 print('BLOCKED: explicit canonical file needed for actual model gate');sys.exit(3)
model=Path(sys.argv[1]).resolve();checks=0
def case(name):
 global checks
 env=dict(os.environ);env['NRW_CONTROL_FIXTURE_CASE']=name
 p=subprocess.Popen([str(root/'builds/window-worker/lifecycle/bin/ControlLifecycleFixture.exe'),'--control'],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',env=env)
 q=queue.Queue()
 def reader():
  for line in p.stdout:q.put(json.loads(line))
 threading.Thread(target=reader,daemon=True).start()
 def wait(predicate):
  end=time.monotonic()+10
  while time.monotonic()<end:
   try:e=q.get(timeout=.1)
   except queue.Empty:continue
   if predicate(e):return e
  raise AssertionError('mocked control event timed out')
 def send(command):p.stdin.write(json.dumps(command)+'\n');p.stdin.flush()
 try:
  wait(lambda e:e.get('event')=='status')
  send({'command':'set-model','revision':1,'path':str(model)})
  wait(lambda e:e.get('event')=='model')
  if name=='live':
   start={'command':'start','revision':2,'mode':'selected','window':{'hwnd':1,'pid':1,'processCreation':1,'windowClass':'ControlFixture','executable':'test-only'},'guides':'off'}
   send(start);wait(lambda e:e.get('event')=='processing-updated');wait(lambda e:e.get('sourceWidth')==96)
   config={'command':'set-processing','revision':3,'nrScalePercent':50,'modelStyle':2,'transferStrength':.42,'colourStrength':.73}
   send(config);ack=wait(lambda e:e.get('event')=='processing-requested')
   assert ack['processing']['requestedRevision']==3 and ack['processing']['appliedRevision']==2,'request acknowledgment retains last completed settings';checks+=1
   done=wait(lambda e:e.get('event')=='processing-updated' and e['processing']['appliedRevision']==3)
   assert done['processing']['applied']['modelStyle']==2 and abs(done['processing']['applied']['transferStrength']-.42)<1e-6,'completed frame applies full config';checks+=1
   assert done['sourceWidth']==96 and done['workWidth']==48 and done['workHeight']==32,'quality combines with resized source in control host';checks+=1
   send(config);refused=wait(lambda e:e.get('event')=='error')
   assert refused['appliedRevision']>=3,'stale revision cannot roll back completed processing';checks+=1
   burst=[]
   for revision in range(4,10):
    config=dict(config,revision=revision,transferStrength=revision/10);burst.append(json.dumps(config))
   p.stdin.write('\n'.join(burst)+'\n');p.stdin.flush()
   latest=wait(lambda e:e.get('event')=='processing-updated' and e['processing']['appliedRevision']==9)
   assert abs(latest['processing']['applied']['transferStrength']-.9)<1e-6,'rapid updates converge to latest requested values';checks+=1
   p.stdin.write(json.dumps(dict(config,revision=10))+'\n'+json.dumps({'command':'stop','revision':11})+'\n');p.stdin.flush()
   stopped=wait(lambda e:e.get('event')=='stopped')
   assert stopped['processing']['state']=='stopped' and stopped['phase']=='Stopped','stop cancels pending live edits';checks+=1
   send(dict(start,revision=12,nrScalePercent=100,modelStyle=0,transferStrength=1,colourStrength=1))
   restarted=wait(lambda e:e.get('event')=='processing-updated' and e['processing']['appliedRevision']==12)
   assert restarted['processing']['applied']['transferStrength']==1,'new session cannot inherit pending edit';checks+=1
   p.stdin.close();p.wait(timeout=5);assert p.returncode==0,'disconnect with active processing retires fixture';checks+=1
   return
  if name=='quarantine':
   send({'command':'start','revision':2,'mode':'selected','window':{'hwnd':1,'pid':1,'processCreation':1,'windowClass':'ControlFixture','executable':'test-only'},'guides':'shadow'})
   failed=wait(lambda e:e.get('event')=='error' and e.get('phase')=='RestartRequired')
   assert failed['nrSubmitted']==0,'quarantined guide stops before any subsequent NR submission'
   checks+=1
   send({'command':'set-model','revision':3,'path':str(model)})
   wait(lambda e:e.get('event') in ('model','error'))
   send({'command':'start','revision':4,'mode':'countdown','seconds':1})
   refused=wait(lambda e:e.get('event') in ('error','countdown'))
   assert refused['event']=='error','retained host cannot admit another countdown after model change'
   checks+=1
  else:
   send({'command':'start','revision':2,'mode':'countdown','seconds':1})
   wait(lambda e:e.get('event')=='countdown')
   send({'command':'set-model','revision':3,'path':str(model)})
   wait(lambda e:e.get('event')=='error')
   if name=='comparison':
    send({'command':'set-comparison','revision':4,'split':.75})
    wait(lambda e:e.get('event')=='comparison')
   started=wait(lambda e:e.get('event')=='started')
   assert started['appliedRevision']==(4 if name=='comparison' else 2),'startup does not acknowledge rejected revision or roll back accepted comparison'
   assert started['comparisonSplit']==(.75 if name=='comparison' else 0),'newer comparison survives countdown startup'
   checks+=2
  send({'command':'quit'});wait(lambda e:e.get('event')=='shutdown');p.stdin.close();p.wait(timeout=5)
  assert p.returncode==0;checks+=1
 finally:
  if p.poll() is None:p.terminate();p.wait(timeout=5)
try:
 for name in ('quarantine','shutdown'):
  env=dict(os.environ);env['NRW_CONTROL_FIXTURE_CASE']=name
  cli=subprocess.run([str(root/'builds/window-worker/lifecycle/bin/ControlLifecycleFixture.exe'),'--run','--model',str(model),'--window','1','--guides','shadow','--duration','1'],capture_output=True,text=True,encoding='utf-8',env=env,timeout=10)
  assert cli.returncode!=0,'CLI reports failed GPU retirement instead of success: '+name
  checks+=1
 for name in ('rejected-model','comparison','quarantine','live'):case(name)
 print(f'PASS mocked control lifecycle {checks} assertions; no NR provider execution')
except Exception as e:print('FAIL mocked control lifecycle:',str(e));sys.exit(1)
