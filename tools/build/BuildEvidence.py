"""Content-bound local build evidence. Requires exclusive checkout/build ownership.

This is a correctness receipt, not protection against a malicious local writer.
Uncaptured producer kinds fail closed. Policy fixtures never become build receipts.
"""
from pathlib import Path
from ProducerCapture import tlog_records,check_native_write,replay_actions,identity_bytes
import argparse,hashlib,json,re,sys
sys.path.insert(0,str(Path(__file__).parents[1]/'packaging/customer'))
from OfflineDependencies import require,file_sha,owned_output,write_owned

def key(p):return str(Path(p).resolve()).casefold()
def row(p,kind=None):
 p=Path(p);result={'path':str(p.absolute()),'sha256':file_sha(p),'bytes':p.stat().st_size}
 if kind:result['kind']=kind
 return result
def check_row(r):
 p=Path(r['path']);require(p.is_absolute(),'Absolute evidence input required')
 require(p.is_file() and p.stat().st_size==r['bytes'] and file_sha(p)==r['sha256'],'Evidence input changed: '+str(p))

def validate_native_tracking_roots(stem,reads,writes,commands):
 """Bind the exact native root relationship, retaining every raw record.

 CL may record a batch write root alongside each source's shared-PDB root,
 while read and command records remain per-source. The only extra root this
 adapter accepts is one complete, duplicate-free group of those same sources.
 Other producer kinds and representations retain exact marker equality.
 """
 require(commands.keys()==reads.keys(),'Command tracking roots differ')
 if reads.keys()==writes.keys():return None
 extra=writes.keys()-reads.keys()
 require(stem.casefold()=='cl' and len(reads)>1 and all(len(marker)==1 for marker in reads)
         and reads.keys()<=writes.keys() and len(extra)==1,'Native read/write tracking roots differ')
 batch=next(iter(extra));sources={marker[0] for marker in reads}
 require(len(batch)==len(sources) and len(set(batch))==len(batch) and set(batch)==sources,'Native read/write tracking roots differ')
 return dict(kind='CL_BATCH_WRITE_WITH_SINGLETON_READ_COMMAND',composite_write_root=[str(p) for p in writes[batch][0]])

def creation_filetime(value):
 return isinstance(value,str) and re.fullmatch('[1-9][0-9]{0,19}',value) is not None and int(value)<=2**64-1

def process_instances(events,*,closed=True):
 """Index actual CREATE-to-EXIT lifetimes without resolving or reopening PIDs.

 A prefix may retain open instances for the owner poller (closed=False).
 A reused PID requires distinct, ordered held-handle creation times. Legacy
 unique-PID traces retain their existing contract; they cannot prove reuse.
 Raw rows are preserved and each event belongs only to its active instance.
 """
 active={};previous={};instances=[]
 def held(create):
  return type(create.get('process_creation_error')) is int and create['process_creation_error']==0 and creation_filetime(create.get('process_creation_filetime'))
 for i,event in enumerate(events):
  require(type(event.get('seq')) is int and event['seq']==i,'Contiguous process event sequence required')
  kind=event.get('kind')
  if kind=='begin':require(i==0,'Unexpected process begin');continue
  if kind=='end':require(i==len(events)-1,'Unexpected process end');continue
  pid=event.get('pid');require(type(pid) is int and pid>0,'Actual process identity required')
  if kind=='create':
   require(pid not in active,'Overlapping process PID reuse')
   if 'process_creation_error' in event or 'process_creation_filetime' in event:
    require(type(event.get('process_creation_error')) is int and event['process_creation_error']>=0
            and (event['process_creation_error']!=0 or creation_filetime(event.get('process_creation_filetime'))),'Invalid process creation context')
   old=previous.get(pid)
   if old is not None:
    require(held(old['create']) and held(event) and int(old['create']['process_creation_filetime'])<int(event['process_creation_filetime']),'Ambiguous reused process creation time')
   if type(event.get('parent_context_error')) is int and event['parent_context_error']==0:
    parent=active.get(event.get('parent_pid'));parent_create=parent['create'] if parent else {}
    require(parent is not None and type(event.get('parent_pid')) is int and type(event.get('parent_create_seq')) is int
            and event['parent_create_seq']==parent_create['seq'] and held(parent_create) and held(event)
            and event.get('parent_creation_filetime')==parent_create['process_creation_filetime']
            and int(parent_create['process_creation_filetime'])<=int(event['process_creation_filetime']),'Unbound parent process instance')
   instance=dict(create=event,events=[event],exit=None);active[pid]=instance;previous[pid]=instance;instances.append(instance)
  else:
   require(kind in ('module','unload','exception','exit') and pid in active,'Unbound process event: '+str(kind))
   instance=active[pid];instance['events'].append(event)
   if kind=='exit':
    require(type(event.get('exit_code')) is int,'Actual process exit required')
    instance['exit']=event;del active[pid]
 require(not closed or not active,'Incomplete process instance lifetime')
 return instances

