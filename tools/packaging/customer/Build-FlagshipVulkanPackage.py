"""Seal a freshly verified flagship build using the accepted customer installer."""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BASE = REPO.parent / '_exports/NeuRotic-Alpha-0.9.6-Setup-9275af1a.zip'
BASE_SHA = '39a77c01009f0e6cf465996b1e3c1157667b3948984270ae5309b6f85f93ad48'


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode()


def git(*args):
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source-commit', required=True)
    args = parser.parse_args()
    commit = git('rev-parse', 'HEAD')
    assert git('branch', '--show-current') == 'codex/flagship'
    assert re.fullmatch(r'[0-9a-f]{40}', args.source_commit)
    assert not git('diff', args.source_commit, '--name-only', '--', 'OptiScaler', 'integration/OptiScaler.ini')
    assert not git('status', '--porcelain', '--', 'tools/packaging/customer/Build-FlagshipVulkanPackage.py')
    build_root = REPO / 'builds/present-multipass-release'
    receipt = json.loads((build_root / 'exact-build-receipt.json').read_text(encoding='utf-8-sig'))
    assert receipt['source_commit'] == args.source_commit
    assert receipt['target'] == 'Rebuild' and receipt['exit_code'] == 0 and receipt['source_unchanged']
    assert sha(BASE.read_bytes()) == BASE_SHA
    with zipfile.ZipFile(BASE) as archive:
        assert archive.testzip() is None
        prefix = archive.namelist()[0].split('/')[0] + '/'
        manifest = json.loads(archive.read(prefix + 'support/PACKAGE-MANIFEST.json'))
        files = {}
        for row in manifest['files']:
            path = row['path']
            assert ':' not in path and '..' not in path.replace('\\', '/').split('/') and path not in files
            raw = archive.read(prefix + path.replace('\\', '/'))
            assert len(raw) == row['bytes'] and sha(raw) == row['sha256']
            files[path] = raw
    # Capture tools are optional; the accepted installer and ordinary launch remain unchanged.
    additions = {
        'Start-Candidate-Control.cmd': 'tools/packaging/customer/Start-Candidate-Control.cmd',
        'Start-Candidate-Observer.cmd': 'tools/packaging/customer/Start-Candidate-Observer.cmd',
        'support\\Start-CandidateCapture.ps1': 'tools/packaging/customer/support/Start-CandidateCapture.ps1',
        'support\\Candidate-Launch.ps1': 'tools/packaging/customer/support/Candidate-Launch.ps1',
        'support\\candidate_observer.py': 'tools/diagnostics/candidate_observer.py',
        'PASSIVE-DISCOVERY-TESTING.txt': 'tools/packaging/customer/PASSIVE-DISCOVERY-TESTING.txt',
    }
    for path in (REPO / 'tools/diagnostics/candidate_observer').iterdir():
        if path.is_file():
            additions['support\\candidate_observer\\' + path.name] = str(path.relative_to(REPO))
    files.update({name: (REPO / source).read_bytes() for name, source in additions.items()})
    # Keep the public launcher name; it calls Collect-Diagnostics.ps1.
    files['support\\Collect-Diagnostics.ps1'] = (REPO / 'tools/packaging/customer/support/Collect-NR-Review.ps1').read_bytes().replace(
        b"'reviews'", b"'diagnostics'").replace(b'REVIEW_ZIP=', b'DIAGNOSTICS_ZIP=')
    prior_build = json.loads(files['support\\BUILD-MANIFEST.json'])
    for name, field in [('OptiScaler.dll', 'product_sha256'), ('nvngx.dll_dlssnr.dll', 'forwarder_sha256')]:
        raw = (build_root / 'bin' / name).read_bytes()
        assert raw[:2] == b'MZ' and sha(raw) == receipt[field]
        files['payload\\' + name] = raw
    ini = (REPO / 'integration/OptiScaler.ini').read_bytes()
    assert sha(ini) == receipt['config_sha256']
    assert sha((REPO / 'docs/implementation-evidence/vulkan-experiment/VULKAN-SHADERS.json').read_bytes()) == receipt['shaders_sha256']
    files['payload\\OptiScaler.ini'] = ini
    notice = (REPO / 'OptiScaler/mfg/THIRD_PARTY_MFGAdaUnlock_LICENSE').read_bytes()
    if notice.decode('utf-8-sig').replace('\r\n', '\n').strip() not in files['payload\\NeuRotic-LICENSE.txt'].decode('utf-8-sig').replace('\r\n', '\n'):
        files['payload\\NeuRotic-LICENSE.txt'] += b'\r\n\r\nMFG Ada Unlock policy (ImDreamt):\r\n' + notice
    files['support\\BUILD-MANIFEST.json'] = encoded({
        'status': 'built', 'commit': commit, 'product_source_commit': args.source_commit,
        'version': '0.9.6', 'architecture': 'x64', 'build': {'exit_code': 0},
        'product_sha256': receipt['product_sha256'], 'forwarder_sha256': receipt['forwarder_sha256'],
        'sdk_sha256': prior_build['sdk_sha256'],
        'provider_model_bundled': False, 'streamline_provider_bundled': False,
        'runtime_prerequisites': prior_build['runtime_prerequisites'],
        'candidate': 'Flagship Vulkan and Passive Discovery integration test', 'prior_acceptance_transfers_to_this_binary': False})
    files['support\\VULKAN-TEST.txt'] = (
        'Vulkan legacy GPU timing isolation candidate\r\n\r\n'
        'This build removes legacy upscaler GPU timestamp insertion; performance recovery requires this test.\r\n'
        'Native Vulkan upscaler GPU timing reads unavailable; D3D12-provider timing is retained.\r\n'
        'NR rendering, model/color, startup preparation, input and overlay behavior are retained.\r\n'
        'The accepted installer and Passive Discovery tools are retained.\r\n'
        'Keep the same game settings and FG off for this comparison.\r\n'
        'With the game closed, ensure [Log] LogToFile=true and LogLevel=2 in OptiScaler.ini.\r\n'
        'Do not use LogLevel=auto: that selects verbose trace logging in this product.\r\n'
        'Launch normally through Steam with Enable NR off. Enter the same level.\r\n'
        'Keep the menu closed; stand still for 10 seconds, turn the camera for 20 seconds, then stand still again.\r\n'
        'If available, compare mouse and controller camera movement.\r\n'
        'First report whether NR-off camera movement improves. If it does, also compare Present and Native.\r\n'
        'Exit the game and collect a fresh Diagnostics ZIP. Send it with those observations.\r\n'
        'NR_HOST_COST summaries run every two seconds, for at most 600 snapshots after first Vulkan Present.\r\n'
        'Frequent tracker/recording callbacks are sampled; input and Present boundaries are fully timed.\r\n'
        'Times include scheduling and locks. Nested categories cannot be summed as total frame cost.\r\n'
        'Driver/provider call time and diagnostic-report time are separate from selected NeuRotic work.\r\n'
        'Measurements do not prove GPU time, displayed frames, or complete hook coverage.\r\n'
        'NR-off retains Vulkan tracking, input and overlay hooks; this build measures those paths.\r\n'
        'Native exposure/color and exact Present depth-guide qualification remain unresolved separately.\r\n'
        'The legacy timer creates no query pool and records no query reset or timestamp commands.\r\n'
        'The private NR model remains separate from the included forwarder.\r\n'
    ).encode('ascii')
    files['support\\PACKAGE-MANIFEST.json'] = encoded({
        'kind': 'neurotic-customer-candidate', 'name': 'NeuRotic Alpha 0.9.6 - Vulkan test',
        'version': '0.9.6', 'commit': commit,
        'files': [{'path': path, 'bytes': len(raw), 'sha256': sha(raw)} for path, raw in sorted(files.items())]})
    output = REPO.parent / f'_exports/NeuRotic-Flagship-Vulkan-{args.source_commit[:8]}.zip'
    assert not output.exists()
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path, raw in sorted(files.items()):
            info = zipfile.ZipInfo(output.stem + '/' + path.replace('\\', '/'))
            info.date_time = (2026, 9, 30, 0, 0, 0)
            archive.writestr(info, raw, compress_type=zipfile.ZIP_DEFLATED, compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None and len(archive.namelist()) == len(files)
    result = {'zip': str(output), 'sha256': sha(output.read_bytes()), 'bytes': output.stat().st_size,
              'packaging_commit': commit, 'product_source_commit': args.source_commit,
              'product_sha256': receipt['product_sha256'], 'forwarder_sha256': receipt['forwarder_sha256'],
              'source_package_sha256': BASE_SHA, 'accepted_installer_preserved': True,
              'prior_acceptance_transfers_to_this_binary': False}
    (build_root / 'flagship-vulkan-package-receipt.json').write_bytes(encoded(result))
    print(json.dumps(result, indent=2))


if __name__ == '__main__':
    main()
