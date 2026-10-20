"""Package only the changed desktop binaries and corresponding source changes."""
from pathlib import Path
import argparse, hashlib, json, re, subprocess, zipfile

ROOT=Path(__file__).resolve().parents[2]
args=argparse.ArgumentParser();args.add_argument('--comparison',action='store_true');args.add_argument('--performance',action='store_true');args.add_argument('--throughput',action='store_true');args.add_argument('--resolution',action='store_true');args.add_argument('--scheduling',action='store_true');options=args.parse_args();comparison=options.comparison;scheduling=options.scheduling;resolution=options.resolution or scheduling;throughput=options.throughput or resolution;performance=options.performance or throughput
assert sum([options.comparison,options.performance,options.throughput,options.resolution,options.scheduling])<=1
EVIDENCE=ROOT/('docs/implementation-evidence/anything-scheduling-20261003' if scheduling else 'docs/implementation-evidence/anything-resolution-20261003' if resolution else 'docs/implementation-evidence/anything-throughput-20261003' if throughput else 'docs/implementation-evidence/anything-performance-20261003' if performance else 'docs/implementation-evidence/anything-comparison-20261003' if comparison else 'docs/implementation-evidence/anything-interaction-20261003')
BASE=Path('C:/NeuRotic/_exports/NeuRotic-Current-Flagship-4134edeb.zip')
BASE_SHA='2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
sha=lambda data:hashlib.sha256(data).hexdigest()
assert sha(BASE.read_bytes())==BASE_SHA
FILES={
 'NeuRotic.Hub.exe':'builds/hub/bin/NeuRotic.Hub.exe',
 'Tools/WindowWorker/NeuRotic.WindowWorker.exe':'builds/window-worker/bin/NeuRotic.WindowWorker.exe',
 'Tools/WindowWorker/nvngx.dll_dlssnr.dll':'builds/window-worker/bin/nvngx.dll_dlssnr.dll'}
SOURCES=['apps/NeuRoticHub/anything/AnythingController.cpp','apps/NeuRoticHub/anything/AnythingView.cpp','apps/NeuRoticWindowWorker/WorkerContracts.h','apps/NeuRoticWindowWorker/WorkerMain.cpp','apps/NeuRoticWindowWorker/capture/CaptureHost.cpp','tests/window-worker/CaptureTests.cpp','tests/hub/AnythingTests.cpp','tests/window-worker/InteractionUpdateTests.py','tools/hub/Build-AnythingTests.cmd','tools/window-worker/Apply-InteractionUpdate.ps1','tools/window-worker/Package-InteractionUpdate.py']
if performance:
 SOURCES += ['apps/NeuRoticWindowWorker/PerformanceMetrics.h','apps/NeuRoticWindowWorker/control/DiagnosticTrace.h','apps/NeuRoticWindowWorker/control/PerformanceRecord.h','apps/NeuRoticWindowWorker/control/FrameTimings.h','apps/NeuRoticWindowWorker/nr/GpuTimings.h','apps/NeuRoticWindowWorker/nr/NrHost.cpp','tests/window-worker/PerformanceTests.py','tests/window-worker/PerformanceSchemaTests.py','tests/window-worker/ControlTests.cpp','tests/window-worker/ControlLifecycleHosts.cpp','tests/window-worker/NrNativeTests.cpp','tools/window-worker/Trace-Anything.ps1','tools/window-worker/performance.schema.json']
if throughput:SOURCES += ['tools/window-worker/capture/Build-CaptureTests.cmd']
if resolution:SOURCES += ['apps/NeuRoticWindowWorker/control/ControlCore.cpp','apps/NeuRoticWindowWorker/nr/NrPolicy.h','apps/NeuRoticWindowWorker/nr/NrPolicy.cpp','OptiScaler/dlssnr/DlssNr_PresentResolution.h','tests/window-worker/NrHostTests.cpp','tests/window-worker/ResolutionProtocolTests.py','tests/hub/AnythingProtocolFixture.cpp']
if scheduling:SOURCES += ['apps/NeuRoticWindowWorker/control/WorkerWake.h','tests/window-worker/SchedulingTests.cpp','tests/window-worker/SchedulingProtocolTests.py','tools/window-worker/Build-SchedulingTests.cmd']
payload={name:(ROOT/path).read_bytes() for name,path in FILES.items()}
pin=re.search(r'"([0-9a-f]{64})"',(ROOT/'builds/window-worker/nr/generated/WindowNrForwarderIdentity.h').read_text()).group(1)
assert sha(payload['Tools/WindowWorker/nvngx.dll_dlssnr.dll'])==pin
with zipfile.ZipFile(BASE) as baseline:
 files=[{'path':name,'bytes':len(data),'sha256':sha(data),'baselineSha256':sha(baseline.read('NeuRotic/'+name))} for name,data in payload.items()]