def validate_module_events(events,pins,tool):
 """Replay a dedicated complete Windows debug-event stream against prefreeze.

 This observes actual process/module events only. It does not certify the
 target's source, command, generated products or successful build graph.
 """
 def event_key(path):
  require(isinstance(path,str) and path,'Actual module path unavailable')
  if path.startswith('\\\\?\\'):path=path[4:]
  require(Path(path).is_absolute(),'Absolute actual module path required')
  return key(path)
 known={key(r['path']):r for r in pins}
 for r in pins:check_row(r)
 rows=[json.loads(s) for s in Path(events).read_text(encoding='utf-8').splitlines()]
 require(len(rows)>=5 and rows[0].get('kind')=='begin' and rows[-1].get('kind')=='end','Complete framed event stream required')
 require(rows[0].get('version')==1,'Unsupported module event version')
 require(event_key(rows[0].get('target'))==event_key(str(tool)),'Root tool differs')
 require(all(type(r.get('seq')) is int and r['seq']==i for i,r in enumerate(rows)),'Contiguous module event sequence required')
 require(rows[-1].get('complete') is True and rows[-1].get('exit_code')==0,'Successful complete module capture required')
 instances=process_instances(rows);roots=[x for x in instances if x['create'].get('root') is True]
 require(len(roots)==1,'Unique root process required');root=roots[0];modules={};root_image=False
 for instance in instances:
  with_modules=False
  for r in instance['events'][1:-1]:
   if r['kind']=='module':
    require(r.get('path_error')==0,'Unresolved actual module event')
    k=event_key(r.get('path'));require(k in known,'Unfrozen actual module: '+r['path']);check_row(known[k]);modules[k]=known[k];with_modules=True
    if instance is root and k==event_key(str(tool)):root_image=True
   elif r['kind']=='exception':require(r.get('first_chance') is True,'Unhandled actual process exception')
  require(with_modules,'Complete successful process/module lifecycle required')
 require(root_image and root['exit']['exit_code']==0,'Complete successful process/module lifecycle required')
 return {'trace_file':row(events,'module-trace'),'process_count':len(instances),'modules':[modules[k] for k in sorted(modules)]}
def validate_contract(contract,pins,*,generated_inputs=()):
 require(contract.get('scope') in ('FIXTURE_POLICY_ONLY','OBSERVED_BUILD'),'Evidence scope required')
 require(contract.get('target') and contract.get('configuration'),'Selected target/configuration required')
 known={key(r['path']):r for r in pins};require(len(known)==len(pins),'Duplicate input pin')
 for r in pins:check_row(r)
 required=contract.get('required_inputs',[]);require(required,'Required consumed input graph is empty')
 for r in required:
  k=key(r['path']);require(k in known,str(r.get('kind') or 'Consumed')+' input absent: '+r['path']);check_row(r)
  require(r['sha256']==known[k]['sha256'],'Required graph identity differs: '+r['path'])
 produced={}
 for producer in contract.get('producers',[]):
  require(producer.get('exit_code')==0 and producer.get('command') and producer.get('tools') and producer.get('inputs') and producer.get('outputs'),'Incomplete generated producer')
  for r in producer['tools']+producer['inputs']:
   check_row(r);k=key(r['path']);require(k in known or k in produced,'Unfrozen producer input: '+r['path'])
   require(r['sha256']==(known.get(k) or produced[k])['sha256'],'Producer input identity differs')
  for r in producer['outputs']:check_row(r);produced[key(r['path'])]=r
 for p in generated_inputs:require(key(p) in produced,'Unproven generated input: '+p)
 selected=contract.get('selected_outputs',[])
 for r in selected:
  require(key(r['path']) in produced,'Selected output lacks completed producer: '+r['path']);check_row(r)
  require(r['sha256']==produced[key(r['path'])]['sha256'],'Selected output producer mismatch')
 return selected

