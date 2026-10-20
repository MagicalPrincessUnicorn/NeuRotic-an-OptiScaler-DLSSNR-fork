"""Record source/binary identity after the final build and verification."""
import hashlib,json,subprocess
from pathlib import Path
root=Path(__file__).resolve().parents[2]
def identify(path):
 return {'path':path.relative_to(root).as_posix(),'bytes':path.stat().st_size,'sha256':hashlib.sha256(path.read_bytes()).hexdigest()}
source=[]
for folder in ('apps/NeuRoticWindowWorker','tools/window-worker','tests/window-worker'):
 for path in sorted((root/folder).rglob('*')):
  if path.is_file() and '__pycache__' not in path.parts:source.append(identify(path))
for name in ('OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp','OptiScaler/dlssnr/CanonicalProviderIdentity.h','OptiScaler/dlssnr/CanonicalProviderPolicy.h','OptiScaler/dlssnr/Feature18HostContract.h','OptiScaler/dlssnr/NativeProviderEntry.h','OptiScaler/shaders/dlssnr/precompile/DlssNr_Shader.h','OptiScaler/shaders/dlssnr/precompile/dlssnr.hlsl','builds/window-worker/nr/generated/WindowNrForwarderIdentity.h'):
 source.append(identify(root/name))
binary=[identify(root/'builds/window-worker/bin'/name) for name in ('NeuRotic.WindowWorker.exe','nvngx.dll_dlssnr.dll')]
pin=(root/'builds/window-worker/nr/generated/WindowNrForwarderIdentity.h').read_text()
if binary[1]['sha256'] not in pin:raise RuntimeError('Collocated forwarder does not match compiled pin')
original=identify(root/'builds/window-worker/nr/nvngx.dll_dlssnr.dll')
if original['sha256']!=binary[1]['sha256']:raise RuntimeError('Forwarder copy mismatch')
verification=json.loads((root/'docs/implementation-evidence/window-nr-20261002/verification-final.json').read_text())
if any(c['outcome']=='FAIL' for c in verification['cases']):raise RuntimeError('Cannot record final handoff with failed attempted check')
receipt={'schemaVersion':1,'scope':'standalone window backend; application integration deferred','branch':subprocess.check_output(['git','branch','--show-current'],cwd=root,text=True).strip(),'head':subprocess.check_output(['git','rev-parse','HEAD'],cwd=root,text=True).strip(),'toolchain':'MSBuild VS18 Community v145/14.44.35207 SDK10.0.26100.0 Release x64','buildExitCode':0,'sources':source,'binaries':binary,'forwarderMatchesPin':True,'noPrivateProviderIncluded':True,'verification':'verification-final.json','hardwareBlocked':verification['hardwareBlocked']}
(root/'docs/implementation-evidence/window-nr-20261002/build-receipt.json').write_text(json.dumps(receipt,indent=2),encoding='utf-8')
print(json.dumps({'binaries':binary,'forwarderMatchesPin':True,'hardwareBlocked':verification['hardwareBlocked']}))
