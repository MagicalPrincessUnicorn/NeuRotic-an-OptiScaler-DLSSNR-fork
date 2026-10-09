"""Seal universal connections into the verified existing full customer package.

No build/download/game install. Preview permits a dirty source tree; final sealing
requires a clean exact source commit and hash-bound successful check logs.
"""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
import sys
from pathlib import Path
sys.path.insert(0,str(Path(__file__).parent))
from NativePackagePolicy import retain_native_shipping
from InspectorLayout import version_inspector, component as inspector_component
ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT.parent/'_exports/NeuRotic-Current-Flagship-4134edeb.zip'
BASE_SHA='2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
def sha(raw):return hashlib.sha256(raw).hexdigest()
def encode(obj):return (json.dumps(obj,indent=2)+'\n').encode()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()
def rows(files):return [dict(path=n.replace('/','\\'),bytes=len(b),sha256=sha(b)) for n,b in sorted(files.items())]
def safe(name):
 name=name.replace('\\','/')
 if name.startswith('/') or ':' in name or any(p in ('','..','.') for p in name.split('/')):raise ValueError('Unsafe archive path: '+name)
 return name
def read_base(path,expected):
 assert sha(path.read_bytes())==expected,'Base package hash mismatch'
 with zipfile.ZipFile(path) as z:
  assert z.testzip() is None
  names=z.namelist();assert len(names)==len({n.casefold()for n in names})
  manifest=json.loads(z.read('NeuRotic/ARCHIVE-MANIFEST.json'));files={}
  for row in manifest['files']:
   name=safe(row['path']);raw=z.read('NeuRotic/'+name)
   assert len(raw)==row['bytes'] and sha(raw)==row['sha256'].lower(),name
   files[name]=raw
  assert set(names)=={'NeuRotic/'+n for n in files}|{'NeuRotic/ARCHIVE-MANIFEST.json'}
 return files,manifest
def verify_receipt(path,commit):
 receipt=json.loads(path.read_text(encoding='utf-8-sig'));assert receipt['sourceCommit']==commit
 assert receipt['checks'] and receipt['artifacts'] and receipt['sources']
 for check in receipt['checks']:
  assert check['exitCode']==0,check['name']
  assert sha((ROOT/safe(check['log'])).read_bytes())==check['sha256'].lower(),check['name']
 for entry in receipt['artifacts']+receipt['sources']:
  assert sha((ROOT/safe(entry['path'])).read_bytes())==entry['sha256'].lower(),entry['path']
 return receipt
def command(script, action=''):
 return ('@echo off\r\nsetlocal\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\\'+script+'" '+action+' %*\r\nexit /b %ERRORLEVEL%\r\n').encode()