if comparison or performance:
 prior=json.loads((ROOT/'docs/implementation-evidence/anything-interaction-20261003/update-receipt.json').read_text())
 assert prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256']
 with zipfile.ZipFile(prior['path']) as old:
  for entry in files:entry['acceptedInstalledSha256']=[entry['baselineSha256'],sha(old.read(entry['path']))]
if performance:
 prior=json.loads((ROOT/'docs/implementation-evidence/anything-comparison-20261003/update-receipt.json').read_text())
 assert prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256']
 with zipfile.ZipFile(prior['path']) as old:
  for entry in files:entry['acceptedInstalledSha256'].append(sha(old.read(entry['path'])))
if throughput:
 prior=json.loads((ROOT/'docs/implementation-evidence/anything-performance-20261003/update-receipt.json').read_text())
 assert prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256']
 with zipfile.ZipFile(prior['path']) as old:
  for entry in files:entry['acceptedInstalledSha256'].append(sha(old.read(entry['path'])))
if resolution:
 prior=json.loads((ROOT/'docs/implementation-evidence/anything-throughput-20261003/update-receipt.json').read_text())
 assert prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256']
 with zipfile.ZipFile(prior['path']) as old:
  for entry in files:entry['acceptedInstalledSha256'].append(sha(old.read(entry['path'])))
if scheduling:
 prior=json.loads((ROOT/'docs/implementation-evidence/anything-resolution-20261003/update-receipt.json').read_text())
 assert prior['sealed'] and sha(Path(prior['path']).read_bytes())==prior['sha256']
 with zipfile.ZipFile(prior['path']) as old:
  for entry in files:entry['acceptedInstalledSha256'].append(sha(old.read(entry['path'])))
manifest={'schemaVersion':1,'role':'Anything event-driven scheduling; existing flagship required' if scheduling else 'Anything NR resolution controls; existing flagship required' if resolution else 'Anything measured throughput repair; existing flagship required' if throughput else 'Anything performance telemetry update; existing flagship required' if performance else 'Anything interaction update; existing flagship required','baselineArchive':BASE.name,'baselineArchiveSha256':BASE_SHA,'baseCommit':'bbffd61a5df17955893fca4375a740c565229278','sourceParentCheckpoint':subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip(),'sourceInputRole':'Exact file hashes below identify compiled changes; parent checkpoint is not a claim that uncommitted changes are in that commit','files':files,'forwarderPin':pin,'sources':[{'path':path,'sha256':sha((ROOT/path).read_bytes())} for path in SOURCES],'qualifications':{'nativeInteractionFixture':True,'actualRTXProvider':False,'automaticAudioDelay':False}}
payload['UPDATE-MANIFEST.json']=(json.dumps(manifest,indent=2)+'\n').encode()
payload['Apply-AnythingUpdate.ps1']=(ROOT/'tools/window-worker/Apply-InteractionUpdate.ps1').read_bytes()
payload['Update-Anything.cmd']=b'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Apply-AnythingUpdate.ps1" -InstallRoot "%~1"\r\nif errorlevel 1 (echo Update was not applied. Read the error above. & pause & exit /b 1)\r\necho Update complete.\r\npause\r\n'
payload['READ ME.txt']=b'''Anything interaction update for NeuRotic-Current-Flagship-4134edeb.zip\r\n\r\nClose NeuRotic and its Window Worker. Extract this small update to a separate folder.\r\nRun Update-Anything.cmd and enter the folder containing your installed NeuRotic.Hub.exe.\r\nOr run: Update-Anything.cmd "C:\\Path\\To\\NeuRotic"\r\nThe updater verifies the baseline and matching worker/forwarder and preserves settings/models.\r\nFor manual copying, replace NeuRotic.Hub.exe and BOTH files in Tools/WindowWorker together.\r\n\r\nOverlay: native mouse/keyboard and live system cursor stay with the source application.\r\nAlt-Tab, source dialogs and menus yield; returning resumes fresh processed output.\r\nCtrl+Alt+F10 disengages, or use Stop in the App. A shortcut conflict is reported in the App.\r\nWindows can refuse the initial focus handoff; click the source once or use countdown selection.\r\nExclusive fullscreen/protected surfaces and software cursors drawn into source pixels remain limited.\r\nThe shortcut is serviced between synchronous backend operations; a blocked vendor call can delay it.\r\nActual game/VLC/RTX and mixed-monitor behavior still require runtime testing.\r\nNo media buffering or audio changes are included.\r\n\r\nUPDATE-MANIFEST.json records this update against the preserved original archive manifest.\r\nSource-Changes contains changed GPL source files to apply to the baseline source tree.\r\nUnchanged source/licenses/dependencies remain supplied by the installed flagship package.\r\n'''
if comparison:payload['READ ME.txt']+=b'\r\nComparison fix: NeuRotic App controls keep the processed source picture running behind them.\r\nOther applications and source menus/dialogs still yield. Depth/motion options are deferred.\r\nThis update accepts the original flagship OR the previous Anything interaction update.\r\n'
if performance:
 payload['Trace-Anything.ps1']=(ROOT/'tools/window-worker/Trace-Anything.ps1').read_bytes()
 payload['Trace-Anything.cmd']=b'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Trace-Anything.ps1" -InstallRoot "%~1"\r\npause\r\n'
 payload['READ ME.txt'] += b'\r\nPERFORMANCE RESEARCH - FIRST STAGE\r\nThis update measures the bottleneck; it does not claim to fix 8 FPS or the remaining comparison issue.\r\nIt accepts the original flagship, interaction update, OR latest comparison fix.\r\nAfter updating, close the App and run Trace-Anything.cmd against your installation folder.\r\nRun Anything for about 60 seconds, move the comparison slider, then Stop.\r\nReturn the created Anything-Trace folder plus Copy session status from the App.\r\nSeparate CPU/GPU timings, overlay yield and command lineage persist in diagnostics.\r\nNo resolution scaling, duplicate-frame reuse, depth/motion, audio or concurrency changes.\r\nDiagnostics collect metadata only, not pictures/audio/model bytes.\r\n'
