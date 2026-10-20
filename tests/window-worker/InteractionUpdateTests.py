"""Exercise the exact update against an isolated installed-baseline fixture."""
from pathlib import Path
import argparse,hashlib,json,os,shutil,subprocess,zipfile

ROOT=Path(__file__).resolve().parents[2]
args=argparse.ArgumentParser();args.add_argument('--comparison',action='store_true');args.add_argument('--performance',action='store_true');args.add_argument('--throughput',action='store_true');args.add_argument('--resolution',action='store_true');args.add_argument('--scheduling',action='store_true');options=args.parse_args();comparison=options.comparison;scheduling=options.scheduling;resolution=options.resolution or scheduling;throughput=options.throughput or resolution;performance=options.performance or throughput
assert sum([options.comparison,options.performance,options.throughput,options.resolution,options.scheduling])<=1
EVIDENCE=ROOT/('docs/implementation-evidence/anything-scheduling-20261003' if scheduling else 'docs/implementation-evidence/anything-resolution-20261003' if resolution else 'docs/implementation-evidence/anything-throughput-20261003' if throughput else 'docs/implementation-evidence/anything-performance-20261003' if performance else 'docs/implementation-evidence/anything-comparison-20261003' if comparison else 'docs/implementation-evidence/anything-interaction-20261003')
WORK=ROOT/'builds/anything-interaction-update'
INSTALL=WORK/'installed'
UPDATE=WORK/'update'
receipt=json.loads((EVIDENCE/'update-receipt.json').read_text())
sha=lambda data:hashlib.sha256(data).hexdigest()
assert not WORK.exists(),'Preserve prior incomplete fixture'
for parent in WORK.parents:
 if parent.exists():assert not parent.stat(follow_symlinks=False).st_file_attributes&0x400
checks=[];logs=[]
def check(ok,label):
 assert ok,label
 checks.append(label);print('PASS',label,flush=True)
def extract(archive,target,baseline=False):
 with zipfile.ZipFile(archive) as package:
  for entry in package.infolist():
   name=entry.filename.removeprefix('NeuRotic/') if baseline else entry.filename
   if baseline and (name.startswith('Source/') or name.startswith('Tools/WindowWorker/apps/') or name.startswith('Tools/WindowWorker/tests/') or name.startswith('Tools/WindowWorker/tools/') or name.startswith('Tools/WindowWorker/references/')):continue
   assert ':' not in name and '..' not in Path(name).parts
   path=target/name;assert path.is_relative_to(target)
   if not entry.is_dir():path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(package.read(entry))