def add_prepared_workflow(files,prefix,pix,reshade_setup=None,include_test_workflow=True):
 ref=ROOT/'references/prepared-guide-installer'
 pins=json.loads((ref/'SOURCES.json').read_text(encoding='utf-8'))
 runtime=reshade_setup or ref/'ReShade_Setup_6.8.0_Addon.exe'
 for row in pins['files']:
  raw=(runtime if row['path']=='ReShade_Setup_6.8.0_Addon.exe' else ref/safe(row['path'])).read_bytes()
  assert len(raw)==row['bytes'] and sha(raw)==row['sha256'],row['path']
 with zipfile.ZipFile(runtime) as z:files[prefix+'payload/ReShade64.dll']=z.read('ReShade64.dll')
 files['Optional/ReShade/ReShade_Setup_6.8.0_Addon.exe']=runtime.read_bytes()
 files['Optional/ReShade/LICENSE.md']=(ref/'ReShade-LICENSE.md').read_bytes()
 files[prefix+'payload/Licenses/Prepared-ReShade-BSD.txt']=(ref/'ReShade-LICENSE.md').read_bytes()
 files[prefix+'payload/WinPixEventRuntime.dll']=pix
 asset=prefix+'payload/OptiScaler/PreparedGuides/'
 for name in ('shaders/NeuRotic_PreparedGuides.fx','shaders/NeuRotic_SourceGuidedColor.fx','LICENSE-DLSS5-FEEDER.txt','README.md'):
  files[asset+name.replace('shaders/','Shaders/')]=(ROOT/'addons/prepared-guides'/name).read_bytes()
 dependencies=[]
 for folder in ('drme','reshade-shaders'):
  for path in sorted((ref/folder).iterdir()):
   target=asset+('Shaders/'+path.name if path.suffix in ('.fx','.fxh') else folder+'/'+path.name)
   raw=path.read_bytes();files[target]=raw;dependencies.append(dict(path=target,sha256=sha(raw)))
 files[asset+'Prepared.ini']=b'Techniques=DRME@MotionEstimation.fx,NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\nTechniqueSorting=DRME@MotionEstimation.fx,NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\n'
 files[asset+'BuiltIn.ini']=b'Techniques=NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\n'
 files[asset+'Baseline.ini']=b'Techniques=\r\n'
 files[asset+'ATTRIBUTION.txt']=b'Personal noncommercial test package. Unmodified DRME by Jakob Wapenhensch, commit 5fc3f434ba158bfc380a71b60aa5a2bddf2242d6, CC BY-NC 4.0. https://github.com/JakobPCoder/ReshadeMotionEstimation https://creativecommons.org/licenses/by-nc/4.0/\nReShade 6.8.0 addon runtime by Patrick Mours, BSD-3-Clause. https://reshade.me/\nStandard shader headers commit fd0022170615ce0d8162d219bff07232fa6dd84f, original notices retained. Third-party components are not relicensed.\n'
 customer=ROOT/'tools/packaging/customer'
 if include_test_workflow:
  for name in ('PreparedProfile.ps1','PreparedSetup.ps1','Start-PreparedTest.ps1'):
   files['support/'+name]=(customer/'prepared-guides'/name).read_bytes()
  files[prefix+'support/Candidate-Launch.ps1']=(customer/'support/Candidate-Launch.ps1').read_bytes()
  for name,action in (('NeuRotic-Setup.cmd','Setup'),('NeuRotic-Uninstall.cmd','Uninstall'),('Restore-Test-Settings.cmd','Restore'),('ReShade-Setup.cmd','ReShade')):
   files[name]=command('PreparedSetup.ps1','-Action '+action)
  for name,mode in (('Start-Prepared-Test.cmd','Prepared'),('Start-Baseline-Test.cmd','Baseline'),('Start-D3D12-Flow-Test.cmd','BuiltIn'),('Start-Vulkan-Prepared-Test.cmd','VulkanPrepared'),('Start-Vulkan-Baseline-Test.cmd','VulkanBaseline')):
   files[name]=command('Start-PreparedTest.ps1',mode)
 files['support/PREPARED-SOURCES.json']=encode(pins)
 files['support/PREPARED-DEPENDENCIES.json']=encode(dict(personal_noncommercial=True,files=dependencies))
 shaders={n.rsplit('/',1)[-1]:b for n,b in files.items() if n.startswith(asset+'Shaders/')}
 for name,raw in shaders.items():
  for include in re.findall(rb'^\s*#include\s+"([^"]+)"',raw,re.M):assert include.decode() in shaders,(name,include)

def add_x86_package(files,core,addon,helper,pix,commit,product_commit,setup,include_launchers=True):
 """One managed PE32 capture package; the renderer remains PE32+ in its host."""
 prefix='packages/prepared-x86/';current='packages/current/'
 for name,raw in list(files.items()):
  if name.startswith(current+'support/') and name.rsplit('/',1)[-1] not in ('BUILD-MANIFEST.json','PACKAGE-MANIFEST.json'):
   files[prefix+name[len(current):]]=raw
 for names in (('NeuRotic-Manual-Setup.cmd','NeuRotic-Setup.cmd'),('NeuRotic-Manual-Uninstall.cmd','NeuRotic-Uninstall.cmd')):
  if not include_launchers:continue
  name=next(name for name in names if current+name in files)
  files[prefix+name]=files[current+name]
 with zipfile.ZipFile(setup) as archive:files[prefix+'payload/ReShade32.dll']=archive.read('ReShade32.dll')
 prepared=prefix+'payload/NeuRotic/Prepared/'
 files[prepared+'NeuRotic-PreparedGuides.addon32']=addon
 files[prepared+'Start-PreparedGame.ps1']=(ROOT/'tools/packaging/customer/prepared-guides/Start-x86-Prepared.ps1').read_bytes()
 source=current+'payload/OptiScaler/PreparedGuides/'
 for name,raw in list(files.items()):
  if name.startswith(source):files[prepared+name[len(source):]]=raw
 files[prepared+'Licenses/ReShade-BSD.txt']=(ROOT/'references/prepared-guide-installer/ReShade-LICENSE.md').read_bytes()
 host={'NeuRotic.PreparedGpuHost.exe':helper,'OptiScaler.dll':core,'WinPixEventRuntime.dll':pix,
       'nvngx.dll_dlssnr.dll':files[current+'payload/nvngx.dll_dlssnr.dll'],
       'OptiScaler.ini':b'[DlssNr]\r\nEnabled=true\r\nApplyModel=true\r\nRoute=2\r\nPresentInputPolicy=1\r\nInputSource=3\r\nInputTransport=1\r\nAllowCpuFallback=false\r\n[Log]\r\nLogToFile=true\r\n'}
 for name,raw in host.items():files[prepared+'NeuRotic.GpuHost/'+name]=raw
 files[prefix+'payload/ReShade.ini']=b'[GENERAL]\r\nEffectSearchPaths=.\\NeuRotic\\Prepared\\Shaders\r\nPresetPath=.\\NeuRotic\\Prepared\\Prepared.ini\r\nPreprocessorDefinitions=NRPG_MV_PROVIDER=0\r\nPerformanceMode=0\r\n[ADDON]\r\nAddonPath=.\\NeuRotic\\Prepared\r\n[NeuRoticPreparedGuides]\r\nFlowBackend=software\r\nProbeContent=1\r\nSharedGpu=0\r\nTransportOnly=0\r\n'
 files[prepared+'READ-ME.txt']=b'32-bit capture with 64-bit NeuRotic renderer. Launch through Hub Play to activate capture in the game process. Supply your private 64-bit nvngx_dlssnr.dll in NeuRotic/Prepared/NeuRotic.GpuHost; it is not bundled. Direct D3D9 capture is currently unavailable. Use NR Anything when no compatible in-game integration is available. Restart after configuration changes.\r\n'
 prior=json.loads(files[current+'support/BUILD-MANIFEST.json'])
 build=dict(prior,commit=commit,packaging_source_commit=commit,product_source_commit=product_commit,
            install_route='prepared-x86',target_bit_size=32,bit_size=64,proxy_source='ReShade32.dll',
            candidate='Universal Connections 32-bit prepared capture',product_sha256=sha(core),
            game_qualified=False,game_runtime_verified=False,provider_model_bundled=False)
 files[prefix+'support/BUILD-MANIFEST.json']=encode(build)
 contents={name[len(prefix):]:raw for name,raw in files.items() if name.startswith(prefix)}
 files[prefix+'support/PACKAGE-MANIFEST.json']=encode(dict(kind='neurotic-customer-candidate',
     name='NeuRotic 32-bit capture / 64-bit renderer experimental',commit=commit,
     product_source_commit=product_commit,public_release=False,game_qualified=False,files=rows(contents)))

