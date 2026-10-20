"""Seal verified Hub inputs into an existing verified combined product package."""
import argparse
import hashlib
import json
import re
import subprocess
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
EVIDENCE = ROOT / 'docs/implementation-evidence/hub-library-polish-20261003'
SUPPORT = ROOT / 'tools/packaging/customer/support'
MODULES = ('NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1',
           'NeuRotic-HubProtocol.ps1', 'NeuRotic-HubHistory.ps1','NeuRotic-HubSanitize.ps1','NeuRotic-HubDumbfire.ps1', 'NeuRotic-HubSettings.ps1','NeuRotic-ObjectRules.ps1','object-rules-v1.schema.json', 'NeuRotic-HubJson.cs',
           'NeuRotic-HubAntiCheat.ps1', 'NeuRotic-AntiCheatCollector.cs',
           'NeuRotic-AntiCheatPolicy.ps1', 'NeuRotic-AntiCheatProviders.json',
           'neurotic_anticheat_rules.json', 'exe_names.txt')

def sha(raw): return hashlib.sha256(raw).hexdigest()
def encode(value): return (json.dumps(value, indent=2) + '\n').encode()
def read(path):
    path = Path(path)
    if any(p.is_symlink() or p.is_junction() for p in (path, *path.absolute().parents)): raise RuntimeError('Linked package input refused')
    return path.read_bytes()
def source_path(relative):
    path = Path(relative)
    if path.is_absolute() or ':' in relative or '..' in path.parts: raise RuntimeError('Unsafe source input')
    return ROOT / path
