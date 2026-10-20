"""Small changed-files update for the exact previously delivered focused App patch."""
import argparse, importlib.util, json, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'docs/implementation-evidence/app-compact-inspection-20261003'
def module(name,file):
 spec=importlib.util.spec_from_file_location(name,ROOT/'tools/hub'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
base=module('app_base','Known-AppBaseline.py');common=module('app_delta','Package-AppDelta.py')
sha,encode,read,git=common.sha,common.encode,common.read,common.git
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);parser.add_argument('--source-commit',required=True);parser.add_argument('--preview',action='store_true');args=parser.parse_args()
 source=git('rev-parse',args.source_commit+'^{commit}');output=Path(args.output).absolute()
 if output.exists():raise RuntimeError('Preserve previous delivery')
 common.subprocess.check_call(['git','-C',str(ROOT),'merge-base','--is-ancestor',source,'HEAD'])
 old,previous=base.navigation_app_files();desired=dict(old);desired['NeuRotic.Hub.exe']=read(common.BIN/'NeuRotic.Hub.exe')
 changed=set(git('diff','--name-only',previous['sourceCommit'],'--','apps/NeuRoticHub','tests/hub','tools/hub','tools/packaging/customer').splitlines())
 changed.update(git('ls-files','--others','--exclude-standard','apps/NeuRoticHub','tests/hub','tools/hub','tools/packaging/customer').splitlines())
 for name in sorted(changed):
  path=ROOT/name
  if path.is_file() and path.suffix.lower() in ('.cpp','.h','.cs','.ps1','.cmd','.py','.vcxproj','.txt'):desired['Source/Hub/'+name]=read(path)
  elif not path.exists():desired.pop('Source/Hub/'+name,None)
 helper=read(ROOT/'tools/packaging/customer/support/NeuRotic-HubProtocol.ps1')
 if b'NeuRotic-HubHistory.ps1' in helper:
  raise RuntimeError('This historical compact patch cannot carry current installer dependencies. Use Build-ReleaseBundle.py for a complete matched bundle.')
 for folder in ('support','packages/current/support'):desired[folder+'/NeuRotic-HubProtocol.ps1']=helper
 inner=json.loads(desired['packages/current/support/PACKAGE-MANIFEST.json']);inner['app_support_commit']=source
 for row in inner['files']:
  raw=desired['packages/current/'+row['path'].replace('\\','/')];row.update(sha256=sha(raw),bytes=len(raw))
 desired['packages/current/support/PACKAGE-MANIFEST.json']=encode(inner)
 registry=json.loads(desired['support/Hub-Packages.json'])
 for package in registry['packages']:
  if package['relativeRoot'].replace('\\','/').rstrip('/')=='../packages/current':package['manifestSha256']=sha(desired['packages/current/support/PACKAGE-MANIFEST.json'])
 desired['support/Hub-Packages.json']=encode(registry)
 receipt=json.loads(old['ARCHIVE-MANIFEST.json']);receipt.update(hub_source_commit=source,hub_sha256=sha(desired['NeuRotic.Hub.exe']),app_update_base_sha256=base.NAVIGATION_SHA,files=common.inventory({n:raw for n,raw in desired.items() if n!='ARCHIVE-MANIFEST.json'}))
 desired['ARCHIVE-MANIFEST.json']=encode(receipt)
 rows=[]
 for name in sorted(set(old)|set(desired)):
  if old.get(name)==desired.get(name):continue
  assert name in ('NeuRotic.Hub.exe','ARCHIVE-MANIFEST.json','support/NeuRotic-HubProtocol.ps1','support/Hub-Packages.json','packages/current/support/NeuRotic-HubProtocol.ps1','packages/current/support/PACKAGE-MANIFEST.json') or name.startswith('Source/Hub/')
  raw=desired.get(name);rows.append({'path':name,'action':'put' if raw is not None else 'remove','sha256':sha(raw) if raw is not None else None,'previousSha256':sha(old[name]) if name in old else None,'bytes':len(raw) if raw is not None else 0})
 if not rows or desired['NeuRotic.Hub.exe']==old['NeuRotic.Hub.exe']:raise RuntimeError('No new App binary')
 # Guard the matched prior runtime, including persistent-file support and Small bundle.
 changed_paths={r['path'] for r in rows}
 dependencies={r['path']:r['sha256'] for r in previous['dependencies'] if r['path'] not in changed_paths}
 for row in previous['files']:
  name=row['path']
  if row['action']=='put' and name not in {r['path'] for r in rows} and not name.startswith(('Source/','verification/')):dependencies[name]=sha(old[name])
 dependencies=[{'path':name,'sha256':digest} for name,digest in sorted(dependencies.items())]
 identity=sha(encode(rows))[:16];manifest={'kind':'neurotic-app-delta','schemaVersion':1,'id':identity,'sourceCommit':source,'baseArchiveSha256':base.NAVIGATION_SHA,'files':rows,'dependencies':dependencies}
 if not args.preview:
  build=json.loads(read(EVIDENCE/'native-build.json'));assert build['exitCode']==0 and build['sourceCommit']==source and build['hubSha256']==sha(desired['NeuRotic.Hub.exe'])
  for row in build['inputs']:assert sha(read(ROOT/row['path']))==row['sha256'],row['path']
  for name in ('loading-ui.log','focused-ui.log','protocol-suite.log','native-self-tests.log','history-tests.log'):
   log=read(EVIDENCE/name);assert b'PASS ' in log and b'FAIL ' not in log,name
  verified=json.loads(read(EVIDENCE/'delta-compatibility.json'));assert verified['passed'] and verified['id']==identity and verified['manifestSha256']==sha(encode(manifest))
  assert not git('status','--porcelain'),'Final delivery requires a clean recorded source/evidence commit'
 files={'APP-UPDATE-MANIFEST.json':encode(manifest),'Apply-AppUpdate.ps1':read(ROOT/'tools/hub/Apply-AppUpdate.ps1'),'NeuRotic-AppUpdateSafety.cs':read(ROOT/'tools/hub/NeuRotic-AppUpdateSafety.cs')}
 for row in rows:
  if row['action']=='put':files['NeuRotic/'+row['path']]=desired[row['path']]
 for name,option in (('Apply NeuRotic App Update.cmd',''),('Rollback NeuRotic App Update.cmd',' -Rollback')):
  files[name]=(f'@echo off\r\nsetlocal\r\ntitle NeuRotic Application Update\r\nset "PSModulePath=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\Modules"\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-AppUpdate.ps1"{option} %*\r\nset "result=%ERRORLEVEL%"\r\npause\r\nexit /b %result%\r\n').encode('ascii')
 files['READ FIRST.txt']=b'NEUROTIC COMPACT HEADER / GAME CHECK UPDATE\r\n\r\nRequires the previous NeuRotic-App-63723cdc-Update.zip patch to be installed.\r\nClose NeuRotic. Extract this ZIP separately, run Apply NeuRotic App Update.cmd, and choose your installed NeuRotic.Hub.exe. The updater validates the exact previous App and dependencies before changing anything.\r\nThis update changes the App, its installer inspection helper, and corresponding source/receipts. Settings, Streamline, private NR files, renderer, discovery and Window Worker are preserved.\r\nRoutine game checks inspect current managed files and send a compact summary. Full recovery validation remains mandatory before changes. The top navigation and NR Anything control are compact; readiness appears as three status lights in the header.\r\nRollback NeuRotic App Update.cmd restores the previous App and helper.\r\n'
 output.parent.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name,raw in sorted(files.items()):z.writestr(name,raw)
 with zipfile.ZipFile(output) as z:
  assert z.testzip() is None
  for name,raw in files.items():assert z.read(name)==raw
 print(json.dumps({'archive':str(output),'sha256':sha(read(output)),'bytes':output.stat().st_size,'changedFiles':len(rows),'members':len(files),'id':identity,'sourceCommit':source,'hubSha256':sha(desired['NeuRotic.Hub.exe']),'matchingBaselineDependencies':len(dependencies)},indent=2))
if __name__=='__main__':main()
