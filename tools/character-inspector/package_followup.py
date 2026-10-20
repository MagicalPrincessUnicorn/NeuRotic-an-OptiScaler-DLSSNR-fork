"""Seal inspector code/models, a verified DLL and a complete discovery runtime.

Reuse sealed companion assets, never unfinished shared Hub/support sources.
No build, dependency acquisition, or game installation occurs here.
"""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import zipfile
sys.path.insert(0,str(Path(__file__).resolve().parent))
from package_checks import require_product_commit

ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT.parent/'_exports/NeuRotic-Flagship-All-Merges-ca9a2c72-review.zip'
BASE_SHA='3733a3240f69deafb3109c61784dc0663989cf62562cacb81848434156b1b996'
DISCOVERY_BASE=ROOT.parent/'_exports/NeuRotic-Hub-Library-AntiCheat-review.zip'
DISCOVERY_BASE_SHA='7545a033d6706795946d218866f180cd83a70ffd7d14adddf93023593e8290f2'
PREFIX='packages/current/'
WORKER=PREFIX+'payload/OptiScaler/CharacterInspector/'
sha=lambda raw:hashlib.sha256(raw).hexdigest()
encode=lambda value:(json.dumps(value,indent=2)+'\n').encode('utf-8')
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()
def rows(files,windows=False):
    return [{'path':name.replace('/','\\') if windows else name,'bytes':len(raw),'sha256':sha(raw)}
            for name,raw in sorted(files.items())]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--build-receipt',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    args=parser.parse_args();build=json.loads(args.build_receipt.read_text())
    snapshot=git('rev-parse','HEAD')
    assert build['exit_code']==0 and build['source_unchanged']
    assert git('branch','--show-current')=='codex/flagship'
    assert git('rev-parse','HEAD:OptiScaler')==build['product_tree']
    scopes=('OptiScaler','tools/nr_semantic_worker','OptiScaler.ini','integration/OptiScaler.ini')
    assert not git('diff','HEAD','--name-only','--',*scopes),'Commit shipping changes first'
    assert not git('ls-files','--others','--exclude-standard','--',*scopes),'Uncommitted shipping files'
    dll=Path(build['product']['path']);raw=dll.read_bytes()
    assert sha(raw)==build['product']['sha256'] and len(raw)==build['product']['bytes']
    version=require_product_commit(dll,build['source_commit'][:8])
    assert sha(BASE.read_bytes())==BASE_SHA
    with zipfile.ZipFile(BASE) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len(set(n.casefold() for n in z.namelist()))
        manifest=json.loads(z.read('NeuRotic/ARCHIVE-MANIFEST.json'));files={}
        for row in manifest['files']:
            name=row['path'];assert ':' not in name and '..' not in name.split('/') and not name.startswith('/')
            data=z.read('NeuRotic/'+name);assert sha(data)==row['sha256'] and len(data)==row['bytes'];files[name]=data
        assert {'NeuRotic/'+n for n in files}|{'NeuRotic/ARCHIVE-MANIFEST.json'}==set(z.namelist())
    # The original companion archive contains only an apphost EXE. Its matching
    # managed DLL and self-contained runtime must travel together after extraction.
    assert sha(DISCOVERY_BASE.read_bytes())==DISCOVERY_BASE_SHA
    with zipfile.ZipFile(DISCOVERY_BASE) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len(set(n.casefold() for n in z.namelist()))
        inventory=json.loads(z.read('NeuRotic/ARCHIVE-MANIFEST.json'))['files']
        assert {'NeuRotic/'+row['path'] for row in inventory}|{'NeuRotic/ARCHIVE-MANIFEST.json'}==set(z.namelist())
        discovery={}
        for row in inventory:
            name=row['path']
            assert ':' not in name and '..' not in name.split('/') and not name.startswith('/')
            if not name.startswith('discovery/'):continue
            data=z.read('NeuRotic/'+name)
            assert len(data)==row['bytes'] and sha(data)==row['sha256']
            discovery[name]=data
    required={'NeuRotic.Discovery.exe','NeuRotic.Discovery.dll','NeuRotic.Discovery.deps.json',
              'NeuRotic.Discovery.runtimeconfig.json','hostfxr.dll','hostpolicy.dll','coreclr.dll'}
    assert {'discovery/'+name for name in required}.issubset(discovery),'Incomplete discovery runtime'
    runtime=json.loads(discovery['discovery/NeuRotic.Discovery.runtimeconfig.json'])['runtimeOptions']
    assert runtime['includedFrameworks']==[{'name':'Microsoft.NETCore.App','version':'10.0.12'}]
    notices=ROOT/'tools/character-inspector/notices/dotnet-runtime-10.0.12'
    for name,expected in (
        ('LICENSE.TXT','d7a68596ab69b06f51ca278a6545148e4269a9381c26d597c13df5d88e08cf5b'),
        ('THIRD-PARTY-NOTICES.TXT','6d15e10a101c6bfff2ab4429ed061bf76c456fc4b23ad6b03e0d0f8377148a21')):
        data=(notices/name).read_bytes();assert sha(data)==expected
        discovery['discovery/'+name]=data
    files={name:data for name,data in files.items() if not name.startswith('discovery/')}
    files.update(discovery)
    files['verification/inspector-discovery-publish.json']=encode({
        'source_archive_sha256':DISCOVERY_BASE_SHA,'files':rows(discovery),
        'helper_sha256':sha(discovery['discovery/NeuRotic.Discovery.exe']),
        'helper_dll_sha256':sha(discovery['discovery/NeuRotic.Discovery.dll'])})
    files[ PREFIX+'payload/OptiScaler.dll' ]=raw
    ini=(ROOT/'integration/OptiScaler.ini').read_bytes()
    # Match the verified review installer defaults without editing user INIs.
    import re
    for section,key,value in [('Menu','OverlayMenu','true'),('Menu','ShortcutKey','45'),
                              ('Log','LogToFile','true'),('Log','LogAsync','true'),('DlssNr','Enabled','false')]:
        match=re.search(rb'(?ms)^\['+section.encode()+rb'\].*?(?=^\[|\Z)',ini);assert match
        fragment,n=re.subn(rb'(?m)^('+key.encode()+rb'\s*=\s*)[^\r\n]*',lambda m:m[1]+value.encode(),match[0]);assert n==1
        ini=ini[:match.start()]+fragment+ini[match.end():]
    files[PREFIX+'payload/OptiScaler.ini']=ini
    for name in git('ls-files','tools/nr_semantic_worker').splitlines():
        relative=name.removeprefix('tools/nr_semantic_worker/')
        if relative.startswith('evidence/') or relative=='acquire_upstream.py' or relative.endswith('.onnx'):continue
        files[WORKER+relative]=(ROOT/name).read_bytes()
    models=json.loads((ROOT/'tools/nr_semantic_worker/models.lock.json').read_text())['models']
    for model in models:
        data=(ROOT/'tools/nr_semantic_worker'/model['destination']).read_bytes()
        assert sha(data)==model['sha256'] and len(data)==model['bytes'];files[WORKER+model['destination']]=data
    distribution_name=WORKER+'DISTRIBUTION-MANIFEST.json'
    distribution=json.loads(files.pop(distribution_name))
    distribution.update(source_commit=snapshot,models=models,private_protocol_schema=2,
                        dependency_archive_sha256=BASE_SHA)
    distribution['files']=rows({n[len(WORKER):]:v for n,v in files.items() if n.startswith(WORKER)},windows=True)
    files[distribution_name]=encode(distribution)
    files[PREFIX+'CHARACTER-INSPECTOR.txt']=(ROOT/'docs/character-inspector/FOLLOWUP-USER-GUIDE.md').read_bytes()
    build_name=PREFIX+'support/BUILD-MANIFEST.json';metadata=json.loads(files[build_name])
    metadata.update(commit=snapshot,product_source_commit=build['source_commit'],
                    product_sha256=sha(raw),product_version=version,config_sha256=sha(ini),
                    exact_verification=build,candidate='People and objects overlay review',
                    base_archive_sha256=BASE_SHA,game_qualified=False,game_runtime_verified=False)
    files[build_name]=encode(metadata)
    package_name=PREFIX+'support/PACKAGE-MANIFEST.json';package=json.loads(files.pop(package_name))
    package.update(commit=snapshot,product_source_commit=build['source_commit'])
    package['files']=rows({n[len(PREFIX):]:v for n,v in files.items() if n.startswith(PREFIX)},windows=True)
    assert any(row['path']=='payload\\OptiScaler.dll' for row in package['files'])
    files[package_name]=encode(package)
    registry=json.loads(files['support/Hub-Packages.json']);registry['packages'][0]['manifestSha256']=sha(files[package_name])
    files['support/Hub-Packages.json']=encode(registry)
    files['README-INSPECTOR.txt']=(ROOT/'docs/character-inspector/FOLLOWUP-USER-GUIDE.md').read_bytes()
    files['ARCHIVE-MANIFEST.json']=encode({'source_commit':snapshot,
        'base_archive_sha256':BASE_SHA,'discovery_archive_sha256':DISCOVERY_BASE_SHA,'files':rows(files)})
    with zipfile.ZipFile(args.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for name,data in sorted(files.items()):z.writestr('NeuRotic/'+name,data)
    with zipfile.ZipFile(args.output) as z:
        assert z.testzip() is None
        for row in json.loads(z.read('NeuRotic/ARCHIVE-MANIFEST.json'))['files']:
            data=z.read('NeuRotic/'+row['path']);assert len(data)==row['bytes'] and sha(data)==row['sha256']
    receipt={'path':str(args.output.resolve()),'bytes':args.output.stat().st_size,'sha256':sha(args.output.read_bytes()),
        'members':len(files),'product_source_commit':build['source_commit'],'product_sha256':sha(raw),
        'hub_package_manifest_sha256':sha(files[package_name]),'all_member_hashes_verified':True,
        'private_protocol_schema':2,'models':[m['sha256'] for m in models],
        'base_archive_sha256':BASE_SHA,'hub_assets_reused':True,'game_files_changed':False,'game_qualified':False,
        'discovery_archive_sha256':DISCOVERY_BASE_SHA,'discovery_files':len(discovery),
        'discovery_helper_sha256':sha(discovery['discovery/NeuRotic.Discovery.exe']),
        'discovery_helper_dll_sha256':sha(discovery['discovery/NeuRotic.Discovery.dll'])}
    args.receipt.write_bytes(encode(receipt));print(json.dumps(receipt,indent=2))

if __name__=='__main__':main()
