"""Record exact task source/binary identities and closed-registry producer coverage."""
import pathlib, json, hashlib, subprocess
root=pathlib.Path(__file__).resolve().parents[2]
receipt=root/'docs/implementation-evidence/capability-facts'
base='23b1cb299d8e378371ba9eb7753f499d6ae4bc6b'
def git(*args): return subprocess.check_output(['git','-c',f'safe.directory={root}',*args],cwd=root,text=True)
paths=set(git('diff','--name-only',base,'--','OptiScaler').splitlines())
paths.update(git('ls-files','--others','--exclude-standard','OptiScaler').splitlines())
paths.update(['OptiScaler/resource_build_date.h','OptiScaler/resource_build_commit.h'])
def identity(path):
    data=path.read_bytes()
    return {'bytes':len(data),'sha256':hashlib.sha256(data).hexdigest()}
data={'base_commit':base,'embedded_commit':'c122ada7','toolchain':'VS18/v145 MSVC14.44.35207 Release x64',
      'changed_product_files':{p:identity(root/p) for p in sorted(paths) if (root/p).is_file()},
      'binary':identity(root/'builds/capability-facts/release/OptiScaler.dll')}
(receipt/'SOURCE_MANIFEST.json').write_text(json.dumps(data,indent=2)+'\n')
registry=json.loads((root/'tests/capability/registry.json').read_text())
current={'config.requested_enabled','config.requested_route'}
correlation={'ngx.raw_requirements','ngx.effective_requirements','parameter.synthetic_defaults','parameter.storage',
             'native.owner_state','native.counter','present.owner_state','provider.owner_support','provider.enabled',
             'hdr.color_class','hdr.metadata','lifecycle.open'}
lines=['# Frozen registry producer coverage','',
       'All 38 predicates are validated and queryable. This table distinguishes actual observation wiring from merely recognizing an ID. No missing producer is replaced with a successful default.','',
       '| Capability | Rightful producer | This slice |','|---|---|---|']
for row in registry['ids']:
    short=row['id'][7:]
    status='Unwired; Unknown until rightful evidence/receipt is available'
    if short in current: status='Observed settings; independent fresh config epoch permits current as-of query'
    elif short in correlation: status='Observed values; correlation/unknown identities prevent qualified current claims'
    elif short=='present.counter': status='Observed process summary; no per-chain/frame attribution'
    lines.append('| `'+row['id']+'` | '+', '.join(row['producers'])+' | '+status+' |')
lines+=['','NGX raw/effective outputs and parameter readbacks preserve existing host calls/returns; they do not prove vendor support or model consumption. Synthetic defaults are labeled as synthetic. Native/Present/HDR/provider/lifecycle do not fabricate missing device, queue, feature, frame, reset, resource or semantic owner receipts.','',
        'Settings and summaries are observations at explicit refresh, not ongoing readiness promises. Minimal exports redact bindings and identity values; imported data is historical even when a local tuple happens to match. Latest event replacement keeps one latest image per writer.']
(receipt/'PRODUCER_COVERAGE.md').write_text('\n'.join(lines)+'\n')
