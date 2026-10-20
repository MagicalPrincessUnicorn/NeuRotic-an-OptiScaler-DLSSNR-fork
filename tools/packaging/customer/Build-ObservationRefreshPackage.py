"""Seal an isolated review build using the verified flagship installer template."""
from pathlib import Path
import hashlib,json,subprocess,zipfile
ROOT=Path(__file__).resolve().parents[3]
BASE=ROOT.parent/'_exports/NeuRotic-Flagship-Vulkan-6176a2a3.zip'
BASE_HASH='a86fe882eb6324577726a79b2311e916b8e71001419de5ad182af87a4a0ff731'
def sha(data):return hashlib.sha256(data).hexdigest()
assert sha(BASE.read_bytes())==BASE_HASH
commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip()
receipt=json.loads((ROOT/'builds/capability-facts/release/exact-build-receipt.json').read_text())
assert receipt['exit_code']==0 and receipt['source_unchanged'] and receipt['source_commit']==commit
dll=(ROOT/'builds/capability-facts/release/OptiScaler.dll').read_bytes()
assert sha(dll)==receipt['product_sha256']
with zipfile.ZipFile(BASE) as archive:
    assert archive.testzip() is None
    prefix=archive.namelist()[0].split('/')[0]+'/'
    members={name[len(prefix):]:archive.read(name) for name in archive.namelist() if not name.endswith('/')}
inventory=json.loads(members['support/PACKAGE-MANIFEST.json'])
for row in inventory['files']:
    data=members[row['path'].replace('\\','/')]
    assert len(data)==row['bytes'] and sha(data)==row['sha256'],row['path']
original=dict(members)
members['payload/OptiScaler.dll']=dll
build=json.loads(members['support/BUILD-MANIFEST.json'])
build.update(commit=commit,product_source_commit=commit,product_sha256=sha(dll),
    candidate='Refresh observations discovery review',prior_acceptance_transfers_to_this_binary=False,
    review_status='LOCAL_VERIFIED_GAME_REVIEW_PENDING_NOT_MERGE_READY',template_archive_sha256=BASE_HASH)
build['build']={'exit_code':0,'exact_receipt':receipt}
members['support/BUILD-MANIFEST.json']=(json.dumps(build,indent=2)+'\n').encode()
members['REFRESH-TEST.txt']=(ROOT/'docs/implementation-evidence/observation-refresh/REFRESH-TEST.txt').read_bytes()
allowed={'payload/OptiScaler.dll','support/BUILD-MANIFEST.json','support/PACKAGE-MANIFEST.json','REFRESH-TEST.txt'}
assert all(members[name]==data for name,data in original.items() if name not in allowed)
inventory.update(name='NeuRotic Observation Refresh Review',commit=commit)
inventory['files']=[{'path':name.replace('/','\\'),'bytes':len(data),'sha256':sha(data)}
    for name,data in sorted(members.items()) if name!='support/PACKAGE-MANIFEST.json']
members['support/PACKAGE-MANIFEST.json']=(json.dumps(inventory,indent=2)+'\n').encode()
name=f'NeuRotic-ObservationRefresh-{commit[:8]}-review'
output=ROOT.parent/'_exports'/f'{name}.zip'
with zipfile.ZipFile(output,'w',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path,data in sorted(members.items()):archive.writestr(name+'/'+path,data)
with zipfile.ZipFile(output) as archive:
    assert archive.testzip() is None
    for row in inventory['files']:
        data=archive.read(name+'/'+row['path'].replace('\\','/'))
        assert len(data)==row['bytes'] and sha(data)==row['sha256']
result={'archive':str(output),'archive_sha256':sha(output.read_bytes()),'product_sha256':sha(dll),
    'source_commit':commit,'files_verified':len(inventory['files']),'installer_and_support_preserved':True,
    'template_sha256':BASE_HASH,'game_run':False,'merge_ready':False}
(ROOT/'builds/observation-refresh/package-receipt.json').write_text(json.dumps(result,indent=2)+'\n')
print(json.dumps(result,indent=2))