def validate_receipt(receipt):
 require(receipt.get('schema_version')==2 and receipt.get('evidence',{}).get('scope')=='OBSERVED_BUILD','Observed version2 producer receipt required')
 require(receipt.get('exit_code')==0 and receipt.get('source_unchanged') is True,'Successful stable producer required')
 require(receipt['target']==receipt['evidence']['target'],'Selected target differs')
 selected=validate_contract(receipt['evidence'],receipt['input_pins'],generated_inputs=receipt['evidence'].get('generated_inputs',[]))
 actual={key(r['path']):r for r in selected}
 require(actual,'Selected outputs missing')
 repo=Path(receipt['repository'])
 permitted={'app':{'NeuRotic.exe'},'worker':{'NeuRotic.WindowWorker.exe','nvngx.dll_dlssnr.dll'},'core':{'OptiScaler.dll'},'discovery':{'NeuRotic.Discovery.exe','NeuRotic.Discovery.dll','NeuRotic.Discovery.deps.json','NeuRotic.Discovery.runtimeconfig.json'}}
 require(receipt['target'] in permitted and {Path(r['path']).name for r in selected}==permitted[receipt['target']],'Selected target output set differs')
 require(len(receipt['files'])==len(actual),'Receipt output inventory differs')
 for r in receipt['files']:
  p=repo/r['path'];owned_output(repo,p)
  require(key(p) in actual and r['sha256']==actual[key(p)]['sha256'] and r['bytes']==actual[key(p)]['bytes'],'Artifact producer mismatch: '+str(p))
 for r in receipt['evidence'].get('trace_files',[]):check_row(r)
 require(receipt['evidence'].get('capture_adapter')=='msvc-tlog-v1','Actual supported capture adapter required')
 paths=receipt['evidence'].get('capture_context',{})
 require(all(paths.get(k) for k in ('output','manifest','log','evaluation')),'Actual capture context required')
 require(Path(paths['manifest']).resolve()==Path(receipt['source_manifest_path']).resolve(),'Producer freeze binding differs')
 # Replay the installed adapter over retained actual tracking, not caller
 # labels, directory placement or newly measured object hashes.
 observed=capture(repo,paths['output'],paths['manifest'],paths['log'],paths['evaluation'],producer_session=paths.get('producer_session'),producer_session_sha256=paths.get('producer_session_sha256'),module_offering=paths.get('module_offering'),module_events=paths.get('module_events'),module_retirements=paths.get('module_retirements',()))
 require(observed==receipt['evidence'],'Retained actual producer trace differs from receipt')

def tlog_paths(p):
 """Return roots plus payloads for read provenance, never write ownership."""
 return list(dict.fromkeys(q for roots,values in tlog_records(p).values() for q in roots+values))

def same_pin(left,right):
 return key(left['path'])==key(right['path']) and all(left[k]==right[k] for k in ('sha256','bytes'))

