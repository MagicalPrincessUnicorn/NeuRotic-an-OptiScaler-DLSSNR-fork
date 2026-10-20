"""Apply/rollback the exact delta in one owned, inert installed-baseline fixture."""
import argparse, hashlib, importlib.util, json, os, shutil, subprocess, zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'builds/delta'
EVIDENCE=ROOT/'docs/implementation-evidence/app-focused-ux-20261003'
BASE=ROOT.parent/'_exports/NeuRotic-Current-Flagship-4134edeb.zip'
PS=Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def ordinary(path):
 for p in (path,*path.absolute().parents):
  if p.is_symlink() or p.is_junction():raise RuntimeError('Linked fixture refused')
def main():
 parser=argparse.ArgumentParser();parser.add_argument('archive');parser.add_argument('--previous-update',action='store_true');parser.add_argument('--navigation-update',action='store_true');parser.add_argument('--compact-update',action='store_true');parser.add_argument('--readable-update',action='store_true');parser.add_argument('--evidence');args=parser.parse_args()
 global EVIDENCE
 if args.evidence:EVIDENCE=(ROOT/args.evidence).resolve();EVIDENCE.relative_to(ROOT/'docs/implementation-evidence')
 if WORK.exists():raise RuntimeError('Inspect retained App delta fixture before reuse')
 ordinary(WORK);WORK.mkdir(parents=True)
 logs=[]
 def check(label,condition):
  if not condition:raise RuntimeError(label)
  logs.append('PASS '+label)
 def run(command,env,timeout=180,stdin=None):
  # Match the App's private-pipe bytes exactly; Windows text mode rewrites LF.
  r=subprocess.run(command,cwd=WORK,env=env,input=stdin.encode('utf-8') if stdin is not None else None,capture_output=True,timeout=timeout)
  logs.append((r.stdout+r.stderr).decode('utf-8',errors='replace'))
  if r.returncode:
   for path in WORK.rglob('*.result.json'):
    try:
     result=json.loads(path.read_text(encoding='utf-8-sig'))
     if result.get('status') not in ('Succeeded','Inspected','Planned','PreflightChecked'):
      logs.append(json.dumps({k:result.get(k) for k in ('status','reason','diagnostic','decisionKind','target')}))
    except (ValueError,OSError):pass
   raise RuntimeError('Fixture command failed: '+str(command[0])+' '+str(r.returncode))
  return r
 try:
  patch=WORK/'patch';install=WORK/'installed';user=WORK/'user-data';game=WORK/'inert-game'
  for p in (patch,install,user,game):p.mkdir()
  with zipfile.ZipFile(args.archive) as z:
   check('delta CRC',z.testzip() is None)
   for n in z.namelist():
    assert ':' not in n and not n.startswith('/') and '..' not in n.split('/')
    target=patch/n;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(z.read(n))
  raw_manifest=(patch/'APP-UPDATE-MANIFEST.json').read_bytes();manifest=json.loads(raw_manifest)
  check('no private NR file exported',not any(Path(r['path']).name.lower()=='nvngx_dlssnr.dll' for r in manifest['files']))
  check('no renderer/game payload changed',not any(r['path'].startswith('packages/current/payload/') for r in manifest['files']))
  if args.previous_update or args.navigation_update or args.compact_update or args.readable_update:
   spec=importlib.util.spec_from_file_location('known_app',ROOT/'tools/hub/Known-AppBaseline.py');known=importlib.util.module_from_spec(spec);spec.loader.exec_module(known)
   baseline,_=known.readable_app_files() if args.readable_update else known.compact_app_files() if args.compact_update else known.navigation_app_files() if args.navigation_update else known.current_app_files();check('exact supported previous App update',(known.READABLE_SHA if args.readable_update else known.COMPACT_SHA if args.compact_update else known.NAVIGATION_SHA if args.navigation_update else known.PREVIOUS_SHA)==manifest['baseArchiveSha256'])
  else:
   with zipfile.ZipFile(BASE) as z:baseline={n.removeprefix('NeuRotic/'):z.read(n) for n in z.namelist()}
   check('exact supported installed archive',sha(BASE.read_bytes())==manifest['baseArchiveSha256'])
  needed={r['path'] for r in manifest['files'] if r['previousSha256']};needed.update(r['path'] for r in manifest['dependencies'])
  for rel,raw in baseline.items():
   if rel in needed or rel.startswith(('support/','packages/current/','Tools/WindowWorker/','discovery/')) or rel=='NeuRotic.Hub.exe':
    target=install/rel;target.parent.mkdir(parents=True,exist_ok=True);target.write_bytes(raw)
  # Settings, legacy/private and AppData data are deliberately outside the delta.
  preserved={install/'models/nvngx_dlssnr.dll':b'user private legacy NR sentinel',install/'streamline/sl.interposer.dll':b'user legacy Streamline sentinel',user/'library.json':b'{"schemaVersion":3,"games":[],"searchRoots":[]}',user/'Runtime/Models/private/nvngx_dlssnr.dll':b'user persistent NR sentinel',user/'Runtime/streamline/sl.interposer.dll':b'user persistent Streamline sentinel'}
  for path,raw in preserved.items():path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
  env={**os.environ,'PSModulePath':str(PS.parent/'Modules'),'NEUROTIC_HUB_FIXTURE_ROOT':str(user)}
  updater=[str(PS),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(patch/'Apply-AppUpdate.ps1'),'-InstallDirectory',str(install)]
  run(updater,env)
  for row in manifest['files']:
   p=install/row['path'];check('applied '+row['path'],sha(p.read_bytes())==row['sha256'] if row['action']=='put' else not p.exists())
  for row in manifest['dependencies']:assert sha((install/row['path']).read_bytes())==row['sha256']
  check('all published dependency pairs unchanged',True)
  check('update preserves private files and library settings',all(p.read_bytes()==raw for p,raw in preserved.items()))
  run([str(install/'NeuRotic.Hub.exe'),'--self-test'],env)
  check('updated extracted native self-tests',True)
  exe=game/'FixtureGame.exe';shutil.copyfile(Path(os.environ['SystemRoot'])/'System32/cmd.exe',exe)
  private=game/'nvngx_dlssnr.dll';private.write_bytes(b'private game NR sentinel')
  ini=game/'OptiScaler.ini';ini.write_text('; preserve custom configuration\n[Menu]\nOverlayMenu=false\nCustomUserKey=retained\n')
  ini_raw=ini.read_bytes()
  def call(request,approval=None):
   request.update(protocolVersion=1);request_path=WORK/'request.json';result_path=WORK/'result.json'
   request_path.write_text(json.dumps(request),encoding='utf-8');result_path.unlink(missing_ok=True)
   run([str(PS),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(install/'support/NeuRotic-Setup-Engine.ps1'),'-HubNonInteractive','-HubRequestPath',str(request_path),'-HubResultPath',str(result_path)],env,stdin=approval)
   result=json.loads(result_path.read_text(encoding='utf-8-sig'))
   logs.append(json.dumps({k:result.get(k) for k in ('status','reason','diagnostic','decisionKind')}))
   return result
  preflight=call({'kind':'Preflight'})
  check('packaged preflight uses persistent Runtime',Path(preflight['components']['streamline']['path'])==user/'Runtime/streamline')
  check('preflight never replaces existing persistent user files',all(p.read_bytes()==raw for p,raw in preserved.items()))
  for operation in ('Install','Update','Uninstall'):
   plan=call({'kind':'Plan','gameExecutable':str(exe),'operation':operation,'packageId':'flagship-approved','proxyName':'winmm.dll','continueWithoutRuntime':True})
   check('packaged '+operation+' plan',plan['status']=='Planned')
   ac=plan.get('antiCheat');approval=None
   if ac and ac.get('acknowledgementRequired'):
    check('packaged anti-cheat rules valid',ac['ruleset_version']=='2026-10-03.1' and 'rules_invalid' not in ac['issues'])
    approval='NRAC1\n'+plan['planId']+'\n'+plan['planFingerprint']+'\n'+ac['fingerprint']+'\n'
   result=call({'kind':'Execute','planId':plan['planId'],'planFingerprint':plan['planFingerprint']},approval)
   check('packaged '+operation+' execution',result['status']=='Succeeded')
   check(operation+' preserves custom ini and private model',ini.read_bytes()==ini_raw and private.read_bytes()==b'private game NR sentinel')
  check('uninstall removes owned loader',(game/'winmm.dll').exists()==False)
  run(updater+['-Rollback'],env)
  for row in manifest['files']:
   p=install/row['path'];check('restored '+row['path'],sha(p.read_bytes())==row['previousSha256'] if row['previousSha256'] else not p.exists())
  check('rollback preserves private files and library settings',all(p.read_bytes()==raw for p,raw in preserved.items()))
  check('packaged App delta',True)
  EVIDENCE.mkdir(parents=True,exist_ok=True)
  (EVIDENCE/'delta-compatibility.json').write_text(json.dumps({'id':manifest['id'],'manifestSha256':sha(raw_manifest),'archiveSha256':sha(Path(args.archive).read_bytes()),'sourceCommit':manifest['sourceCommit'],'changedFiles':len(manifest['files']),'matchingDependencies':len(manifest['dependencies']),'passed':True},indent=2))
 finally:
  EVIDENCE.mkdir(parents=True,exist_ok=True);(EVIDENCE/'delta-compatibility.log').write_text('\n'.join(logs),encoding='utf-8')
  ordinary(WORK)
  for p in WORK.rglob('*'):ordinary(p)
  shutil.rmtree(WORK)
 print('PASS packaged App delta')
if __name__=='__main__':main()
