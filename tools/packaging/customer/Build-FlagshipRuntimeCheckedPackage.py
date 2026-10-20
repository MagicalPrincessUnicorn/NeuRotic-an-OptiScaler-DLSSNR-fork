"""Repackage the verified flagship DLLs with runtime-aware Setup; no rebuild/run."""
import argparse
import hashlib
import json
import subprocess
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[3]
BASE = REPO.parent / '_exports/NeuRotic-Flagship-Audited-371869c3-review.zip'
BASE_SHA = 'e7a5401adbd9f9ae38deef41c9a6ac6adce3c5520eeb6e244cf46780eb9e32c0'
PRODUCT_COMMIT = '371869c337b0e22e95d3ceb0ac14b067d489fb82'
PRODUCT_TREE = 'b3cf9b39b2e8b7e2d0f7983a508dd7b6a0fc4157'

def sha(raw):
    return hashlib.sha256(raw).hexdigest()

def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode()

def git(*args):
    return subprocess.check_output(['git', '-C', str(REPO), *args], text=True).strip()

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--preview', action='store_true', help='Create only the stable local test archive.')
    parser.add_argument('--installer-recovery', action='store_true', help='Package forgiving file recovery and console progress.')
    args = parser.parse_args()
    commit = git('rev-parse', 'HEAD')
    assert git('branch', '--show-current') == 'codex/flagship'
    assert git('rev-parse', 'HEAD:OptiScaler') == PRODUCT_TREE
    assert not git('diff', 'HEAD', '--name-only', '--', 'OptiScaler', 'integration/OptiScaler.ini')
    if not args.preview:
        assert not git('status', '--porcelain', '--', 'tools/packaging/customer', 'tests/Test-NeuroticPrerequisites.ps1', 'tests/Test-NeuroticRuntimePacket.py', 'tests/Test-NeuroticInstallerRecovery.py', 'tests/Test-CustomerInstaller.ps1')
    subprocess.run(['git', '-C', str(REPO), 'merge-base', '--is-ancestor', PRODUCT_COMMIT, 'HEAD'], check=True)
    assert sha(BASE.read_bytes()) == BASE_SHA
    work = REPO / 'builds/ui-release'
    receipt_label = 'installer-recovery' if args.installer_recovery else 'runtime-warning'
    output = work / f'{receipt_label}-preview.zip' if args.preview else REPO.parent / f'_exports/NeuRotic-Alpha-0.9.6-Setup-{commit[:8]}.zip'
    if args.preview:
        # This stable test artifact is regenerable; delivered exports are immutable.
        assert output.parent.resolve() == work.resolve()
        output.unlink(missing_ok=True)
    else:
        assert not output.exists() and output.parent.resolve() == (REPO.parent/'_exports').resolve()
    with zipfile.ZipFile(BASE) as archive:
        assert archive.testzip() is None
        prefix = archive.namelist()[0].split('/')[0] + '/'
        manifest = json.loads(archive.read(prefix + 'support/PACKAGE-MANIFEST.json'))
        files = {}
        for row in manifest['files']:
            path = row['path']
            raw = archive.read(prefix + path.replace('\\', '/'))
            assert path not in files and len(raw) == row['bytes'] and sha(raw) == row['sha256']
            files[path] = raw
    customer = REPO / 'tools/packaging/customer'
    # Development reports remain local; ship only the installation materials.
    files.pop('support\\FLAGSHIP-AUDIT.md', None)
    files.pop('support\\TESTING.txt', None)
    files['Collect-Diagnostics.cmd'] = files.pop('Collect-NR-Review.cmd').replace(
        b'Collect-NR-Review.ps1', b'Collect-Diagnostics.ps1')
    files['support\\Collect-Diagnostics.ps1'] = files.pop('support\\Collect-NR-Review.ps1').replace(
        b"'reviews'", b"'diagnostics'").replace(b'REVIEW_ZIP=', b'DIAGNOSTICS_ZIP=')
    for name in ('NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1'):
        files['support\\' + name] = (customer/'support'/name).read_bytes()
    # Package only our instructions, never locally supplied provider/model DLLs.
    for folder, name in (('Streamline', 'PLACE STREAMLINE FILES HERE.txt'),
                         ('NR', 'PLACE DLSSNR DLL HERE.txt')):
        files[f'Optional\\{folder}\\{name}'] = (customer/'Optional'/folder/name).read_bytes()
    build = json.loads(files['support\\BUILD-MANIFEST.json'])
    assert build['product_sha256'] == sha(files['payload\\OptiScaler.dll'])
    assert build['forwarder_sha256'] == sha(files['payload\\nvngx.dll_dlssnr.dll'])
    # Keep required integrity fields; provenance and build notes belong in receipts.
    build = {'status':'built', 'commit':commit, 'version':'0.9.6',
             'build':{'exit_code':0}, 'architecture':'x64',
             'product_sha256':build['product_sha256'], 'forwarder_sha256':build['forwarder_sha256'],
             'sdk_sha256':build['sdk_sha256'],
             'provider_model_bundled':False, 'streamline_provider_bundled':False,
             'runtime_prerequisites':{'visual_cpp_x64_minimum':'14.44.35207.0',
                 'download_url':'https://aka.ms/vc14/vc_redist.x64.exe', 'automatic_install':False,
                 'missing_runtime_policy':'warn_allow_continue'}}
    files['support\\BUILD-MANIFEST.json'] = encoded(build)
    files['READ ME.txt'] = (customer / 'READ ME.txt').read_bytes()
    files['support\\PACKAGE-MANIFEST.json'] = encoded({
        'kind':'neurotic-customer-candidate', 'name':'NeuRotic Alpha 0.9.6',
        'version':'0.9.6', 'commit':commit,
        'files':[{'path':path,'bytes':len(raw),'sha256':sha(raw)} for path,raw in sorted(files.items())]})
    with zipfile.ZipFile(output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for path,raw in sorted(files.items()):
            assert ':' not in path and '..' not in path.split('\\')
            info = zipfile.ZipInfo(output.stem + '/' + path.replace('\\','/'))
            info.date_time=(2026,9,30,0,0,0)
            archive.writestr(info,raw,compress_type=zipfile.ZIP_DEFLATED,compresslevel=6)
    with zipfile.ZipFile(output) as archive:
        assert archive.testzip() is None and len(archive.namelist()) == len(files)
    receipt={'zip':str(output),'sha256':sha(output.read_bytes()),'bytes':output.stat().st_size,
             'members':len(files),'packaging_commit':commit,'product_source_commit':PRODUCT_COMMIT,
             'product_source_tree':PRODUCT_TREE,'product_sha256':build['product_sha256'],
             'forwarder_sha256':build['forwarder_sha256'],'preview':args.preview,
             'source_package_sha256':BASE_SHA, 'customer_text_scrubbed':True}
    (work/(f'{receipt_label}-preview-receipt.json' if args.preview else f'{receipt_label}-package-receipt.json')).write_bytes(encoded(receipt))
    print(json.dumps(receipt,indent=2))

if __name__ == '__main__':
    main()