def auxiliary_query_failures(offering,events,producer_images):
 """Account for a frozen optional query, never waive a producer failure.

 Extra create fields are supplied by the owned observer from held process
 handles at creation. A legacy trace, filename alone, or later PID lookup
 cannot establish this role. All module/lifetime checks run before this join.
 """
 rules=offering.get('optional_queries',[])
 require(isinstance(rules,list),'Auxiliary query role list required')
 modules={key(r['path']):r for r in offering['modules']}
 required={'id','role','image','parent_image','telemetry_module','command_line','allowed_exit_codes'}
 ids=set();bindings=set()
 def image_key(path):return key(path[4:] if path.startswith('\\\\?\\') else path)
 def filetime(value):
  return isinstance(value,str) and re.fullmatch('[1-9][0-9]{0,19}',value) and int(value)<=2**64-1
 def zero(event,name):return type(event.get(name)) is int and event[name]==0
 def command(value):
  if not isinstance(value,str) or not value or '\0' in value:return False
  try:return len(value.encode('utf-16-le'))<=65532
  except UnicodeEncodeError:return False
 for rule in rules:
  require(isinstance(rule,dict) and set(rule)==required,'Auxiliary query role shape differs')
  require(isinstance(rule['id'],str) and rule['id'] and rule['id'] not in ids,'Duplicate/empty auxiliary query role')
  ids.add(rule['id'])
  require(rule['role']=='OPTIONAL_TELEMETRY_NETWORK_QUERY' and rule['allowed_exit_codes']==[1]
          and type(rule['allowed_exit_codes'][0]) is int,'Unsupported auxiliary query role/exit contract')
  for field in ('image','parent_image','telemetry_module'):
   pin=rule[field]
   require(isinstance(pin,dict) and isinstance(pin.get('path'),str)
           and {'path','sha256','bytes'}<=set(pin),'Auxiliary image pin required')
   require(key(pin['path']) in modules and same_pin(pin,modules[key(pin['path'])]),'Auxiliary image differs from frozen module offering')
  query=key(rule['image']['path']);parent=key(rule['parent_image']['path']);telemetry=key(rule['telemetry_module']['path'])
  require(Path(rule['image']['path']).name.casefold()=='getmac.exe'
          and Path(rule['telemetry_module']['path']).name.casefold()=='microsoft.visualstudio.telemetry.dll'
          and Path(rule['parent_image']['path']).suffix.casefold()=='.exe'
          and len({query,parent,telemetry})==3,'Unsupported auxiliary query image roles')
  require(query not in producer_images,'Auxiliary query conflicts with named producer')
  require(command(rule['command_line']),'Exact bounded auxiliary command required')
  binding=(query,parent,telemetry,rule['command_line'])
  require(binding not in bindings,'Ambiguous auxiliary query role');bindings.add(binding)
 instances=process_instances(events);by_create={x['create']['seq']:x for x in instances}
 def first_image(instance):
  return next(r for r in instance['events'] if r['kind']=='module')
 result=[]
 for instance in instances:
  create=instance['create'];pid=create['pid'];exit_event=instance['exit']
  if exit_event['exit_code']==0:continue
  actual_image=first_image(instance)
  possible=[r for r in rules if image_key(actual_image['path'])==key(r['image']['path'])]
  if not possible:continue
  require(create.get('root') is not True and filetime(create.get('process_creation_filetime'))
          and zero(create,'process_creation_error') and zero(create,'parent_context_error')
          and zero(create,'command_line_error') and command(create.get('command_line'))
          and create.get('command_line_source')=='held-handle-PEB-at-create'
          and create.get('command_line_machine')=='AMD64','Authentic auxiliary create context required')
  parent_pid=create.get('parent_pid')
  require(type(parent_pid) is int and parent_pid!=pid,'Auxiliary parent process identity required')
  parent_instance=by_create.get(create.get('parent_create_seq'));parent=parent_instance['create'] if parent_instance else None
  require(parent is not None and parent['pid']==parent_pid
          and type(create.get('parent_create_seq')) is int and create['parent_create_seq']==parent['seq']
          and zero(parent,'process_creation_error') and filetime(parent.get('process_creation_filetime'))
          and create.get('parent_creation_filetime')==parent['process_creation_filetime']
          and int(parent['process_creation_filetime'])<=int(create['process_creation_filetime'])
          and parent['seq']<create['seq']<parent_instance['exit']['seq'],'Auxiliary parent instance/lifetime differs')
  # Replay the parent's actual loaded set at query creation; a DLL in another
  # process, a later load or a completed unload is not telemetry ancestry.
  loaded={}
  for event in parent_instance['events']:
   if event['seq']>=create['seq']:break
   if event['kind']=='module' and type(event.get('base')) is int and event['base']>0:loaded[event['base']]=event
   elif event['kind']=='unload':loaded.pop(event.get('base'),None)
  matches=[]
  for rule in possible:
   telemetry=[r for r in loaded.values() if image_key(r['path'])==key(rule['telemetry_module']['path'])]
   if (image_key(first_image(parent_instance)['path'])==key(rule['parent_image']['path'])
       and create['command_line']==rule['command_line'] and telemetry):matches.append((rule,telemetry))
  require(len(matches)==1,'Auxiliary command/telemetry ancestry missing or ambiguous')
  rule,telemetry=matches[0]
  require(exit_event['exit_code'] in rule['allowed_exit_codes'],'Unexplained auxiliary query exit')
  result.append(dict(id=rule['id'],role=rule['role'],disposition='NONFATAL_AUXILIARY_QUERY_FAILURE',
      pid=pid,create_seq=create['seq'],process_creation_filetime=create['process_creation_filetime'],
      parent_pid=parent_pid,parent_create_seq=parent['seq'],parent_creation_filetime=create['parent_creation_filetime'],
      command_line=create['command_line'],exit_event=exit_event,image=rule['image'],parent_image=rule['parent_image'],
      telemetry_module=rule['telemetry_module'],telemetry_load_events=telemetry))
 return result

