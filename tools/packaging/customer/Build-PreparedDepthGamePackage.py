"""Seal the verified prepared-depth DLL with reversible setup and A/B launchers."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BASE = REPO.parent / '_exports/NeuRotic-W03-UI-Preflight-23b1cb29-review.zip'
BASE_PIN = 'c93db9e6c5d62097f43dc63e8ae0117af7a863f5e30a4962ad795e3c99a8b925'

def sha(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()

def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2, ensure_ascii=False) + '\n').encode('utf-8')

def git(*args: str) -> str:
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()

def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--preview', action='store_true', help='allow packaging-only uncommitted files; product hashes still required')
    args = parser.parse_args()
    output = args.output.resolve()
    assert not output.exists(), 'Sealed package exists; do not overwrite it'
    assert git('branch', '--show-current') == 'codex/prepared-depth-guides'
    dirty = bool(git('status', '--porcelain'))
    assert not dirty or args.preview, 'Final package requires committed packaging sources'
    receipt = json.loads((REPO / 'builds/prepared-depth/BUILD-RECEIPT.json').read_text(encoding='utf-8-sig'))
    assert receipt['buildExitCode'] == 0 and receipt['runtimeQualification'] == 'NOT_RUN'
    for row in receipt['sourceHashes']:
        assert sha((REPO / row['path']).read_bytes()) == row['sha256'], row['path']
    product_commit = receipt['offRampCommit']
    subprocess.check_call(['git', '-C', str(REPO), 'merge-base', '--is-ancestor', product_commit, 'HEAD'])
    product = (REPO / 'builds/prepared-depth/release/out/OptiScaler.dll').read_bytes()
    assert sha(product) == receipt['dllSha256'] and len(product) == receipt['dllBytes']
    assert sha(BASE.read_bytes()) == BASE_PIN, 'Accepted base package changed'
    with zipfile.ZipFile(BASE) as archive:
        roots = {name.split('/')[0] for name in archive.namelist()}
        assert len(roots) == 1
        prefix = roots.pop() + '/'
        old = json.loads(archive.read(prefix + 'support/PACKAGE-MANIFEST.json'))
        paths = [row['path'].replace('\\', '/') for row in old['files']]
        assert len(paths) == len(set(path.casefold() for path in paths))
        assert len(archive.namelist()) == len(paths) + 1
        for row, path in zip(old['files'], paths):
            assert sha(archive.read(prefix + path)) == row['sha256'], path
        files = {path.replace('/', '\\'): archive.read(prefix + path)
                 for path in paths if path.startswith('payload/')}
    files['payload\\OptiScaler.dll'] = product
    files['payload\\OptiScaler.ini'] = (REPO / 'integration/OptiScaler.ini').read_bytes()
    ini_bytes = files['payload\\OptiScaler.ini']
    ini = ini_bytes.decode('utf-16' if ini_bytes.startswith((b'\xff\xfe', b'\xfe\xff')) else 'utf-8-sig')
    assert 'NativeProtocol=false' in ini and 'ExperimentalMode = false' in ini
    assert 'LogToFile = false' in ini
    customer = REPO / 'tools/packaging/customer'
    for path in ('NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd', 'Collect-NR-Review.cmd',
                 'support/NeuRotic-Setup-Engine.ps1', 'support/NeuRotic-Prerequisites.ps1', 'support/NeuRotic-RuntimeFiles.ps1', 'support/Collect-NR-Review.ps1'):
        files[path.replace('/', '\\')] = (customer / path).read_bytes()
    depth = customer / 'prepared-depth'
    for name in ('Launch-Baseline.cmd', 'Launch-PreparedDepth.cmd', 'READ ME.txt'):
        files[name] = (depth / name).read_bytes()
    files['support\\Start-PreparedDepth-Test.ps1'] = (depth / 'Start-PreparedDepth-Test.ps1').read_bytes()
    files['support\\TESTING.txt'] = files['READ ME.txt']
    commit = git('rev-parse', 'HEAD')
    build = {'status': 'built', 'commit': commit, 'branch': 'codex/prepared-depth-guides',
             'product_commit': product_commit, 'build': {'exit_code': 0, 'source_unchanged': True},
             'packaging_preview': args.preview, 'packaging_source_dirty': dirty,
             'source_package_sha256': BASE_PIN, 'product_sha256': sha(product),
             'forwarder_sha256': sha(files['payload\\nvngx.dll_dlssnr.dll']),
             'sdk_sha256': sha(files['payload\\OptiScaler\\amd_fidelityfx_dx12.dll']),
             'runtime_scope': 'NOT_RUN; recorded copies/wrapper results are not qualification',
             'aggregate_status': receipt['robustness'], 'provider_model_bundled': False,
             'artifacts': [{'name': name, 'sha256': sha(files['payload\\'+name])}
                           for name in ('OptiScaler.dll', 'nvngx.dll_dlssnr.dll')]}
    files['support\\BUILD-MANIFEST.json'] = encoded(build)
    files['support\\BUILD-RECEIPT.json'] = encoded(receipt)
    for path in files:
        assert ':' not in path and '..' not in path.split('\\')
        assert not path.lower().endswith(('\\nvngx_dlssnr.dll', '.pdb', '.obj', '.lib', '.exp', '.pch'))
    name = output.stem
    manifest = {'kind': 'neurotic-customer-candidate', 'lifecycle': 'experimental-review',
                'name': name, 'commit': commit, 'product_commit': product_commit,
                'branch': 'codex/prepared-depth-guides', 'public_release': False, 'deployed': False,
                'runtime_result': 'Inconclusive; prepared-depth game test pending',
                'source_package_sha256': BASE_PIN,
                'files': [{'path': path, 'bytes': len(raw), 'sha256': sha(raw)}
                          for path, raw in sorted(files.items())]}
    files['support\\PACKAGE-MANIFEST.json'] = encoded(manifest)
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            info = zipfile.ZipInfo(name + '/' + path.replace('\\', '/'))
            info.date_time = (2026, 9, 30, 0, 0, 0)
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert len(archive.namelist()) == len(files)
        for path, raw in files.items():
            assert archive.read(name + '/' + path.replace('\\', '/')) == raw, path
    print(json.dumps({'zip': str(output), 'sha256': sha(output.read_bytes()),
                      'bytes': output.stat().st_size, 'members': len(files),
                      'product_commit': product_commit, 'packaging_commit': commit,
                      'product_sha256': sha(product), 'archive_roundtrip': True}, indent=2))

if __name__ == '__main__':
    main()
