"""Package an exact local development result without copying build trees."""
import hashlib,json,subprocess,zipfile
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
EVIDENCE=ROOT/'docs/implementation-evidence/nr-advanced-20261008'
def git(*args):return subprocess.check_output(['git',*args],cwd=ROOT,text=True).strip()
def sha(path):
    value=hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda:stream.read(1024*1024),b''):value.update(chunk)
    return value.hexdigest()
def write(name,value):
    path=EVIDENCE/name;path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8');return path
source=git('rev-parse','HEAD')
paths=git('ls-files','--','OptiScaler','addons/prepared-guides','apps/NeuRoticHub/anything','localization','tests/universal_connections').splitlines()
sources=[{'path':p,'sha256':sha(ROOT/p)} for p in paths if (ROOT/p).is_file()]
source_manifest=write('source-files.json',{'sourceCommit':source,'scope':'Tracked runtime, addon, shared controller, localization and connection fixtures; external dependencies recorded separately.','files':sources})
files={
 'runtime/OptiScaler.dll':'builds/universal-input/runtime/bin/OptiScaler.dll',
 'addon/NeuRotic-PreparedGuides.addon64':'builds/prepared-guides/NeuRotic-PreparedGuides.addon64',
 'addon/NeuRotic-PreparedGuides.map':'builds/prepared-guides/NeuRotic-PreparedGuides.map',
 'addon/WinPixEventRuntime.dll':'builds/prepared-guides/WinPixEventRuntime.dll',
 'tests/adapter_capture_tests.exe':'builds/universal-guides/adapter-tests/adapter_capture_tests.exe',
 'tests/WinPixEventRuntime.dll':'builds/universal-guides/adapter-tests/WinPixEventRuntime.dll',
 'tests/mixed_input_runtime.exe':'builds/universal-input/runtime-tests/mixed_input_runtime.exe',
 'tests/mixed_input_selection.exe':'builds/universal-input/policy/mixed_input_selection.exe',
 'tests/nr-advanced-ui.exe':'builds/universal-input/advanced-ui/nr-advanced-ui.exe',
 'tests/nr-pipeline-view.exe':'builds/universal-input/pipeline-tests/nr_pipeline_view.exe',
 'tests/fg-route.exe':'builds/universal-input/pipeline-tests/fg-route.exe',
}
artifacts=[]
for archive_name,path in files.items():
    file=ROOT/path
    if not file.is_file():raise FileNotFoundError(file)
    artifacts.append({'path':path,'archivePath':archive_name,'sha256':sha(file)})
dependency_root=Path('C:/NeuRotic/Optiscaler-Stuff/external')
dependencies=[]
for name in ('FidelityFX-SDK','FidelityFX-SDK-v2','magic_enum','nvapi','simpleini','spdlog','unordered_dense','vulkan','xess'):
    path=dependency_root/name
    result=subprocess.run(['git','-c','safe.directory='+str(path),'-C',str(path),'rev-parse','HEAD'],capture_output=True,text=True)
    dependencies.append({'path':str(path),'head':result.stdout.strip() if result.returncode==0 else 'unavailable; installed dependency retained'})
artifact_manifest=write('artifacts.json',{'sourceCommit':source,'sourceManifestSha256':sha(source_manifest),'kind':'local development artifacts, not installer or game qualification','artifacts':artifacts,'externalDependencies':dependencies,'coreSymbols':'Release configuration emitted no core PDB; addon linker map retained.'})
archive=ROOT/f'builds/universal-input/NeuRotic-NRAdvanced-{source[:8]}-Development.zip'
with zipfile.ZipFile(archive,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as package:
    for item in artifacts:package.write(ROOT/item['path'],item['archivePath'])
    for file in (source_manifest,artifact_manifest,EVIDENCE/'REPORT.md'):
        package.write(file,'evidence/'+file.name)
    for file in sorted(EVIDENCE.glob('*.log')):package.write(file,'evidence/'+file.name)
    for folder in ('ui','pages'):
        for file in sorted((EVIDENCE/folder).glob('*')):
            if file.is_file():package.write(file,'evidence/'+folder+'/'+file.name)
with zipfile.ZipFile(archive) as package:
    assert package.testzip() is None
    for item in artifacts:assert hashlib.sha256(package.read(item['archivePath'])).hexdigest()==item['sha256']
write('delivery.json',{'sourceCommit':source,'branch':git('branch','--show-current'),'archive':str(archive),'archiveSha256':sha(archive),'archiveVerified':True,'artifactManifestSha256':sha(artifact_manifest),'sourceManifestSha256':sha(source_manifest),'noGameOrModelQualification':True})
print('PASS archive integrity and every packaged binary hash: '+str(archive))
