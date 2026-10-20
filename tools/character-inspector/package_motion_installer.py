"""Build a complete CMD installer from sealed baseline dependencies and a verified DLL."""
import argparse, hashlib, json, subprocess, sys, zipfile
from pathlib import Path
sys.path.insert(0,str(Path(__file__).resolve().parent))
from package_checks import require_product_commit
ROOT=Path(__file__).resolve().parents[2]
BASE=ROOT.parent/'_exports/NeuRotic-Current-Flagship-4134edeb.zip'
BASE_SHA='2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
WORKER='payload/OptiScaler/CharacterInspector/'
sha=lambda b:hashlib.sha256(b).hexdigest()
encode=lambda v:(json.dumps(v,indent=2)+'\n').encode()
def git(*args):return subprocess.check_output(['git','-C',str(ROOT),*args],text=True).strip()
def rows(files):return [dict(path=n.replace('/','\\'),bytes=len(b),sha256=sha(b)) for n,b in sorted(files.items())]
def main():
    p=argparse.ArgumentParser();p.add_argument('--build-receipt',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True);p.add_argument('--receipt',type=Path,required=True)
    p.add_argument('--gpu-capture',action='store_true',help='Include the opt-in Crimson GPU diagnostic launcher')
    a=p.parse_args();build=json.loads(a.build_receipt.read_text());commit=git('rev-parse','HEAD')
    assert build['exit_code']==0 and build['source_unchanged']
    assert git('branch','--show-current')=='codex/inspector-experimental-20261003'
    assert git('rev-parse','HEAD:OptiScaler')==build['product_tree']
    scopes=('OptiScaler','tools/nr_semantic_worker','tools/character-inspector')
    assert not git('diff','HEAD','--name-only','--',*scopes),'Commit shipping changes first'
    assert not git('ls-files','--others','--exclude-standard','--',*scopes)
    dll=Path(build['product']['path']);raw=dll.read_bytes()
    assert sha(raw)==build['product']['sha256'] and len(raw)==build['product']['bytes']
    version=require_product_commit(dll,build['source_commit'][:8])
    assert sha(BASE.read_bytes())==BASE_SHA
    with zipfile.ZipFile(BASE) as z:
        assert z.testzip() is None
        prefix='NeuRotic/packages/current/'
        files={n[len(prefix):]:z.read(n) for n in z.namelist() if n.startswith(prefix) and not n.endswith('/')}
        old=json.loads(files['support/PACKAGE-MANIFEST.json'])
        for row in old['files']:
            b=files[row['path'].replace('\\','/')];assert sha(b)==row['sha256'] and len(b)==row['bytes']
    files['payload/OptiScaler.dll']=raw
    for path in git('ls-files','tools/nr_semantic_worker').splitlines():
        relative=path.removeprefix('tools/nr_semantic_worker/')
        if relative.startswith('evidence/') or relative=='acquire_upstream.py' or relative.endswith('.onnx'):continue
        files[WORKER+relative]=(ROOT/path).read_bytes()
    models=json.loads(files[WORKER+'models.lock.json'])['models']
    for model in models:
        b=files[WORKER+model['destination']];assert sha(b)==model['sha256'] and len(b)==model['bytes']
    name=WORKER+'DISTRIBUTION-MANIFEST.json';distribution=json.loads(files.pop(name))
    distribution.update(source_commit=commit,models=models,private_protocol_schema=3,dependency_archive_sha256=BASE_SHA)
    distribution['files']=rows({n[len(WORKER):]:b for n,b in files.items() if n.startswith(WORKER)})
    files[name]=encode(distribution)
    # Keep the original pre-App launcher and interactive workflow byte-for-byte.
    # This hash matches both the Alpha 9275af1a and early Inspector deliveries.
    assert sha(files['NeuRotic-Setup.cmd'])=='9c6158036ef49f8c487b192c59d5e9b5587c8ec8196f5da10b9262d501b41a79'
    guide=(ROOT/'docs/character-inspector/MOTION-INSTALLER.txt').read_bytes()
    files['START-HERE.txt']=guide;files['CHARACTER-INSPECTOR.txt']=guide
    if a.gpu_capture:
        for name in ('Start-MfgGpuCapture.cmd','Start-MfgGpuCapture.ps1','README-MfgGpuCapture.txt'):
            files[name]=(ROOT/'tools/character-inspector'/name).read_bytes()
        files['Candidate-Launch.ps1']=(ROOT/'tools/packaging/customer/support/Candidate-Launch.ps1').read_bytes()
        assert sha(raw).encode() in files['Start-MfgGpuCapture.ps1'], 'Capture launcher must match this exact DLL'
        files['START-HERE.txt']=(b'MFG REAL-FRAME UPDATE - resolution-switch repair awaits game verification.\r\n'
            b'Use the original NeuRotic-Setup.cmd to install. Includes real-image Inspector sampling for native D3D12 Streamline.\r\n'
            b'For an optional GPU fault capture, follow README-MfgGpuCapture.txt and run\r\n'
            b'Start-MfgGpuCapture.cmd from this extracted folder.\r\n\r\n'+guide)
    oldbuild=json.loads(files['support/BUILD-MANIFEST.json'])
    metadata={k:oldbuild[k] for k in ('status','version','architecture','forwarder_sha256','sdk_sha256',
        'provider_model_bundled','streamline_provider_bundled','runtime_prerequisites')}
    metadata.update(commit=commit,package_source_commit=commit,product_source_commit=build['source_commit'],
        branch=git('branch','--show-current'),build={'exit_code':0},product_sha256=sha(raw),product_version=version,
        config_sha256=sha(files['payload/OptiScaler.ini']),exact_verification=build,
        candidate='Inspector DX11/D3D12/Vulkan compatibility - original NeuRotic Setup installer',base_archive_sha256=BASE_SHA,
        prior_acceptance_transfers_to_this_binary=False,game_qualified=False,game_runtime_verified=False)
    files['support/BUILD-MANIFEST.json']=encode(metadata)
    files['support/PRODUCT-BUILD-RECEIPT.json']=encode(build)
    name='support/PACKAGE-MANIFEST.json';files.pop(name)
    # The baseline's current-component attestations describe a different DLL,
    # schema and GUI bundle. Preserve dependency identity without inheriting them.
    old={k:old[k] for k in ('kind','version')}
    old.update(commit=commit,product_source_commit=build['source_commit'],name='NeuRotic Inspector Multi-API Compatibility',
        base_archive_sha256=BASE_SHA,component_provenance={'inspector':{
            'source_commit':commit,'product_source_commit':build['source_commit'],
            'product_sha256':sha(raw),'private_protocol_schema':3,'game_qualified':False}})
    old['files']=rows(files);files[name]=encode(old)
    with zipfile.ZipFile(a.output,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as z:
        for n,b in sorted(files.items()):z.writestr('NeuRotic-Inspector/'+n,b)
    with zipfile.ZipFile(a.output) as z:
        assert z.testzip() is None
        assert len(z.namelist())==len({n.casefold() for n in z.namelist()})
        for n,b in files.items():assert z.read('NeuRotic-Inspector/'+n)==b
    receipt=dict(path=str(a.output.resolve()),bytes=a.output.stat().st_size,sha256=sha(a.output.read_bytes()),
        members=len(files),product_source_commit=build['source_commit'],product_sha256=sha(raw),
        package_source_commit=commit,private_protocol_schema=3,base_archive_sha256=BASE_SHA,
        all_member_hashes_verified=True,models=[m['sha256'] for m in models],game_qualified=False)
    a.receipt.write_bytes(encode(receipt));print(json.dumps(receipt,indent=2))
if __name__=='__main__':main()
