"""Seal the verified Passive Discovery DLL with the accepted reversible installer."""
from __future__ import annotations
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BASE = REPO.parent / '_exports/NeuRotic-W03-UI-Preflight-23b1cb29-review.zip'
BASE_SHA = 'c93db9e6c5d62097f43dc63e8ae0117af7a863f5e30a4962ad795e3c99a8b925'
PRODUCT_COMMIT = '52e0c6bb'
KEEP = {
    'Collect-NR-Review.cmd', 'NeuRotic-Setup.cmd', 'NeuRotic-Uninstall.cmd',
    'payload\\Licenses\\DirectX_LICENSE.txt', 'payload\\Licenses\\FidelityFX_v1_LICENSE.md',
    'payload\\Licenses\\FidelityFX_v2_LICENSE.md', 'payload\\Licenses\\RenoDX_ATTRIBUTION.txt',
    'payload\\Licenses\\XeSS_LICENSE.txt', 'payload\\NeuRotic-LICENSE.txt',
    'payload\\OptiScaler.ini', 'payload\\OptiScaler\\amd_fidelityfx_dx12.dll',
    'payload\\nvngx.dll_dlssnr.dll', 'support\\Collect-NR-Review.ps1',
    'support\\NeuRotic-Setup-Engine.ps1',
}

def sha(raw: bytes) -> str:
    return hashlib.sha256(raw).hexdigest()

def encoded(value: object) -> bytes:
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')

def git(*args: str) -> str:
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()

def build(destination: Path) -> dict:
    if destination.exists():
        raise ValueError('Choose a fresh output; sealed packages are never overwritten.')
    if sha(BASE.read_bytes()) != BASE_SHA:
        raise ValueError('Accepted installer bytes changed.')
    commit = git('rev-parse', 'HEAD')
    product_commit = git('rev-parse', PRODUCT_COMMIT)
    if git('rev-parse', 'HEAD:OptiScaler') != git('rev-parse', product_commit + ':OptiScaler') or git('diff', 'HEAD', '--', 'OptiScaler'):
        raise ValueError('Product source differs from the verified product commit.')
    receipt_path = REPO / 'docs/implementation-evidence/passive-discovery/verification.json'
    receipt = json.loads(receipt_path.read_text(encoding='utf-8'))
    for name, expected in receipt['product_source_sha256'].items():
        if sha((REPO / name).read_bytes()) != expected:
            raise ValueError('Verified source changed: ' + name)
    product = (REPO / 'builds/candidate-observer/release/out/OptiScaler.dll').read_bytes()
    if sha(product) != receipt['product_dll']['sha256']:
        raise ValueError('Verified DLL changed.')
    files = {}
    with zipfile.ZipFile(BASE) as prior:
        if prior.testzip() is not None:
            raise ValueError('Accepted archive is damaged.')
        prefix = prior.namelist()[0].split('/')[0] + '/'
        manifest = json.loads(prior.read(prefix + 'support/PACKAGE-MANIFEST.json'))
        for row in manifest['files']:
            raw = prior.read(prefix + row['path'].replace('\\', '/'))
            if sha(raw) != row['sha256'].lower() or len(raw) != row['bytes']:
                raise ValueError('Accepted member changed: ' + row['path'])
            if row['path'] in KEEP:
                files[row['path']] = raw
    if files.keys() != KEEP:
        raise ValueError('Accepted installer assets are missing.')
    for old, new in (('NeuRotic-Setup.cmd', 'NeuRotic-Manual-Setup.cmd'), ('NeuRotic-Uninstall.cmd', 'NeuRotic-Manual-Uninstall.cmd')):
        files.pop(old)
        files[new] = (REPO / 'tools/packaging/customer' / new).read_bytes()
    for name in ('NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1'):
        files['support\\' + name] = (REPO / 'tools/packaging/customer/support' / name).read_bytes()
    files['payload\\OptiScaler.dll'] = product
    additions = {
        'Start-Candidate-Control.cmd': 'tools/packaging/customer/Start-Candidate-Control.cmd',
        'Start-Candidate-Observer.cmd': 'tools/packaging/customer/Start-Candidate-Observer.cmd',
        'support\\Start-CandidateCapture.ps1': 'tools/packaging/customer/support/Start-CandidateCapture.ps1',
        'support\\Candidate-Launch.ps1': 'tools/packaging/customer/support/Candidate-Launch.ps1',
        'README.txt': 'tools/packaging/customer/PASSIVE-DISCOVERY-TESTING.txt',
        'support\\TESTING.txt': 'tools/packaging/customer/PASSIVE-DISCOVERY-TESTING.txt',
        'support\\PASSIVE-DISCOVERY-VERIFICATION.json': str(receipt_path.relative_to(REPO)),
        'support\\candidate_observer.py': 'tools/diagnostics/candidate_observer.py',
    }
    for path in (REPO / 'tools/diagnostics/candidate_observer').iterdir():
        if path.is_file():
            additions['support\\candidate_observer\\' + path.name] = str(path.relative_to(REPO))
    files.update({name: (REPO / source).read_bytes() for name, source in additions.items()})
    build_record = {
        'status': 'built', 'commit': commit, 'branch': git('branch', '--show-current'),
        'product_commit': product_commit,
        'build': {'exit_code': 0, 'source_unchanged': True, 'configuration': 'Release|x64',
                  'binary_reused': True, 'verification': 'support\\PASSIVE-DISCOVERY-VERIFICATION.json'},
        'package_source_dirty': bool(git('status', '--porcelain')),
        'source_package_sha256': BASE_SHA, 'product_sha256': sha(product),
        'provider_model_bundled': False, 'game_validation': 'NOT_RUN',
        'package_sources': {source: sha((REPO / source).read_bytes()) for source in sorted(set(additions.values()))},
    }
    files['support\\BUILD-MANIFEST.json'] = encoded(build_record)
    package_record = {
        'kind': 'neurotic-customer-candidate', 'name': destination.stem, 'commit': commit, 'branch': build_record['branch'],
        'lifecycle': 'experimental-review', 'provider_model_bundled': False,
        'files': [{'path': name, 'bytes': len(raw), 'sha256': sha(raw)} for name, raw in sorted(files.items())],
    }
    files['support\\PACKAGE-MANIFEST.json'] = encoded(package_record)
    destination.parent.mkdir(parents=True, exist_ok=True)
    prefix = destination.stem + '/'
    with zipfile.ZipFile(destination, 'x', compression=zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, raw in sorted(files.items()):
            archive.writestr(prefix + name.replace('\\', '/'), raw)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None or set(archive.namelist()) != {prefix + name.replace('\\', '/') for name in files}:
            raise ValueError('Output archive verification failed.')
        for name, raw in files.items():
            if archive.read(prefix + name.replace('\\', '/')) != raw:
                raise ValueError('Output member changed: ' + name)
    return {'path': str(destination), 'sha256': sha(destination.read_bytes()), 'bytes': destination.stat().st_size,
            'members': len(files), 'product_sha256': sha(product), 'package_source_dirty': build_record['package_source_dirty']}

if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    print(json.dumps(build(parser.parse_args().output.resolve()), indent=2))