def invoke(expect=True):
 result=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(UPDATE/'Apply-AnythingUpdate.ps1'),'-InstallRoot',str(INSTALL)],capture_output=True,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
 logs.append(result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace'))
 check((result.returncode==0)==expect,'updater accepted' if expect else 'updater refused unsafe input')
 return result
try:
 WORK.mkdir()
 archive=Path(receipt['path']);check(sha(archive.read_bytes())==receipt['sha256'],'exact delivered update ZIP hash')
 extract('C:/NeuRotic/_exports/NeuRotic-Current-Flagship-4134edeb.zip',INSTALL,True);extract(archive,UPDATE)
 manifest=json.loads((UPDATE/'UPDATE-MANIFEST.json').read_text())
 paths=[INSTALL/file['path'] for file in manifest['files']]
 original={path:path.read_bytes() for path in paths}
 sentinel=INSTALL/'user-data/Anything/Models/private-model-sentinel';sentinel.parent.mkdir(parents=True);sentinel.write_bytes(b'private model unchanged')
 settings=INSTALL/'user-data/Anything/anything.json';settings.write_text('{"modelPath":"private-model-sentinel","personal":true}')
 sentinels={sentinel:sentinel.read_bytes(),settings:settings.read_bytes()}
 target=paths[0];target.write_bytes(b'unsupported baseline');bad=target.read_bytes();invoke(False)
 check(target.read_bytes()==bad and all(path.read_bytes()==original[path] for path in paths[1:]),'wrong baseline refusal changes no files');target.write_bytes(original[target])
 payload=UPDATE/manifest['files'][2]['path'];good_payload=payload.read_bytes();payload.write_bytes(b'tampered forwarder');invoke(False)
 check(all(path.read_bytes()==original[path] for path in paths),'tampered matching dependency refusal changes no files');payload.write_bytes(good_payload)
 locked=WORK/'hold-file.ps1';ready=WORK/'lease-ready'
 locked.write_text("$s=[IO.File]::Open($args[0],[IO.FileMode]::Open,[IO.FileAccess]::Read,[IO.FileShare]::None);[IO.File]::WriteAllText($args[1],'ready');try{Start-Sleep -Seconds 30}finally{$s.Dispose()}")
 holder=subprocess.Popen(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(locked),str(paths[1]),str(ready)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,creationflags=subprocess.CREATE_NO_WINDOW)
 try:
  import time
  until=time.monotonic()+8
  while not ready.exists() and time.monotonic()<until:time.sleep(.02)
  check(ready.exists(),'controlled running-file lease acquired');invoke(False)
 finally:
  holder.terminate();holder.wait(timeout=5)
  (EVIDENCE/'running-lease.log').write_bytes(holder.stdout.read())
 check(all(path.read_bytes()==original[path] for path in paths),'running binary refusal changes no files')
 env=os.environ.copy();env['NEUROTIC_HUB_FIXTURE_ROOT']=str(INSTALL/'user-data');env['PSMODULEPATH']=str(Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/Modules')
 baseline_test=subprocess.run([str(INSTALL/'NeuRotic.Hub.exe'),'--self-test'],capture_output=True,env=env,cwd=INSTALL,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
 (EVIDENCE/'hub-baseline-self-test.log').write_bytes(baseline_test.stdout+baseline_test.stderr)
 baseline_failures=[line for line in baseline_test.stdout.decode(errors='replace').splitlines() if line.startswith('FAIL ')]
 native_fixture=WORK/'native-selftest-fixture'
 if native_fixture.exists():
  for directory,dirs,files in os.walk(native_fixture,followlinks=False):
   assert not Path(directory).stat(follow_symlinks=False).st_file_attributes&0x400
   assert all(not Path(directory,name).stat(follow_symlinks=False).st_file_attributes&0x400 for name in dirs+files)
  shutil.rmtree(native_fixture)
 invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'all three installed outputs match the final receipt')
 if comparison or performance:
  prior=json.loads((ROOT/'docs/implementation-evidence/anything-interaction-20261003/update-receipt.json').read_text())
  check(prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256'],'previous delivered interaction update is intact')
  with zipfile.ZipFile(prior['path']) as old:
   for file in manifest['files']:(INSTALL/file['path']).write_bytes(old.read(file['path']))
  invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'previous interaction update upgrades to the matched comparison fix')
 if performance:
  prior=json.loads((ROOT/'docs/implementation-evidence/anything-comparison-20261003/update-receipt.json').read_text())
  check(prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256'],'previous delivered comparison fix is intact')
  with zipfile.ZipFile(prior['path']) as old:
   for file in manifest['files']:(INSTALL/file['path']).write_bytes(old.read(file['path']))
  invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'latest comparison fix upgrades to the performance trace update')
  launcher=subprocess.run(['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(UPDATE/'Trace-Anything.ps1'),'-InstallRoot',str(INSTALL),'-NoLaunch'],capture_output=True,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
  (EVIDENCE/'trace-launcher.log').write_bytes(launcher.stdout+launcher.stderr)
  check(launcher.returncode==0,'trace launcher validates installed matched binaries without launching')
 if throughput:
  prior=json.loads((ROOT/'docs/implementation-evidence/anything-performance-20261003/update-receipt.json').read_text())
  check(prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256'],'previous delivered performance trace update is intact')
  with zipfile.ZipFile(prior['path']) as old:
   for file in manifest['files']:(INSTALL/file['path']).write_bytes(old.read(file['path']))
  invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'installed performance trace version upgrades to the matched throughput update')
 if resolution:
  prior=json.loads((ROOT/'docs/implementation-evidence/anything-throughput-20261003/update-receipt.json').read_text())
  check(prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256'],'previous throughput update and equivalent full-copy runtime are intact')
  with zipfile.ZipFile(prior['path']) as old:
   for file in manifest['files']:(INSTALL/file['path']).write_bytes(old.read(file['path']))
  invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'latest throughput/full copy upgrades to NR resolution controls')
 if scheduling:
  prior=json.loads((ROOT/'docs/implementation-evidence/anything-resolution-20261003/update-receipt.json').read_text())
  check(prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256'],'previous resolution update/full-copy runtime is intact')
  with zipfile.ZipFile(prior['path']) as old:
   for file in manifest['files']:(INSTALL/file['path']).write_bytes(old.read(file['path']))
  invoke();check(all(sha((INSTALL/file['path']).read_bytes())==file['sha256'] for file in manifest['files']),'latest resolution/full copy upgrades to event-driven scheduling')
 invoke();check(all(path.read_bytes()==data for path,data in sentinels.items()),'idempotent update preserves settings and private model')
 worker=INSTALL/'Tools/WindowWorker/NeuRotic.WindowWorker.exe'
 result=subprocess.run([str(worker),'--control'],input=b'{"command":"status"}\n{"command":"quit"}\n',capture_output=True,env=env,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
 events=[json.loads(line) for line in result.stdout.splitlines()]
 check(result.returncode==0 and events[0]['apiVersion']==1 and events[0]['model']['ready']==False and 'interaction' in events[0] and events[-1]['event']=='shutdown','extracted worker stays model locked and shuts down through compatible protocol')
 result=subprocess.run([str(INSTALL/'NeuRotic.Hub.exe'),'--self-test'],capture_output=True,env=env,cwd=INSTALL,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
 (EVIDENCE/'hub-patched-self-test.log').write_bytes(result.stdout+result.stderr)
 patched_failures=[line for line in result.stdout.decode(errors='replace').splitlines() if line.startswith('FAIL ')]
 check((baseline_test.returncode==result.returncode and patched_failures==baseline_failures) or result.returncode==0,'patched Hub adds no native self-test failures over installed baseline')
 native_comparison={'baselineExitCode':baseline_test.returncode,'patchedExitCode':result.returncode,'baselineFailures':baseline_failures,'patchedFailures':patched_failures}
 loading=subprocess.run([str(INSTALL/'NeuRotic.Hub.exe'),'--loading-ui-test'],capture_output=True,env=env,cwd=INSTALL,timeout=20,creationflags=subprocess.CREATE_NO_WINDOW)
 (EVIDENCE/'hub-loading-ui.log').write_bytes(loading.stdout+loading.stderr)
 check(loading.returncode==0,'patched installed App loading and cancel interaction checks')
 preview=WORK/'preview'
 result=subprocess.run([str(INSTALL/'NeuRotic.Hub.exe'),'--smoke-test',str(preview)],capture_output=True,env=env,cwd=INSTALL,timeout=60,creationflags=subprocess.CREATE_NO_WINDOW)
 smoke=json.loads((preview/'smoke.json').read_text())
 check(result.returncode==0 and smoke['pagesRendered']==6 and smoke['brandEmbedded'] and not any(smoke[key] for key in ['proxyLoaded','ngxLoaded','captureStarted']),'patched installed App renders six pages without model/capture activation')
 for path in preview.iterdir():
  if path.is_file():shutil.copyfile(path,EVIDENCE/('patched-preview-'+path.name))
 check(all(path.read_bytes()==data for path,data in sentinels.items()),'App/worker verification preserves settings and private model sentinel')
 (EVIDENCE/'patch-tests.json').write_text(json.dumps({'status':'PASS','checks':checks,'archiveSha256':receipt['sha256'],'nativeComparison':native_comparison,'smoke':smoke,'proprietaryProviderInvoked':False},indent=2)+'\n')
finally:
 (EVIDENCE/'patch-installer.log').write_text('\n'.join(logs))
 assert WORK.is_relative_to(ROOT/'builds') and WORK==ROOT/'builds/anything-interaction-update'
 for directory,dirs,files in os.walk(WORK,followlinks=False):
  assert not Path(directory).stat(follow_symlinks=False).st_file_attributes&0x400
  assert all(not Path(directory,name).stat(follow_symlinks=False).st_file_attributes&0x400 for name in dirs+files)
 if WORK.exists():shutil.rmtree(WORK)