def refresh_hub_packages(files):
 files['support/Hub-Packages.json']=encode(dict(schemaVersion=1,packages=[
  dict(id=identifier,name=label,relativeRoot='../packages/'+folder,
       manifestSha256=sha(files['packages/'+folder+'/support/PACKAGE-MANIFEST.json']))
  for identifier,folder,label in (
   ('flagship-approved','current','NeuRotic Flagship'),)]))

def main():
 p=argparse.ArgumentParser(description=__doc__)
 p.add_argument('--output',type=Path,required=True);p.add_argument('--verification',type=Path,required=True)
 p.add_argument('--reshade-setup',type=Path,default=ROOT/'references/prepared-guide-installer/ReShade_Setup_6.8.0_Addon.exe',help='User-preserved runtime; must match SOURCES.json hash pin')
 p.add_argument('--base',type=Path,default=BASE);p.add_argument('--base-sha256',default=BASE_SHA)
 p.add_argument('--core',type=Path,default=ROOT/'builds/universal-guides/bin/OptiScaler.dll')
 p.add_argument('--addon64',type=Path,default=ROOT/'builds/prepared-guides/NeuRotic-PreparedGuides.addon64')
 p.add_argument('--addon32',type=Path,default=ROOT/'builds/prepared-guides/x86/NeuRotic-PreparedGuides.addon32')
 p.add_argument('--helper',type=Path,default=ROOT/'builds/universal-connections/helper/production/NeuRotic.PreparedGpuHost.exe')
 p.add_argument('--pix',type=Path,default=ROOT/'builds/universal-connections/helper/production/WinPixEventRuntime.dll')
 p.add_argument('--hub',type=Path,required=True,help='Fresh App binary containing the window preference mapping')
 p.add_argument('--external64',type=Path,default=ROOT/'builds/universal-connections/external-feeder/x64/NeuRotic-ExternalFeeder.addon64')
 p.add_argument('--external32',type=Path,default=ROOT/'builds/universal-connections/external-feeder/x86/NeuRotic-ExternalFeeder.addon32')
 p.add_argument('--discovery',type=Path,help='Fresh discovery publish folder; every replacement must be hash-bound in the verification receipt')
 p.add_argument('--preview',action='store_true');p.add_argument('--dry-run',action='store_true');args=p.parse_args()
 assert git('branch','--show-current')=='codex/universal-connections-experimental'
 dirty=bool(git('status','--porcelain'));assert args.preview or not dirty,'Final seal requires clean source'
 commit=git('rev-parse','HEAD');receipt=verify_receipt(args.verification,commit)
 product_commit=receipt.get('productSourceCommit',commit)
 if product_commit!=commit:
  provenance=receipt['reusedArtifactProvenance'];prior_path=ROOT/safe(provenance['verificationReceipt'])
  assert sha(prior_path.read_bytes())==provenance['verificationSha256']
  previous=json.loads(prior_path.read_text(encoding='utf-8-sig'));assert previous['sourceCommit']==product_commit
  core_relative=args.core.resolve().relative_to(ROOT.resolve()).as_posix()
  previous_core=next(row for row in previous['artifacts'] if row['path']==core_relative)
  assert previous_core['sha256']==sha(args.core.read_bytes()),'Reused core differs from prior verified build'
 files,base_manifest=read_base(args.base,args.base_sha256);prefix='packages/current/'
 bound={safe(x['path']):x['sha256'].lower() for x in receipt['artifacts']}
 def artifact(path):
  path=path.resolve();rel=path.relative_to(ROOT.resolve()).as_posix();raw=path.read_bytes();assert bound.get(rel)==sha(raw),'Artifact absent from final verification: '+rel;return raw
 core=artifact(args.core);pix=artifact(args.pix)
 files['NeuRotic.Hub.exe']=artifact(args.hub)
 if args.discovery:
  for path in sorted(args.discovery.iterdir()):
   if path.is_file() and path.suffix.lower()!='.pdb':files['discovery/'+path.name]=artifact(path)
 files[prefix+'payload/OptiScaler.dll']=core
 files[prefix+'payload/WinPixEventRuntime.dll']=pix
 support=ROOT/'tools/packaging/customer/support'
 for source in sorted(support.iterdir()):
  if source.is_file():
   files['support/'+source.name]=source.read_bytes()
   files[prefix+'support/'+source.name]=source.read_bytes()
 files['README.md']=(ROOT/'apps/NeuRoticHub/README.md').read_bytes()
 files['support/UNIVERSAL-CONNECTIONS-VERIFICATION.json']=encode(receipt)
 retain_native_shipping(files)
 version_inspector(files)
 readme=('NeuRotic native connections\r\n\r\nOpen NeuRotic.Hub.exe for Install, Uninstall and NR Anything. Native Vulkan retains captured depth and estimated motion. Game and display qualification remains specific to the tested build.\r\n\r\n32-bit in-game installation is unavailable. Use NR Anything until a compatible native integration is available. ReShade is not included or required. Existing independent ReShade may be kept through explicit coexistence.\r\n').encode()
 files['UNIVERSAL-CONNECTIONS-READ-ME.txt']=readme
 prior=json.loads(files[prefix+'support/BUILD-MANIFEST.json'])
 build=dict(prior,commit=commit,packaging_source_commit=commit,product_source_commit=product_commit,branch='codex/universal-connections-experimental',version='universal-connections-experimental',candidate='Universal Connections experimental',product_sha256=sha(core),game_qualified=False,game_runtime_verified=False,prior_acceptance_transfers_to_this_binary=False,provider_model_bundled=False,experimental_verification=receipt,source_tree_dirty=dirty)
 build['reused_base_manifest']=prior;files[prefix+'support/BUILD-MANIFEST.json']=encode(build)
 files.pop(prefix+'support/PACKAGE-MANIFEST.json',None)
 package={n[len(prefix):]:b for n,b in files.items() if n.startswith(prefix)}
 files[prefix+'support/PACKAGE-MANIFEST.json']=encode(dict(kind='neurotic-customer-candidate',name='Universal Connections Experimental',commit=commit,product_source_commit=product_commit,public_release=False,game_qualified=False,components={'characterInspector':inspector_component()},files=rows(package)))
 refresh_hub_packages(files)
 files.pop('ARCHIVE-MANIFEST.json',None)
 manifest=dict(personal_noncommercial=True,source_commit=commit,product_source_commit=product_commit,preview=args.preview,source_tree_dirty=dirty,game_qualified=False,reused_dependency_delivery_sha256=args.base_sha256,reused_component_provenance=base_manifest.get('component_provenance',{}),files=rows(files))
 files['ARCHIVE-MANIFEST.json']=encode(manifest)
 assert len(files)==len({n.casefold() for n in files})
 if args.dry_run:print('PASS package inputs and manifests validated; no output written');return
 assert not args.output.exists(),'Preserve delivered archives; choose a new output path'
 args.output.parent.mkdir(parents=True,exist_ok=True)
 with zipfile.ZipFile(args.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
  for n,raw in sorted(files.items()):z.writestr('NeuRotic/'+safe(n),raw)
 with zipfile.ZipFile(args.output) as z:
  assert z.testzip() is None
  for n,raw in files.items():assert z.read('NeuRotic/'+n)==raw,n
 print(json.dumps(dict(path=str(args.output),bytes=args.output.stat().st_size,sha256=sha(args.output.read_bytes()),members=len(files),preview=args.preview)))
if __name__=='__main__':main()
