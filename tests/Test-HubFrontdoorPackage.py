"""Verify the actual delivered Hub layout and its one bound installer payload."""
import argparse
import hashlib
import json
import posixpath
import zipfile

parser = argparse.ArgumentParser()
parser.add_argument('--package', required=True)
args = parser.parse_args()
with zipfile.ZipFile(args.package) as archive:
    names = archive.namelist()
    assert archive.testzip() is None, 'ZIP CRC failed'
    assert len(names) == len({n.casefold() for n in names}), 'Duplicate member'
    roots = {n.split('/')[0] for n in names}
    assert len(roots) == 1
    prefix = roots.pop() + '/'
    files = {n[len(prefix):]: archive.read(n) for n in names}
    assert 'NeuRotic.Hub.exe' in files, 'Hub must be the top-level app entry point'
    assert 'Tools/Hub/NeuRotic.Hub.exe' not in files, 'Old nested Hub was retained'
    assert 'packages/current/NeuRotic-Setup.cmd' in files
    assert 'packages/current/NeuRotic-Uninstall.cmd' in files
    assert 'support/Collect-NR-Review.ps1' in files, 'Hub full diagnostic exporter missing'
    assert 'discovery/NeuRotic.Discovery.exe' in files
    assert [n for n in files if n.endswith('/payload/OptiScaler.dll')] == ['packages/current/payload/OptiScaler.dll'], 'One combined product payload required'
    registry = json.loads(files['support/Hub-Packages.json'])
    entry, = registry['packages']
    assert entry['id'] == 'flagship-approved'
    package = posixpath.normpath('support/' + entry['relativeRoot'])
    assert package == 'packages/current', 'Hub must resolve the delivered installer'
    sha = lambda raw: hashlib.sha256(raw).hexdigest()
    assert entry['manifestSha256'].lower() == sha(files[package+'/support/PACKAGE-MANIFEST.json'])
    for manifest_path, root in [('ARCHIVE-MANIFEST.json', ''), (package+'/support/PACKAGE-MANIFEST.json', package+'/')]:
        manifest = json.loads(files[manifest_path])
        for row in manifest['files']:
            name = row['path'].replace('\\', '/')
            assert ':' not in name and '..' not in name.split('/') and not name.startswith('/')
            raw = files[root+name]
            assert len(raw) == row['bytes'] and sha(raw) == row['sha256'], root+name
        if not root:
            assert {row['path'] for row in manifest['files']} | {manifest_path} == set(files)
    assert files['support/NeuRotic-Setup-Engine.ps1'] == files[package+'/support/NeuRotic-Setup-Engine.ps1'], 'Hub and direct installer owners differ'
    print('PASS top-level Hub, single merged payload, diagnostic exporter, discovery, registry binding, CRC and all manifest hashes')