def worker_context(repo,output,manifest,doc,ev,known,session_path,session_sha,offering_path,events_path,retirements):
 """Join one owner-frozen session with a completed observed MSBuild child.

 The build entry point proves freshness and actual argv before invoking the
 observer. Retained receipt replay checks these same immutable inputs, rather
 than launching a new observer or accepting custom replay as module proof.
 """
 require(session_path and session_sha,'Worker producer context required')
 require(offering_path and events_path,'Module capture context required')
 require(doc.get('target')=='worker' and key(doc.get('repository',''))==key(repo), 'Worker source context differs')
 require(ev.get('configuration')=='Release|x64','Worker configuration differs')
 session_path=Path(session_path);require(session_path.is_absolute(),'Absolute producer session required')
 require(re.fullmatch('[0-9a-f]{64}',session_sha) and file_sha(session_path)==session_sha,'Session identity differs')
 session=json.loads(session_path.read_bytes())
 require(session.get('schema_version')==1 and session.get('scope')=='LOCAL_PRODUCER_CAPTURE_ONLY','Local producer session scope required')
 require(key(session['repository'])==key(repo) and key(session['output_root'])==key(output),'Session context differs')
 require(same_pin(session['source_manifest'],row(manifest)),'Session freeze differs')
 capture_root=owned_output(repo,session['capture_root'],tree=True)
 require(capture_root.resolve().is_relative_to(output.resolve()) and key(session['forwarder_tracking_root'])==key(output/'forwarder-obj'),'Session output context differs')
 plans=session['actions'];ids=[p['id'] for p in plans]
 require(plans and len(ids)==len(set(ids)),'Duplicate or empty session action set')
 require(all(p['configuration']=='Release|x64' for p in plans),'Session action configuration differs')
 require(all(p['kind'] in ('dxc-direct','forwarder-identity') for p in plans),'Unsupported worker custom producer')
 outputs=[p for plan in plans for p in plan['outputs']]
 require(len({key(p) for p in outputs})==len(outputs),'Duplicate custom output producer')
 for p in outputs:
  check_native_write(repo,output,Path(p));require(key(p) not in known,'Generated output prefrozen instead of named producer: '+p)
 config=doc.get('module_capture') or {}
 require(config.get('observer') and key(config.get('offering',''))==key(offering_path) and key(config.get('events',''))==key(events_path),'Module capture context differs')
 for p in (config['observer'],offering_path):
  require(key(p) in known,'Module observer/offering absent from freeze: '+str(p));check_row(known[key(p)])
 offering=json.loads(Path(offering_path).read_bytes())
 require(offering.get('schema_version')==1 and offering.get('target')=='worker' and offering.get('configuration')=='Release|x64' and key(offering.get('repository',''))==key(repo) and key(offering.get('output',''))==key(output),'Module offering context differs')
 argv=offering.get('argv');require(isinstance(argv,list) and argv and argv==ev.get('argv'),'Module observed invocation differs')
 session_args=[arg.split('=',1)[1] for arg in argv if isinstance(arg,str) and arg.casefold().startswith('/p:neuroticcapturesession=')]
 require(len(session_args)==1 and Path(session_args[0]).is_absolute() and key(session_args[0])==key(session_path),'Session argument differs')
 modules=offering.get('modules',[]);require(modules and len({key(r['path']) for r in modules})==len(modules),'Missing/duplicate module offering')
 for r in modules:
  require(key(r['path']) in known and same_pin(r,known[key(r['path'])]),'Module image absent/different from freeze: '+r['path'])
  check_row(r)
 events_path=owned_output(repo,events_path)
 require(events_path.resolve().is_relative_to(output.resolve()),'Module event output context differs')
 observed=validate_module_events(events_path,modules,argv[0]);observed['argv']=argv
 # The shared event validator establishes root success and a closed process
 # tree. Independently account for non-root failures using typed retirement
 # or a positively observed optional query, never a failed producer tool.
 events=[json.loads(line) for line in events_path.read_text(encoding='utf-8').splitlines()]
 instances=process_instances(events);root_instance=next(x for x in instances if x['create'].get('root') is True)
 root=root_instance['create']['pid'];root_exit=root_instance['exit']
 failed={x['create']['seq']:x for x in instances if x['exit']['exit_code']!=0};retired=set();retirement_rows=[]
 def image_key(path):
  # Match the shared event validator's Windows extended-path spelling rule.
  return key(path[4:] if path.startswith('\\\\?\\') else path)
 allowed={key(p) for p in (config.get('retirements') or [])}
 for path in retirements:
  require(key(path) in allowed,'Undeclared telemetry retirement')
  path=owned_output(repo,path);require(path.resolve().is_relative_to(output.resolve()),'Telemetry retirement output context differs')
  record=json.loads(path.read_bytes());pid=record.get('process',{}).get('ProcessId')
  ft=record['process'].get('creation_filetime')
  candidates=[x for x in failed.values() if x['create']['pid']==pid
              and (ft is None or creation_filetime(x['create'].get('process_creation_filetime'))
                   and type(ft) is int and ft==int(x['create']['process_creation_filetime']))]
  require(record.get('stage')=='POST_ROOT_EXIT_OWNED_TELEMETRY_STOP' and len(candidates)==1 and candidates[0] is not root_instance,'Telemetry retirement instance missing/ambiguous')
  instance=candidates[0];create=instance['create'];instance_seq=create['seq']
  if ft is not None or sum(x['create']['pid']==pid for x in instances)>1:
   require(type(ft) is int and ft>0 and creation_filetime(create.get('process_creation_filetime'))
           and type(create.get('process_creation_error')) is int and create['process_creation_error']==0
           and ft==int(create['process_creation_filetime']),'Telemetry retirement creation identity differs')
  require(instance_seq not in retired,'Duplicate telemetry retirement')
  images=[r for r in instance['events'] if r['kind']=='module' and image_key(r['path'])==image_key(record['process']['ExecutablePath'])]
  require(len(images)==1 and Path(images[0]['path']).name.casefold()=='vctip.exe' and instance['exit']['seq']>root_exit['seq'] and record.get('root_exit')==[root_exit],'Telemetry retirement/root identity differs')
  retired.add(instance_seq);retirement_rows.append(row(path,'telemetry-retirement'))
 producer_images={key(argv[0])}|{key(p['command'][0]) for p in plans}
 producer_images|={key(r['path']) for r in known.values() if Path(r['path']).name.casefold() in ('cl.exe','c1xx.dll','c2.dll','link.exe','rc.exe')}
 auxiliary=auxiliary_query_failures(offering,events,producer_images)
 auxiliary_instances={r['create_seq'] for r in auxiliary}
 require(not retired&auxiliary_instances and set(failed)==retired|auxiliary_instances,'Unexplained nonzero producer child exit')
 if 'optional_queries' in offering:observed['auxiliary_queries']=auxiliary
 observed['offering']=row(offering_path,'module-offering');observed['retirements']=retirement_rows
 replay=replay_actions(session_path)
 require(replay['scope']=='LOCAL_PRODUCER_CAPTURE_ONLY' and replay['requires']=='05_OBSERVED_TASK_MODULE_AND_NATIVE_TARGET_CLOSURE','Custom replay closure contract differs')
 records=replay['producers'];require(len(records)==len(plans) and {r['id'] for r in records}==set(ids),'Named custom producer set differs')
 images={key(r['path']) for r in observed['modules']}
 for record,plan in zip(records,plans):
  require(record['id']==plan['id'] and record.get('kind')==plan['kind'] and record.get('scope')=='LOCAL_PRODUCER_CAPTURE_ONLY','Custom producer context differs')
  command=record.get('command');require(isinstance(command,list) and command and key(command[0]) in {key(r['path']) for r in record['tools']} and key(command[0]) in images,'Custom command image absent from actual module envelope')
  if plan['kind']=='forwarder-identity':
   require(Path(plan['outputs'][0]).read_bytes()==identity_bytes(plan['inputs'][0]),'Forwarder identity bytes differ')
 return records,replay['trace_files']+[row(session_path,'producer-session')]+retirement_rows,observed

