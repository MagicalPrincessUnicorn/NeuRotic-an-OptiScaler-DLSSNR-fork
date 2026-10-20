"""Compact local integration handoff; explicit source closure, no provider/SDK binaries."""
import hashlib,json,shutil,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[2]
destination=root.parent/'_exports/NeuRotic-WindowWorker-20261002'
archive=destination.with_suffix('.zip')
files={}
def add(source,target=None):
 source=root/source
 if not source.is_file():raise RuntimeError('Missing package input: '+str(source))
 if source.is_symlink():raise RuntimeError('Package input must be ordinary: '+str(source))
 relative=Path(target) if target else source.relative_to(root)
 files[relative.as_posix()]=source
def tree(folder):
 for source in sorted((root/folder).rglob('*')):
  if source.is_file():add(source.relative_to(root))
for folder in ('apps/NeuRoticWindowWorker','tools/window-worker','tests/window-worker','references/window-nr','docs/implementation-evidence/window-nr-20261002'):
 tree(folder)
for source in ('LICENSE','Licenses/RenoDX_ATTRIBUTION.txt','external/nlohmann/json.hpp',
 'OptiScaler/dlssnr/CanonicalProviderPolicy.h','OptiScaler/dlssnr/CanonicalProviderIdentity.h',
 'OptiScaler/dlssnr/Feature18HostContract.h','OptiScaler/dlssnr/NativeProviderEntry.h',
 'OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp','OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h','OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl',
 'docs/superpowers/specs/2026-10-02-window-nr-worker.md','docs/superpowers/plans/2026-10-02-window-nr-worker.md'):
 add(source)
for source in sorted((root/'OptiScaler/shaders/dlssnr').glob('*.hlsl')):add(source.relative_to(root))
for source in ('NeuRotic.WindowWorker.exe','nvngx.dll_dlssnr.dll'):
 add('builds/window-worker/bin/'+source,'bin/'+source)
for source in sorted((root/'builds/window-worker/bin').glob('*.pdb')):add(source.relative_to(root),'bin/'+source.name)
for name,source in files.items():
 if source.name.lower() in ('nvngx_dlssnr.dll','model.onnx','directml.dll','onnxruntime.dll','onnxruntime_providers_shared.dll'):
  raise RuntimeError('Provider binary excluded: '+name)
 if any(part in ('obj','forwarder-obj','__pycache__') for part in Path(name).parts):raise RuntimeError('Intermediate excluded: '+name)
 if source.suffix.lower() in ('.obj','.pch','.ilk','.pyc','.lib'):raise RuntimeError('Intermediate excluded: '+name)
destination.mkdir(parents=True,exist_ok=True)
manifest=[]
for name,source in sorted(files.items()):
 target=destination/name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(source,target)
 manifest.append({'path':name,'bytes':target.stat().st_size,'sha256':hashlib.sha256(target.read_bytes()).hexdigest()})
(destination/'README.md').write_text('''# NeuRotic standalone window worker — local integration handoff

Launch `bin/NeuRotic.WindowWorker.exe --control`. Keep the collocated forwarder.
Start with apps/NeuRoticWindowWorker/INTEGRATION.md and the verification report in docs/implementation-evidence/window-nr-20261002/IMPLEMENTATION.md.

The source is an overlay for the existing flagship checkout. It preserves backend source, current shader/forwarder source, tests, notices and build scripts. Existing separately licensed NVIDIA SDK headers are required to rebuild and are excluded. No user NR file, depth model or provider runtime is bundled. This is a local handoff; full RTX NR execution and image quality still require hardware qualification. No Hub UI changes are included.
''',encoding='utf-8')
(destination/'MANIFEST.json').write_text(json.dumps({'schemaVersion':1,'role':'LOCAL_INTEGRATION_HANDOFF','files':manifest},indent=2),encoding='utf-8')
expected=set(files)|{'README.md','MANIFEST.json'}
actual={p.relative_to(destination).as_posix() for p in destination.rglob('*') if p.is_file()}
if actual!=expected:raise RuntimeError('Unexpected stale package members: '+str(actual-expected))
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
 for name in sorted(expected):z.write(destination/name,name)
with zipfile.ZipFile(archive) as z:
 if set(z.namelist())!=expected or z.testzip():raise RuntimeError('Archive verification failed')
 for item in manifest:
  if hashlib.sha256(z.read(item['path'])).hexdigest()!=item['sha256']:raise RuntimeError('Archive hash mismatch')
receipt={'archive':str(archive),'bytes':archive.stat().st_size,'sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'members':len(expected),'providerBinariesIncluded':False,'allMemberHashesVerified':True}
archive.with_suffix('.receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps(receipt))