if throughput:
 payload['READ ME.txt']=payload['READ ME.txt'].split(b'PERFORMANCE RESEARCH - FIRST STAGE')[0]+b'''MEASURED THROUGHPUT REPAIR\r\nGPU clears replace full-frame CPU zero uploads. Capture/presentation use exact fence events instead of polling sleeps.\r\nNo scaling, temporal policy, model, depth/motion or audio changes. RTX FPS gain is not yet measured.\r\nThe visible comparison issue remains open; the returned trace showed all frames continued processing.\r\nAccepts original flagship and every prior Anything update, including Performance Trace.\r\nAfter updating, run Trace-Anything.cmd, exercise the same source and slider for about 60 seconds, then Stop.\r\nReturn the generated trace ZIP plus session status. Diagnostics contain metadata only.\r\n'''
if resolution:
 payload['READ ME.txt']=payload['READ ME.txt'].split(b'MEASURED THROUGHPUT REPAIR')[0]+b'''NR RESOLUTION CONTROLS\r\nChoose Full (100%), Quality (75%), Balanced (67%) or Performance (50%) before Start.\r\nBalanced is the App default. Output stays at the captured window size; model dimensions follow resize.\r\nThis uses existing Present-style composition, not DLSS Super Resolution or FSR.\r\nIncludes previous performance fixes. Accepts original flagship and all prior Anything updates/full copy.\r\nActual RTX quality and FPS at reduced NR resolution need testing with your private model.\r\nPersistent background overlay, snapshots, pacing and multipass remain follow-up work.\r\nRun Trace-Anything.cmd for a measured session; return its diagnostics and session status.\r\n'''
if scheduling:
 payload['READ ME.txt']+=b'\r\nEVENT-DRIVEN SCHEDULING\r\nRendering wakes for captured frames, commands and window messages instead of sleeping after each frame.\r\nIncludes NR resolution presets and prior performance fixes. No FPS cap or audio changes.\r\nTrace now records actual scheduler wait time and completion intervals. Actual GPU FPS gain needs a new run.\r\nAccepts the previous resolution update and full copy as well as older Anything updates.\r\n'
for path in SOURCES:payload['Source-Changes/'+path]=(ROOT/path).read_bytes()
archive=Path('C:/NeuRotic/_exports')/('NeuRotic-Anything-Scheduling-Update-20261003.zip' if scheduling else 'NeuRotic-Anything-Resolution-Update-20261003.zip' if resolution else 'NeuRotic-Anything-Throughput-Update-20261003.zip' if throughput else 'NeuRotic-Anything-Performance-Trace-20261003.zip' if performance else 'NeuRotic-Anything-Comparison-Fix-20261003.zip' if comparison else 'NeuRotic-Anything-Interaction-Update-20261003.zip')
if archive.exists():
 previous=json.loads((EVIDENCE/'update-receipt.json').read_text())
 assert not previous.get('sealed',False) and Path(previous['path'])==archive and sha(archive.read_bytes())==previous['sha256'],'Preserve an existing delivered update'
with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as package:
 for name,data in sorted(payload.items()):package.writestr(name,data)
with zipfile.ZipFile(archive) as package:
 assert package.testzip() is None
 assert all(package.read(name)==data for name,data in payload.items())
receipt={'path':str(archive),'bytes':archive.stat().st_size,'sha256':sha(archive.read_bytes()),'manifest':manifest,'members':len(payload),'memberBytes':sum(map(len,payload.values())),'sealed':False}
(EVIDENCE/'update-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'path':str(archive),'bytes':receipt['bytes'],'sha256':receipt['sha256'],'members':len(payload)},indent=2))