def capture(repo,output,manifest,log,evaluation,*,producer_session=None,producer_session_sha256=None,module_offering=None,module_events=None,module_retirements=()):
 """Join current native tracking with named custom and closed module evidence.

 No recursive binary discovery, generated-folder exemption, or custom replay
 alone can establish an observed worker build. App's native-only path remains
 unchanged; the complete worker proof context is mandatory.
 """
 repo=Path(repo);output=owned_output(repo,output,tree=True);doc=json.loads(Path(manifest).read_bytes())
 require(doc['target'] in ('app','worker'),'No complete capture adapter for selected producer; receipt refused')
 require(doc.get('native_only',False) or doc['target']=='worker','Discovery needs separately observed producer evidence')
 pins=doc['files'];known={key(r['path']):r for r in pins};require(len(known)==len(pins),'Duplicate input pin')
 ev=json.loads(Path(evaluation).read_bytes());helper=Path(__file__).with_name('ProducerCapture.py')
 require(key(helper) in known,'Shared native tracking parser absent from freeze: '+str(helper));check_row(known[key(helper)])
 graph=[known[key(helper)]]
 for r in ev['inputs']:
  require(key(r['path']) in known,str(r.get('kind') or 'Consumed')+' input absent: '+r['path']);graph.append(r)
 custom=[];traces=[];module_observation=None
 if doc['target']=='worker':
  custom,traces,module_observation=worker_context(repo,output,manifest,doc,ev,known,producer_session,producer_session_sha256,module_offering,module_events,module_retirements)
  graph += module_observation['modules']+[known[key(module_offering)],known[key(doc['module_capture']['observer'])]]
 else:require(not any((producer_session,producer_session_sha256,module_offering,module_events,module_retirements)),'Worker producer context on non-worker target')
 producers=[];generated=[];alloutputs={}
 roots=[output/'obj']+([output/'forwarder-obj'] if doc['target']=='worker' else [])
 families=[]
 for root in roots:
  for read in sorted(root.rglob('*.read.*.tlog')):
   stem=read.name.split('.read.',1)[0];command=read.with_name(read.name.replace('.read.','.command.'));write=read.with_name(read.name.replace('.read.','.write.'))
   require(command.is_file() and write.is_file(),'Missing command/write tracking for '+str(read))
   require(stem.lower() in ('cl','link','rc'),'Unmodeled producer tracking: '+stem)
   families.append((stem,read,command,write))
 require(families and any(s.lower()=='link' for s,*_ in families),'Actual native compiler/link tracking required')
 def add_outputs(outputs):
  for r in outputs:
   check_native_write(repo,output,Path(r['path']));check_row(r)
   if module_observation:
    require(key(r['path']) not in known,'Generated output prefrozen instead of named producer: '+r['path'])
    require(key(r['path']) not in alloutputs,'Duplicate output producer: '+r['path'])
   alloutputs[key(r['path'])]=r
 pending=list(families)
 while pending or custom:
  progress=False
  for record in custom[:]:
   if any(key(r['path']) not in known and key(r['path']) not in alloutputs for r in record['inputs']):continue
   inputs=[]
   for r in record['inputs']:
    k=key(r['path'])
    if k in alloutputs:require(same_pin(r,alloutputs[k]),'Custom generated input identity differs');inputs.append(alloutputs[k]);generated.append(r['path'])
    else:require(not Path(r['path']).resolve().is_relative_to(output.resolve()),'Generated input requires named producer: '+r['path']);inputs.append(r)
   if record.get('native_parent'):
    for r in record['native_parent']['selected_outputs']:
     require(key(r['path']) in alloutputs and same_pin(r,alloutputs[key(r['path'])]),'Actual forwarder parent producer absent/different')
   tools=[r for r in record['tools'] if key(r['path']) in {key(m['path']) for m in module_observation['modules']}]
   require(tools,'Custom command module evidence absent')
   outputs=record['outputs'];add_outputs(outputs)
   producers.append(dict(kind=record['kind'],id=record['id'],command=record['command'],tools=tools,offered_tools=record['tools'],inputs=inputs,outputs=outputs,exit_code=0))
   graph+=inputs+record['tools'];custom.remove(record);progress=True
  for family in pending[:]:
   stem,read,command,write=family;inputs=[];deps=[]
   reads=tlog_records(read);writes=tlog_records(write)
   commands=tlog_records(command,command=True)
   tracking_roots=validate_native_tracking_roots(stem,reads,writes,commands)
   for p in dict.fromkeys(p for roots,values in reads.values() for p in roots+values):
    if key(p) in alloutputs and (module_observation or key(p) not in known):inputs.append(alloutputs[key(p)]);generated.append(str(p))
    elif key(p) in known:
     require(not module_observation or not p.resolve().is_relative_to(output.resolve()),'Generated input prefrozen instead of named producer: '+str(p))
     r=row(p,'link' if p.suffix.lower()=='.lib' else 'source' if p.suffix.lower() in ('.cpp','.c') else 'resource' if p.suffix.lower() in ('.rc','.ttf','.otf') else 'include')
     require(r['sha256']==known[key(p)]['sha256'],'Consumed input changed: '+str(p));inputs.append(r)
    else:deps.append(p)
   if deps:
    if all(p.is_relative_to(output) for p in deps):continue
    raise ValueError('Consumed input absent from freeze: '+str(deps[0]))
   toolnames={'cl':['cl.exe','c1xx.dll','c2.dll'],'link':['link.exe'],'rc':['rc.exe']}[stem.lower()]
   tools=[]
   for name in toolnames:
    matches=[r for r in pins if Path(r['path']).name.lower()==name];require(len(matches)==1,'Actual tool pin missing/ambiguous: '+name);tools+=matches
   if module_observation:
    require(all(key(r['path']) in {key(m['path']) for m in module_observation['modules']} for r in tools),'Native tool module absent from actual envelope')
   write_values=list(dict.fromkeys(p for roots,values in writes.values() for p in values))
   for p in write_values:check_native_write(repo,output,p)
   outputs=[row(p) for p in write_values if p.is_file() and p.suffix.lower() in ('.obj','.res','.exe','.dll','.lib','.pdb')]
   require(outputs,'Producer writes missing: '+str(write));cmd=command.read_text(encoding='utf-16');require(cmd.strip(),'Empty actual producer command')
   add_outputs(outputs);producer=dict(kind=stem,command=cmd,tools=tools,inputs=inputs,outputs=outputs,exit_code=0)
   if tracking_roots:producer['tracking_roots']=tracking_roots
   producers.append(producer)
   traces.extend(row(p) for p in (read,write,command));graph+=inputs;pending.remove(family);progress=True
  require(progress,'Unproven generated inputs or cyclic producer graph')
 names=['bin/NeuRotic.exe'] if doc['target']=='app' else ['bin/NeuRotic.WindowWorker.exe','bin/nvngx.dll_dlssnr.dll']
 selected=[]
 for name in names:
  p=output/name;require(key(p) in alloutputs,'Selected target output not produced: '+str(p));selected.append(alloutputs[key(p)])
 graph=[r for r in graph if key(r['path']) in known]
 context={'output':str(output),'manifest':str(Path(manifest)),'log':str(Path(log)),'evaluation':str(Path(evaluation))}
 if module_observation:
  context.update(producer_session=str(producer_session),producer_session_sha256=producer_session_sha256,module_offering=str(module_offering),module_events=str(module_events),module_retirements=list(module_retirements))
  traces += [module_observation['trace_file'],module_observation['offering']]
 evidence={'scope':'OBSERVED_BUILD','capture_adapter':'msvc-tlog-v1','capture_context':context,'target':doc['target'],'configuration':'Release|x64','required_inputs':graph,'producers':producers,'selected_outputs':selected,'generated_inputs':sorted(set(generated)),'trace_files':traces+[row(evaluation),row(log)]}
 if module_observation:evidence['module_observation']=module_observation
 validate_contract(evidence,pins,generated_inputs=evidence['generated_inputs']);return evidence


if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--repo',required=True);p.add_argument('--output',required=True);p.add_argument('--manifest',required=True);p.add_argument('--log',required=True);p.add_argument('--evaluation',required=True);p.add_argument('--result',required=True);p.add_argument('--producer-session');p.add_argument('--producer-session-sha256');p.add_argument('--module-offering');p.add_argument('--module-events');p.add_argument('--module-retirement',action='append',default=[]);a=p.parse_args()
 result=capture(a.repo,a.output,a.manifest,a.log,a.evaluation,producer_session=a.producer_session,producer_session_sha256=a.producer_session_sha256,module_offering=a.module_offering,module_events=a.module_events,module_retirements=a.module_retirement)
 write_owned(a.repo,a.result,(json.dumps(result,indent=2)+'\n').encode())