def rows(files, windows=False):
    return [{'path': p.replace('/', '\\') if windows else p,
             'bytes': len(b), 'sha256': sha(b)} for p, b in sorted(files.items())]

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--base-archive', required=True)
    parser.add_argument('--base-sha256', required=True)
    parser.add_argument('--output-name', required=True)
    args = parser.parse_args()
    if not re.fullmatch(r'NeuRotic-Hub-[A-Za-z0-9_-]{1,64}', args.output_name): raise RuntimeError('Invalid output identity')
    destination = ROOT.parent / '_exports' / (args.output_name + '.zip')
    if destination.exists(): raise RuntimeError('Preserve the existing delivery')
    native = json.loads(read(EVIDENCE / 'native-build.json').decode('utf-8-sig'))
    if native['exitCode'] != 0 or not native['sourceUnchanged']: raise RuntimeError('Unverified native build')
    for item in native['inputs']:
        if sha(read(source_path(item['path']))) != item['sha256']: raise RuntimeError('Native source changed: ' + item['path'])
    if 'PASS suite All' not in read(EVIDENCE / 'all-tests-final.log').decode('utf-8-sig'): raise RuntimeError('Full Hub suite missing')
    raw = read(args.base_archive)
    if sha(raw) != args.base_sha256.lower(): raise RuntimeError('Base package changed')
    with zipfile.ZipFile(args.base_archive) as archive:
        if archive.testzip() is not None: raise RuntimeError('Corrupt base archive')
        names = archive.namelist()
        if len(names) != len(set(n.casefold() for n in names)): raise RuntimeError('Duplicate archive member')
        for n in names:
            if not n.startswith('NeuRotic/') or ':' in n or '..' in n.split('/'): raise RuntimeError('Unsafe archive member')
        files = {n.removeprefix('NeuRotic/'): archive.read(n) for n in names}
    manifest = json.loads(files.pop('ARCHIVE-MANIFEST.json'))
    if {r['path'] for r in manifest['files']} != set(files): raise RuntimeError('Base inventory mismatch')
    for item in manifest['files']:
        if sha(files[item['path']]) != item['sha256'] or len(files[item['path']]) != item['bytes']: raise RuntimeError('Base member mismatch')
    hub = read(ROOT / 'builds/hub/bin/NeuRotic.Hub.exe')
    if sha(hub) != native['hubSha256']: raise RuntimeError('Hub binary changed')
    files['NeuRotic.Hub.exe'] = hub
    files['discovery/NeuRotic.Discovery.exe'] = read(ROOT / 'builds/hub/bin/discovery/NeuRotic.Discovery.exe')
    helper = json.loads(read(EVIDENCE / 'roots-helper-provenance.json'))
    if sha(files['discovery/NeuRotic.Discovery.exe']) != helper['helper_exe_sha256']: raise RuntimeError('Discovery binary changed')
    discovery = ROOT / 'builds/hub/bin/discovery'
    if sha(read(discovery / 'NeuRotic.Discovery.dll')) != helper['helper_dll_sha256']: raise RuntimeError('Discovery managed binary changed')
    # The EXE is an apphost, not a single-file deployment. Include its complete
    # published runtime; copying the EXE alone makes scans fail after extraction.
    published = {}
    for path in sorted(discovery.rglob('*')):
        if path.is_symlink() or path.is_junction(): raise RuntimeError('Linked discovery publish output')
        if path.is_file(): published[path.relative_to(discovery).as_posix()] = read(path)
    required = {'NeuRotic.Discovery.exe', 'NeuRotic.Discovery.dll',
                'NeuRotic.Discovery.deps.json', 'NeuRotic.Discovery.runtimeconfig.json',
                'hostfxr.dll', 'hostpolicy.dll', 'coreclr.dll'}
    if not required.issubset(published): raise RuntimeError('Incomplete self-contained discovery runtime')
    for name, raw in published.items(): files['discovery/' + name] = raw
    files['verification/discovery-publish.json'] = encode({'files': rows(published)})
    for item in helper['source_inputs']:
        if sha(read(source_path(item['path']))) != item['sha256']: raise RuntimeError('Discovery source changed')
    for name in MODULES:
        raw = read(SUPPORT / name)
        if raw != read(ROOT / 'builds/hub/bin/support' / name): raise RuntimeError('Staged support differs: ' + name)
        files['support/' + name] = raw
        files['packages/current/support/' + name] = raw
    for name, expected in [('NeuRotic.WindowWorker.exe', 'b17c5e7dacc5ef4985694691c1b31a4dd483344e26529b5d666c373b7affc451'),
                           ('nvngx.dll_dlssnr.dll', 'af7125b2c7f44a03043877b5490b85b80c9bdd6f4f91f7172e6b173af7eacb4e')]:
        raw = read(ROOT / 'builds/hub/bin/window-worker' / name)
        if sha(raw) != expected: raise RuntimeError('Worker identity changed')
        files['Tools/WindowWorker/' + name] = raw
    files['Tools/WindowWorker/INTEGRATION.md'] = read(ROOT / 'apps/NeuRoticWindowWorker/INTEGRATION.md')
    commit = subprocess.check_output(['git', '-C', str(ROOT), 'rev-parse', 'HEAD'], text=True).strip()
    for item in native['inputs']:
        files['Source/Hub/' + item['path']] = read(source_path(item['path']))
    for name in MODULES: files['Source/Hub/tools/packaging/customer/support/' + name] = read(SUPPORT / name)
    prefix = 'packages/current/'
    inner = {p[len(prefix):]: b for p, b in files.items() if p.startswith(prefix) and p != prefix + 'support/PACKAGE-MANIFEST.json'}
    inner_manifest = json.loads(files[prefix + 'support/PACKAGE-MANIFEST.json'])
    inner_manifest.update(hub_source_commit=commit, files=rows(inner, True))
    files[prefix + 'support/PACKAGE-MANIFEST.json'] = encode(inner_manifest)
    registry = json.loads(files['support/Hub-Packages.json'])
    registry['packages'][0]['name'] = 'NeuRotic Alpha 0.9.6'
    registry['packages'][0]['manifestSha256'] = sha(files[prefix + 'support/PACKAGE-MANIFEST.json'])
    files['support/Hub-Packages.json'] = encode(registry)
    files['README-FIRST.txt'] = b'''NEUROTIC HUB REVIEW BUILD\r\n\r\nExtract the complete ZIP and open NeuRotic.Hub.exe. Keep all folders together.\r\nLibrary roots, shortcut capture, sliders and local anti-cheat warnings are included.\r\nOptional Streamline and private NR model files are supplied by you.\r\nAnything is experimental; provide your DLSS NR file before choosing a window.\r\nNo account-safety certification is provided. Game and HDR qualification remain open.\r\nDirect Setup/Uninstall tools use the same single payload under packages/current.\r\n'''
    files['verification/hub-native-build.json'] = encode(native)
    files['verification/hub-tests.log'] = read(EVIDENCE / 'all-tests-final.log')
    receipt = {'kind': 'neurotic-hub-review', 'hub_source_commit': commit,
               'hub_sha256': sha(hub), 'base_archive_sha256': args.base_sha256.lower(),
               'product_sha256': sha(files[prefix + 'payload/OptiScaler.dll']),
               'synthetic_verified': True, 'real_game_qualified': False,
               'files': rows(files)}
    files['ARCHIVE-MANIFEST.json'] = encode(receipt)
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for p, b in sorted(files.items()): archive.writestr('NeuRotic/' + p, b)
    with zipfile.ZipFile(destination) as archive:
        if archive.testzip() is not None: raise RuntimeError('Output archive verification failed')
        for p, b in files.items():
            if archive.read('NeuRotic/' + p) != b: raise RuntimeError('Output member verification failed')
    result = {'path': str(destination), 'bytes': destination.stat().st_size, 'sha256': sha(read(destination)),
              'members': len(files), 'hub_source_commit': commit, 'hub_sha256': sha(hub),
              'product_sha256': receipt['product_sha256']}
    (EVIDENCE / 'delivery.json').write_bytes(encode(result))
    print(json.dumps(result, indent=2))

if __name__ == '__main__': main()
