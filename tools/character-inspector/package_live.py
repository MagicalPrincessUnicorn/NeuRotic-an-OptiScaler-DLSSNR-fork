"""Seal the rebuilt experimental DLL and offline worker into the existing installer.

Run only after a clean source commit and successful Release build. Runtime and
wheel roots are explicit inputs; no acquisition or game installation occurs here.
"""
import argparse, hashlib, json, os, re, struct, subprocess, zipfile, io
from pathlib import Path
from package_checks import require_product_commit

ROOT=Path(__file__).resolve().parents[2]
WORK=ROOT/'builds/character-inspector'
sha=lambda raw:hashlib.sha256(raw).hexdigest()
encode=lambda value:(json.dumps(value,indent=2)+'\n').encode()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()

def tree_files(path,exclude=()):
    """Enumerate a known real dependency tree, never traverse a junction."""
    for directory,dirs,files in os.walk(path,followlinks=False):
        dirs[:]=[d for d in dirs if d not in exclude and not(Path(directory,d).stat().st_file_attributes&0x400)]
        for name in files:
            p=Path(directory,name)
            if p.suffix=='.pyc' or p.stat().st_file_attributes&0x400:continue
            yield p,p.relative_to(path).as_posix()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--runtime',type=Path,required=True)
    parser.add_argument('--packages',type=Path,required=True)
    parser.add_argument('--product-commit',required=True)
    args=parser.parse_args()
    source=git('rev-parse',args.product_commit);packaging=git('rev-parse','HEAD')
    assert git('branch','--show-current')=='codex/character-inspector-experimental'
    assert not git('status','--porcelain','--','OptiScaler','tools/nr_semantic_worker','tools/character-inspector')
    assert not git('diff',source,'--name-only','--','OptiScaler','tools/nr_semantic_worker')
    assert (WORK/'live-build-exit.txt').read_text(encoding='utf-8-sig').strip()=='0'
    assert source[:8] in (ROOT/'OptiScaler/resource_build_commit.h').read_text()
    version=require_product_commit(WORK/'bin/OptiScaler.dll',git('rev-parse','--short',source))
    receipt=json.loads((WORK/'live-build-receipt.json').read_text())
    assert receipt['exit_code']==0 and receipt['source_commit']==source and receipt['product_version']==version
    dll=(WORK/'bin/OptiScaler.dll').read_bytes();pe=struct.unpack_from('<I',dll,0x3c)[0]
    assert sha(dll)==receipt['product_sha256']
    assert dll[:2]==b'MZ' and dll[pe:pe+4]==b'PE\0\0' and struct.unpack_from('<H',dll,pe+4)[0]==0x8664
    base=Path('C:/NeuRotic/_exports/NeuRotic-Alpha-0.9.6-Setup-9275af1a.zip')
    assert sha(base.read_bytes())=='39a77c01009f0e6cf465996b1e3c1157667b3948984270ae5309b6f85f93ad48'
    files={}
    with zipfile.ZipFile(base) as archive:
        assert archive.testzip() is None
        prefix=archive.namelist()[0].split('/')[0]+'/'
        manifest=json.loads(archive.read(prefix+'support/PACKAGE-MANIFEST.json'))
        for row in manifest['files']:
            name=row['path'];raw=archive.read(prefix+name.replace('\\','/'))
            assert len(raw)==row['bytes'] and sha(raw)==row['sha256']
            files[name]=raw
    base_build=json.loads(files['support\\BUILD-MANIFEST.json'])
    customer=ROOT/'tools/packaging/customer'
    for name in ('NeuRotic-Setup-Engine.ps1','NeuRotic-Prerequisites.ps1'):
        files['support\\'+name]=(customer/'support'/name).read_bytes()
    files['support\\Collect-Diagnostics.ps1']=(customer/'support/Collect-NR-Review.ps1').read_bytes().replace(b"'reviews'",b"'diagnostics'").replace(b'REVIEW_ZIP=',b'DIAGNOSTICS_ZIP=')
    files['payload\\OptiScaler.dll']=dll
    ini=(ROOT/'integration/OptiScaler.ini').read_bytes()
    for section,key,value in [('Menu','OverlayMenu','true'),('Menu','ShortcutKey','45'),('Log','LogToFile','true'),('Log','LogAsync','true'),('DlssNr','Enabled','false')]:
        pattern=re.compile(rb'(?ms)^\['+section.encode()+rb'\].*?(?=^\[|\Z)');match=pattern.search(ini);assert match
        fragment,count=re.subn(rb'(?m)^('+key.encode()+rb'\s*=\s*)[^\r\n]*',lambda m:m[1]+value.encode(),match[0]);assert count==1
        ini=ini[:match.start()]+fragment+ini[match.end():]
    assert b'[CharacterInspector]' in ini and b'Enabled=false' in ini
    files['payload\\OptiScaler.ini']=ini
    files['payload\\Licenses\\CharacterInspector-MIT.txt']=(ROOT/'docs/character-inspector/REFERENCE_LICENSE.txt').read_bytes()
    worker=ROOT/'tools/nr_semantic_worker';prefix='payload\\OptiScaler\\CharacterInspector\\'
    for p,name in tree_files(worker,('runtime','packages','evidence','__pycache__')):
        if p.name=='acquire_upstream.py':continue
        files[prefix+name.replace('/','\\')]=p.read_bytes()
    locks=json.loads((worker/'models.lock.json').read_text())['models']
    for model in locks:
        assert sha(files[prefix+model['destination'].replace('/','\\')])==model['sha256']
    # Ship real files, never the development junctions. The full stdlib excludes
    # build/test/GUI/install tooling and site-packages; imports use private wheels.
    for name in ('python.exe','python3.dll','python312.dll','vcruntime140.dll','vcruntime140_1.dll','LICENSE.txt'):
        files[prefix+'runtime\\'+name]=(args.runtime/name).read_bytes()
    for p,name in tree_files(args.runtime/'DLLs'):
        if 'tkinter' in name or name.startswith(('tcl','tk8')):continue
        files[prefix+'runtime\\DLLs\\'+name.replace('/','\\')]=p.read_bytes()
    # CPython's standard Windows zip landmark avoids hundreds of separately
    # installed stdlib files, while preserving normal isolated import behavior.
    library=io.BytesIO()
    with zipfile.ZipFile(library,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for p,name in tree_files(args.runtime/'Lib',('site-packages','__pycache__','test','tests','ensurepip','idlelib','tkinter')):
            archive.writestr(name,p.read_bytes())
    files[prefix+'runtime\\python312.zip']=library.getvalue()
    for p,name in tree_files(args.packages,('__pycache__','tests','test','bin')):
        if p.suffix in ('.pyi','.h','.hpp','.pxd','.c','.cpp','.cmake','.rst'):continue
        files[prefix+'packages\\'+name.replace('/','\\')]=p.read_bytes()
    worker_manifest={'schema':1,'source_commit':source,'python':'3.12','numpy':'2.3.5','opencv':'4.13.0.92',
        'models':locks,'network_required_at_runtime':False,'packaged_models':True,
        'files':[{'path':name[len(prefix):].replace('\\','/'),'bytes':len(raw),'sha256':sha(raw)} for name,raw in sorted(files.items()) if name.startswith(prefix)]}
    files[prefix+'DISTRIBUTION-MANIFEST.json']=encode(worker_manifest)
    files['READ ME.txt']=('''NEUROTIC CHARACTER INSPECTOR - LIVE EXPERIMENTAL SETUP

1. Extract the COMPLETE ZIP outside the game folder. Close the target game.
2. Run NeuRotic-Setup.cmd and select CrimsonDesert.exe (or another D3D12 game).
   Choose dxgi.dll; use Setup's backup/replace or ReShade options if it exists.
   Keep the separate DOOM/Vulkan patching installation unchanged.
3. After full installation verification, launch the game and press Insert.
4. Turn frame generation OFF, including any external FG mod. Leave Enable NR off.
   Override DLSSG Ratio: Game follows the game's request; Off forces DLSSG off.
   Game no longer pauses inspection merely because an earlier request said On.
5. Neural Rendering > Character Inspector: Enable Character Inspector, then
   Start live inspection. This loads the bundled local CPU provider and models.
   Starting live mode clears synthetic previews. Close the menu with Insert.
6. Expect sampled character boxes only when the detector produces a usable pose.
   Boxes expire after their hold; unknown color or focus loss revokes them.
   Stop live inspection or disable the inspector to stop its worker.
7. Box hold (ms) defaults to 500 and can be adjusted from 0 to 2000. Each person's
   last sampled position persists between detections; missed detections do not
   renew that person's lifetime. Scene, focus, resize and FG changes revoke it.
   Maximum persons defaults to 4. A setting of 1 limits inspection to one person.
   If boxes disappear, check processed/fresh/expired samples and processing time
   in the inspector panel. Reduce Maximum persons if samples arrive too late.
   After testing, close the game and run Collect-Diagnostics.cmd for that game.

D3D12 SDR, HDR10/PQ and scRGB capture have synthetic GPU tests. HDR conversion
affects only a private analysis image. FG, DX11, interop bridges and Vulkan live
capture are unsupported. Preview full-game overlay remains a separate synthetic
test. CPU samples have a 10Hz ceiling. Capture must be fresh within 150ms before
inference; replies have a 500ms bound with hold enabled, or 150ms without hold.
Held geometry retains each person's original capture and receipt timestamps.
Counts/IDs are sampled associations, not semantic game identities.
Native FG pausing follows tracked forwarded generation calls, including calls
still in flight and 500ms of recent activity. This is an activity observation,
not a universal FG-enabled query; unobserved external generators are unsupported.
Normal 8-bit SDR swapchains now accept DXGI's default color space even when
the game never calls SetColorSpace1. Explicit color changes retain their own
provenance. This repairs the permanent SDR refusal seen in two returned sessions.

This experimental model can miss armor/partial bodies and produce false positives.
The supplied Crimson Desert title image produced one false pose in the local
provider test. Recognition accuracy, gameplay performance and game stability
are NOT qualified. The earlier DEVICE_HUNG crash is not proven fixed.

Includes setup, repair, uninstall, offline Python/NumPy/OpenCV, two Apache-2.0
models and redistribution notices. No download or model loading occurs until
explicit live start. Existing settings are preserved; fresh Inspector is OFF.
No game has been installed or modified automatically by creating this archive.

To uninstall, close the game and run NeuRotic-Uninstall.cmd for that executable,
or Uninstall NeuRotic.cmd in its folder. Setup retains rollback history.

Standard installer guide:

''').replace('\n','\r\n').encode()+(customer/'READ ME.txt').read_bytes()
    files['support\\BUILD-MANIFEST.json']=encode({**base_build,'commit':packaging,'product_source_commit':source,
        'product_sha256':sha(dll),'product_version':version,'config_sha256':sha(ini),'branch':git('branch','--show-current'),
        'candidate':'Character Inspector sampled live CPU experimental','live_character_inference':True,
        'game_executed':False,'game_qualified':False,'prior_game_acceptance_transferred':False})
    files['support\\PACKAGE-MANIFEST.json']=encode({'kind':'neurotic-customer-candidate','name':'NeuRotic Character Inspector Live Experimental Setup',
        'version':'0.9.6','commit':packaging,'product_source_commit':source,
        'files':[{'path':name,'bytes':len(raw),'sha256':sha(raw)} for name,raw in sorted(files.items())]})
    output=Path('C:/NeuRotic/_exports')/('NeuRotic-CharacterInspector-Live-'+source[:8]+'-'+packaging[:8]+'.zip')
    folder='NeuRotic' # Avoid duplicating a long archive name in Windows extraction paths.
    with zipfile.ZipFile(output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
        for name,raw in sorted(files.items()):archive.writestr(folder+'/'+name.replace('\\','/'),raw)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None
        for row in json.loads(archive.read(folder+'/support/PACKAGE-MANIFEST.json'))['files']:
            raw=archive.read(folder+'/'+row['path'].replace('\\','/'));assert len(raw)==row['bytes'] and sha(raw)==row['sha256']
    receipt={'path':str(output),'bytes':output.stat().st_size,'sha256':sha(output.read_bytes()),'members':len(files),
        'all_member_hashes_verified':True,'product_source_commit':source,'packaging_commit':packaging,'product_sha256':sha(dll),
        'models_bundled':True,'worker_bundled':True,'game_files_changed':False,'game_qualified':False}
    (WORK/'live-package-receipt.json').write_bytes(encode(receipt));print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
