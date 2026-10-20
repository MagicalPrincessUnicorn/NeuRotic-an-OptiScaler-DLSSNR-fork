"""Personal experimental full installer; reuses sealed components and verified binaries."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT.parent / '_exports/NeuRotic-Current-Flagship-4134edeb.zip'
BASE_SHA = '2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
REF = ROOT / 'references/prepared-guide-installer'
EVIDENCE = ROOT / 'docs/implementation-evidence/universal-guides-installer-20261004'
def sha(raw): return hashlib.sha256(raw).hexdigest()
def encode(value): return (json.dumps(value, indent=2)+'\n').encode()
def git(*args): return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()
def rows(files): return [dict(path=n.replace('/','\\'),bytes=len(b),sha256=sha(b)) for n,b in sorted(files.items())]
def cmd(script, action):
    return ('@echo off\r\nsetlocal\r\nset "PSModulePath="\r\n"%SystemRoot%\\System32\\WindowsPowerShell\\v1.0\\powershell.exe" -NoProfile -ExecutionPolicy Bypass -File "%~dp0support\\'+script+'" '+action+' %*\r\nset "NR_TEST_RESULT=%ERRORLEVEL%"\r\nif not "%NR_TEST_RESULT%"=="0" pause\r\nexit /b %NR_TEST_RESULT%\r\n').encode()

def main():
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--output',type=Path,required=True);parser.add_argument('--preview',action='store_true');parser.add_argument('--native',action='store_true');parser.add_argument('--native-verification',type=Path);args=parser.parse_args()
    assert args.native or args.native_verification is None, '--native-verification requires --native'
    assert git('branch','--show-current') in ('codex/universal-guides-experimental','codex/universal-vulkan-performance-60625fed')
    assert args.preview or not git('status','--porcelain'), 'Commit packaging sources before final seal'
    assert not args.output.exists(), 'Preserve existing delivered ZIP'
    assert sha(BASE.read_bytes())==BASE_SHA
    pins=json.loads((REF/'SOURCES.json').read_text())
    for row in pins['files']:
        raw=(REF/row['path']).read_bytes();assert len(raw)==row['bytes'] and sha(raw)==row['sha256'],row['path']
    with zipfile.ZipFile(BASE) as z:
        assert z.testzip() is None
        manifest=json.loads(z.read('NeuRotic/ARCHIVE-MANIFEST.json'));files={}
        for row in manifest['files']:
            path=row['path'].replace('\\','/');assert not path.startswith('/') and '..' not in path.split('/') and ':' not in path
            raw=z.read('NeuRotic/'+path);assert len(raw)==row['bytes'] and sha(raw)==row['sha256'].lower(),path;files[path]=raw
        assert set(z.namelist())=={'NeuRotic/'+n for n in files}|{'NeuRotic/ARCHIVE-MANIFEST.json'}
    original_commands={name:files[name] for name in ('NeuRotic-Setup.cmd','NeuRotic-Uninstall.cmd')}
    verification=json.loads((ROOT/'docs/implementation-evidence/universal-guides-20261004/VERIFICATION.json').read_text(encoding='utf-8-sig'))
    startup_path=ROOT/'docs/implementation-evidence/universal-guides-launch-20261004/ADDON-FIX.json'
    depth_path=ROOT/'docs/implementation-evidence/universal-guides-depth-20261004/ADDON-FIX.json'
    mainline_path=ROOT/'docs/implementation-evidence/reshade-mainline-20261004/VERIFICATION.json'
    native_path=args.native_verification or ROOT/'docs/implementation-evidence/native-guides-20261004/VERIFICATION.json'
    fix_path=native_path if args.native else mainline_path if mainline_path.exists() else depth_path if depth_path.exists() else startup_path
    addon_fix=json.loads(fix_path.read_text(encoding='utf-8-sig')) if fix_path.exists() else None
    replacements={row['path']:row for row in addon_fix['artifacts']} if addon_fix else {}
    if addon_fix:
        for row in addon_fix['artifacts']:
            raw=(ROOT/row['path']).read_bytes();assert len(raw)==row['bytes'] and sha(raw)==row['sha256'],row['path']
        for row in addon_fix['sources']:
            assert sha((ROOT/row['path']).read_bytes())==row['sha256'],row['path']
    for row in verification['artifacts']:
        if args.native and row['path'] not in ('builds/universal-guides/bin/OptiScaler.dll','builds/universal-guides/flow/neurotic_prepared_flow.lib'):
            continue  # External helpers/addons are neither rebuilt nor shipped by the native package.
        row=replacements.get(row['path'],row)
        raw=(ROOT/row['path']).read_bytes();assert len(raw)==row['bytes'] and sha(raw)==row['sha256'],row['path']
    product_commit=addon_fix['productSourceCommit'] if addon_fix and 'productSourceCommit' in addon_fix else git('rev-parse','0c9b928d')
    assert git('rev-parse','HEAD:OptiScaler')==git('rev-parse',product_commit+':OptiScaler'), 'Product changed since verified build'
    prefix='packages/current/'
    # Keep the dependency delivery and optional App/Anything components; their
    # original source provenance is retained rather than declaring a new build.
    for path in list(files):
        if path.endswith('PACKAGE-MANIFEST.json'): del files[path]
    files[prefix+'payload/OptiScaler.dll']=(ROOT/'builds/universal-guides/bin/OptiScaler.dll').read_bytes()
    # Preserve the sealed Vulkan-capable game forwarder. The helper uses its own
    # freshly built, hash-pinned D3D11/D3D12 companion beneath PreparedGuides.
    assets={
        'NeuRotic.PreparedHelper.exe':'builds/universal-guides/helper/NeuRotic.PreparedHelper.exe',
        'nvngx.dll_dlssnr.dll':'builds/universal-guides/helper/nvngx.dll_dlssnr.dll',
        'Shaders/NeuRotic_PreparedGuides.fx':'addons/prepared-guides/shaders/NeuRotic_PreparedGuides.fx',
        'Shaders/NeuRotic_SourceGuidedColor.fx':'addons/prepared-guides/shaders/NeuRotic_SourceGuidedColor.fx',
        'LICENSE-DLSS5-FEEDER.txt':'addons/prepared-guides/LICENSE-DLSS5-FEEDER.txt',
        'README.md':'addons/prepared-guides/README.md'}
    for name,path in assets.items():files[prefix+'payload/OptiScaler/PreparedGuides/'+name]=(ROOT/path).read_bytes()
    for name,path in [('NeuRotic-PreparedGuides.addon64','builds/prepared-guides/NeuRotic-PreparedGuides.addon64'),('WinPixEventRuntime.dll','builds/prepared-guides/WinPixEventRuntime.dll')]:files[prefix+'payload/'+name]=(ROOT/path).read_bytes()
    runtime=REF/'ReShade_Setup_6.8.0_Addon.exe';runtime_bytes=runtime.read_bytes()
    with zipfile.ZipFile(runtime) as z:files[prefix+'payload/ReShade64.dll']=z.read('ReShade64.dll')
    files['Optional/ReShade/ReShade_Setup_6.8.0_Addon.exe']=runtime_bytes
    files['Optional/ReShade/LICENSE.md']=(REF/'ReShade-LICENSE.md').read_bytes()
    files[prefix+'payload/Licenses/Prepared-ReShade-BSD.txt']=(REF/'ReShade-LICENSE.md').read_bytes()
    files[prefix+'support/UniversalConnectionDependencies.json']=(ROOT/'tools/packaging/customer/UniversalConnectionDependencies.json').read_bytes()
    dependencies=[]
    for folder in ('drme','reshade-shaders'):
        for path in sorted((REF/folder).iterdir()):
            if path.suffix in ('.fx','.fxh'):target=prefix+'payload/OptiScaler/PreparedGuides/Shaders/'+path.name
            else:target=prefix+'payload/OptiScaler/PreparedGuides/'+folder+'/'+path.name
            raw=path.read_bytes();files[target]=raw;dependencies.append(dict(path=target,sha256=sha(raw)))
    files[prefix+'payload/OptiScaler/PreparedGuides/Prepared.ini']=b'Techniques=DRME@MotionEstimation.fx,NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\nTechniqueSorting=DRME@MotionEstimation.fx,NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\n'
    files[prefix+'payload/OptiScaler/PreparedGuides/BuiltIn.ini']=b'Techniques=NeuRotic_PreparedGuides@NeuRotic_PreparedGuides.fx\r\n'
    files[prefix+'payload/OptiScaler/PreparedGuides/Baseline.ini']=b'Techniques=\r\n'
    attribution='Personal noncommercial test package. DRME is unmodified Jakob Wapenhensch ReshadeMotionEstimation, commit 5fc3f434ba158bfc380a71b60aa5a2bddf2242d6, CC BY-NC 4.0. https://github.com/JakobPCoder/ReshadeMotionEstimation https://creativecommons.org/licenses/by-nc/4.0/\nReShade runtime 6.8.0 (addon build) by Patrick Mours, BSD-3-Clause. https://reshade.me/\nReShade standard shader headers commit fd0022170615ce0d8162d219bff07232fa6dd84f; ReShade.fxh CC0-1.0. Original notices remain in source. External components are not relicensed as NeuRotic.\n'
    files[prefix+'payload/OptiScaler/PreparedGuides/ATTRIBUTION.txt']=attribution.encode()
    customer=ROOT/'tools/packaging/customer';src=customer/'prepared-guides'
    for name in ('PreparedProfile.ps1','PreparedSetup.ps1','Start-PreparedTest.ps1'):files['support/'+name]=(src/name).read_bytes()
    # Use this branch's installer and launcher helpers, preserving manifest-based
    # backup/restore rather than writing arbitrary game files from a new owner.
    for name in ('NeuRotic-Setup-Engine.ps1','NeuRotic-ConnectionDependencies.ps1','Candidate-Launch.ps1'):files[prefix+'support/'+name]=(customer/'support'/name).read_bytes()
    files['NeuRotic-Setup.cmd']=cmd('PreparedSetup.ps1','-Action Setup')
    files['NeuRotic-Uninstall.cmd']=cmd('PreparedSetup.ps1','-Action Uninstall')
    files['Restore-Test-Settings.cmd']=cmd('PreparedSetup.ps1','-Action Restore')
    files['ReShade-Setup.cmd']=cmd('PreparedSetup.ps1','-Action ReShade')
    for name,mode in [('Start-Prepared-Test.cmd','Prepared'),('Start-Baseline-Test.cmd','Baseline'),('Start-D3D12-Flow-Test.cmd','BuiltIn'),('Start-Vulkan-Shared-Test.cmd','Shared'),('Start-Vulkan-Transport-Only.cmd','TransportOnly'),('Start-Vulkan-Prepared-Test.cmd','VulkanPrepared'),('Start-Vulkan-Baseline-Test.cmd','VulkanBaseline')]:files[name]=cmd('Start-PreparedTest.ps1',mode)
    readme=(src/'READ ME.txt').read_bytes();files['START HERE.txt']=files['READ ME.txt']=files['README-FIRST.txt']=files['README.md']=readme
    files['support/UNIVERSAL-GUIDES-VERIFICATION.json']=encode(verification)
    if startup_path.exists():files['support/ADDON-STARTUP-FIX.json']=startup_path.read_bytes()
    if addon_fix:files['support/ADDON-VERIFICATION.json']=encode(addon_fix)
    files['support/PREPARED-DEPENDENCIES.json']=encode(dict(reshade_setup_url='https://reshade.me/downloads/ReShade_Setup_6.8.0_Addon.exe',reshade_setup_sha256=sha(runtime_bytes),drme_commit='5fc3f434ba158bfc380a71b60aa5a2bddf2242d6',standard_shaders_commit='fd0022170615ce0d8162d219bff07232fa6dd84f',files=dependencies))
    files['support/PREPARED-SOURCES.json']=encode(pins)
    if args.native:
        # Native acquisition, flow and dispatch are compiled into the core.
        # Keep the standard installer and do not install an external producer.
        for name in list(files):
            if (name.startswith('Optional/ReShade/') or name.startswith('support/PREPARED-') or
                name in ('support/PreparedProfile.ps1','support/PreparedSetup.ps1','support/Start-PreparedTest.ps1','ReShade-Setup.cmd','Restore-Test-Settings.cmd') or
                name in ('support/ADDON-STARTUP-FIX.json','support/ADDON-VERIFICATION.json','support/UNIVERSAL-GUIDES-VERIFICATION.json') or
                name.startswith('Start-') and name.endswith('-Test.cmd') or name=='Start-Vulkan-Transport-Only.cmd' or
                name.startswith(prefix+'payload/OptiScaler/PreparedGuides/') or
                name in (prefix+'payload/NeuRotic-PreparedGuides.addon64',prefix+'payload/ReShade64.dll',prefix+'payload/Licenses/Prepared-ReShade-BSD.txt')):
                del files[name]
        files.update(original_commands)
        readme=(src/'NATIVE READ ME.txt').read_bytes()
        for name in ('START HERE.txt','READ ME.txt','README-FIRST.txt','README.md'):files[name]=readme
        files['support/NATIVE-GUIDES-VERIFICATION.json']=encode(addon_fix)
        files[prefix+'payload/Licenses/Native-FidelityFX.txt']=(ROOT/'addons/prepared-guides/flow/LICENSE-FIDELITYFX.txt').read_bytes()
        fg_path='builds/native-framegen/bridge/NeuRotic.Fsr3.Vulkan.dll'
        if fg_path in replacements:
            files[prefix+'payload/OptiScaler/NeuRotic.Fsr3.Vulkan.dll']=(ROOT/fg_path).read_bytes()
            files[prefix+'payload/Licenses/Native-FSR-Vulkan-PROVENANCE.json']=(ROOT/'addons/native-framegen/PROVENANCE.json').read_bytes()
    commit=git('rev-parse','HEAD');prior_build=json.loads(files[prefix+'support/BUILD-MANIFEST.json'])
    build=dict(status='built',version='universal-guides-experimental',architecture='x64',build=dict(exit_code=verification['mainProductBuildExit']),forwarder_sha256=prior_build['forwarder_sha256'],sdk_sha256=prior_build['sdk_sha256'],provider_model_bundled=False,streamline_provider_bundled=False,runtime_prerequisites=prior_build['runtime_prerequisites'],prior_acceptance_transfers_to_this_binary=False,experimental_verification=verification,reused_delivery_build_manifest=prior_build)
    build.update(commit=commit,branch=git('branch','--show-current'),product_source_commit=product_commit,product_sha256=sha(files[prefix+'payload/OptiScaler.dll']),game_runtime_verified=False,game_qualified=False,candidate='Universal guides personal test',reused_dependency_delivery_sha256=BASE_SHA)
    if startup_path.exists():build['addon_startup_fix_verification']=json.loads(startup_path.read_text(encoding='utf-8-sig'))
    if addon_fix:
        build['addon_verification']=addon_fix
        if 'mainProductBuildExit' in addon_fix:build['build']['exit_code']=addon_fix['mainProductBuildExit']
    if args.native:
        build['candidate']='Native Vulkan captured guides personal test'
        build['version']='native-vulkan-guides-experimental'
        build['experimental_verification']=addon_fix
        build.pop('addon_verification',None)
        build.pop('addon_startup_fix_verification',None)
        build['native_guides_verification']=addon_fix
        build['requires_reshade']=False
    files[prefix+'support/BUILD-MANIFEST.json']=encode(build)
    installer={n[len(prefix):]:b for n,b in files.items() if n.startswith(prefix)}
    files[prefix+'support/PACKAGE-MANIFEST.json']=encode(dict(kind='neurotic-customer-candidate',name='Native Vulkan Guides Experimental' if args.native else 'Universal Guides Experimental',commit=commit,product_source_commit=product_commit,public_release=False,game_qualified=False,files=rows(installer)))
    archive_manifest=dict(source_commit=commit,product_source_commit=product_commit,base_source_commit=git('rev-parse','60625fed' if git('branch','--show-current')=='codex/universal-vulkan-performance-60625fed' else '422acbfa'),reused_component_provenance=manifest['component_provenance'],experimental_core=dict(source_commit=product_commit,sha256=sha(files[prefix+'payload/OptiScaler.dll'])),personal_noncommercial=True,game_qualified=False,files=rows(files))
    files['ARCHIVE-MANIFEST.json']=encode(archive_manifest)
    shaders={n.rsplit('/',1)[-1]:b for n,b in files.items() if '/PreparedGuides/Shaders/' in n}
    import re
    for name,raw in shaders.items():
        for dependency in re.findall(rb'^\s*#include\s+"([^"]+)"',raw,re.M):assert dependency.decode() in shaders,(name,dependency)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(args.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,raw in sorted(files.items()):z.writestr('NeuRotic/'+name,raw)
    with zipfile.ZipFile(args.output) as z:
        assert z.testzip() is None
        for name,raw in files.items():assert z.read('NeuRotic/'+name)==raw,name
    evidence=fix_path.parent if addon_fix else EVIDENCE
    evidence.mkdir(parents=True,exist_ok=True)
    (evidence/'PACKAGE.json').write_bytes(encode(dict(zip=str(args.output),bytes=args.output.stat().st_size,sha256=sha(args.output.read_bytes()),packaging_source_commit=commit,product_source_commit=product_commit,preview=args.preview,members=len(files),game_executed=False,compiler_intermediates_generated=0)))
    print(f'PASS: {len(files)} sealed members: {args.output}')
if __name__=='__main__':main()
