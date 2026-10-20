"""Small changed-files update for the exact previously delivered focused App patch."""
import argparse, importlib.util, json, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'docs/implementation-evidence/app-readable-tabs-20261003'
def module(name,file):
 spec=importlib.util.spec_from_file_location(name,ROOT/'tools/hub'/file);m=importlib.util.module_from_spec(spec);spec.loader.exec_module(m);return m
base=module('app_base','Known-AppBaseline.py');common=module('app_delta','Package-AppDelta.py')
sha,encode,read,git=common.sha,common.encode,common.read,common.git
def main():
 parser=argparse.ArgumentParser();parser.add_argument('--output',required=True);parser.add_argument('--source-commit',required=True);parser.add_argument('--preview',action='store_true');parser.add_argument('--audit',action='store_true');args=parser.parse_args()
 global EVIDENCE
 if args.audit:EVIDENCE=ROOT/'docs/implementation-evidence/app-adversarial-audit-20261003'
 source=git('rev-parse',args.source_commit+'^{commit}');output=Path(args.output).absolute()
 if output.exists():raise RuntimeError('Preserve previous delivery')
 common.subprocess.check_call(['git','-C',str(ROOT),'merge-base','--is-ancestor',source,'HEAD'])
 old,previous=base.readable_app_files() if args.audit else base.compact_app_files();base_sha=base.READABLE_SHA if args.audit else base.COMPACT_SHA;desired=dict(old);desired['NeuRotic.Hub.exe']=read(common.BIN/'NeuRotic.Hub.exe')
 changed=set(git('diff','--name-only',previous['sourceCommit'],'--','apps/NeuRoticHub','tests/hub','tools/hub').splitlines())
 changed.update(git('ls-files','--others','--exclude-standard','apps/NeuRoticHub','tests/hub','tools/hub').splitlines())
 for name in sorted(changed):
  path=ROOT/name
  if path.is_file() and path.suffix.lower() in ('.cpp','.h','.cs','.ps1','.cmd','.py','.vcxproj','.txt'):desired['Source/Hub/'+name]=read(path)
  elif not path.exists():desired.pop('Source/Hub/'+name,None)
 receipt=json.loads(old['ARCHIVE-MANIFEST.json']);receipt.update(hub_source_commit=source,hub_sha256=sha(desired['NeuRotic.Hub.exe']),app_update_base_sha256=base_sha,files=common.inventory({n:raw for n,raw in desired.items() if n!='ARCHIVE-MANIFEST.json'}))
 desired['ARCHIVE-MANIFEST.json']=encode(receipt)
 rows=[]
 for name in sorted(set(old)|set(desired)):
  if old.get(name)==desired.get(name):continue
  assert name=='NeuRotic.Hub.exe' or name=='ARCHIVE-MANIFEST.json' or name.startswith('Source/Hub/')
  raw=desired.get(name);rows.append({'path':name,'action':'put' if raw is not None else 'remove','sha256':sha(raw) if raw is not None else None,'previousSha256':sha(old[name]) if name in old else None,'bytes':len(raw) if raw is not None else 0})
 if not rows or desired['NeuRotic.Hub.exe']==old['NeuRotic.Hub.exe']:raise RuntimeError('No new App binary')
 # Guard the matched prior runtime, including persistent-file support and Small bundle.
 dependencies={r['path']:r['sha256'] for r in previous['dependencies']}
 for row in previous['files']:
  name=row['path']
  if row['action']=='put' and name not in {r['path'] for r in rows} and not name.startswith(('Source/','verification/')):dependencies[name]=sha(old[name])
 dependencies=[{'path':name,'sha256':digest} for name,digest in sorted(dependencies.items())]
 identity=sha(encode(rows))[:16];manifest={'kind':'neurotic-app-delta','schemaVersion':1,'id':identity,'sourceCommit':source,'baseArchiveSha256':base_sha,'files':rows,'dependencies':dependencies}
 if not args.preview:
  build=json.loads(read(EVIDENCE/'native-build.json'));assert build['exitCode']==0 and build['sourceCommit']==source and build['hubSha256']==sha(desired['NeuRotic.Hub.exe'])
  for row in build['inputs']:assert sha(read(ROOT/row['path']))==row['sha256'],row['path']
  for name in ('loading-ui.log','focused-ui.log','native-self-tests.log'):
   log=read(EVIDENCE/name);assert b'PASS ' in log and b'FAIL ' not in log,name
  verified=json.loads(read(EVIDENCE/'delta-compatibility.json'));assert verified['passed'] and verified['id']==identity and verified['manifestSha256']==sha(encode(manifest))
  assert not git('status','--porcelain'),'Final delivery requires a clean recorded source/evidence commit'
 files={'APP-UPDATE-MANIFEST.json':encode(manifest),'Apply-AppUpdate.ps1':read(ROOT/'tools/hub/Apply-AppUpdate.ps1'),'NeuRotic-AppUpdateSafety.cs':read(ROOT/'tools/hub/NeuRotic-AppUpdateSafety.cs')}
 for row in rows:
  if row['action']=='put':files['NeuRotic/'+row['path']]=desired[row['path']]
 for name,option in (('Apply NeuRotic App Update.cmd',''),('Rollback NeuRotic App Update.cmd',' -Rollback')):
  files[name]=(f'@echo off\r\nsetlocal\r\ntitle NeuRotic Application Update\r\nset "PSModulePath=%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\Modules"\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-AppUpdate.ps1"{option} %*\r\nset "result=%ERRORLEVEL%"\r\npause\r\nexit /b %result%\r\n').encode('ascii')
 files['READ FIRST.txt']=b'NEUROTIC READABILITY UPDATE\r\n\r\nRequires NeuRotic-App-4992bece-Update.zip already installed.\r\nClose NeuRotic. Extract separately and run Apply NeuRotic App Update.cmd. Select your installed NeuRotic.Hub.exe. The updater checks the exact previous App and its matching dependencies before writing.\r\nThis update adds tab dividers, a crosshair with a centered NR Anything label and reserved countdown space, and larger text with consistent Windows DPI scaling. No separate scaling control is needed.\r\nSettings, runtime files, game backups, renderer and Window Worker are preserved. Rollback NeuRotic App Update.cmd restores the previous App.\r\n'
 if args.audit:files['READ FIRST.txt']=b'NEUROTIC AUDIT REPAIR\r\n\r\nRequires NeuRotic-App-28f09848-Update.zip already installed.\r\nClose NeuRotic. Extract separately and run Apply NeuRotic App Update.cmd. Select your installed NeuRotic.Hub.exe.\r\nProtects library, artwork and NR preferences against linked temporary files and incomplete saves. Keeps the approved interface and matched rendering components.\r\nSettings, private runtime files and game folders are preserved. Rollback NeuRotic App Update.cmd restores the previous App.\r\n'
 output.parent.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for name,raw in sorted(files.items()):z.writestr(name,raw)
 with zipfile.ZipFile(output) as z:
  assert z.testzip() is None
  for name,raw in files.items():assert z.read(name)==raw
 print(json.dumps({'archive':str(output),'sha256':sha(read(output)),'bytes':output.stat().st_size,'changedFiles':len(rows),'members':len(files),'id':identity,'sourceCommit':source,'hubSha256':sha(desired['NeuRotic.Hub.exe']),'matchingBaselineDependencies':len(dependencies)},indent=2))
if __name__=='__main__':main()
