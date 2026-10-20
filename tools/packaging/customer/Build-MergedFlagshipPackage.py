"""Seal the verified merged binaries with the existing direct installer.

No build, download, game installation, or recursive build-tree copy occurs here.
The Character CPU dependencies are reused from a hash-verified delivered ZIP.
"""
import hashlib
import json
import os
import re
import subprocess
import sys
import zipfile
sys.path.insert(0, str(Path(__file__).parent))
from NativePackagePolicy import retain_native_shipping
from InspectorLayout import version_inspector, component as inspector_component
from pathlib import Path

ROOT = Path(__file__).resolve().parents[3]
EXPORTS = ROOT.parent / '_exports'
WORK = ROOT / 'builds/merged-package'
EVIDENCE = ROOT / 'docs/implementation-evidence/hub-frontdoor-20261003'
sys.path.insert(0, str(ROOT / 'tools/character-inspector'))
from package_checks import require_product_commit


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def encode(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def load_archive(name, expected):
    path = EXPORTS / name
    assert sha(path.read_bytes()) == expected, name
    with zipfile.ZipFile(path) as archive:
        assert archive.testzip() is None
        names = archive.namelist()
        assert len(names) == len(set(n.casefold() for n in names))
        prefix = names[0].split('/')[0] + '/'
        manifest_name = prefix + 'support/PACKAGE-MANIFEST.json'
        manifest = json.loads(archive.read(manifest_name))
        files = {}
        for row in manifest['files']:
            name = row['path'].replace('\\', '/')
            assert not name.startswith('/') and ':' not in name and '..' not in name.split('/')
            assert name not in files
            raw = archive.read(prefix + name)
            assert len(raw) == row['bytes'] and sha(raw) == row['sha256']
            files[name] = raw
        assert {prefix + n for n in files} | {manifest_name} == set(names)
    return files


def add_tree(files, folder, prefix):
    assert not folder.is_symlink() and not folder.is_junction()
    for directory, dirs, names in os.walk(folder, followlinks=False):
        for name in dirs + names:
            assert not (os.stat(Path(directory, name), follow_symlinks=False).st_file_attributes & 0x400)
        dirs[:] = [d for d in dirs if d not in ('__pycache__', 'obj', 'bin')]
        for name in names:
            path = Path(directory, name)
            if path.suffix.lower() in ('.pyc', '.obj', '.pch', '.ilk', '.pdb'):
                continue
            files[prefix + path.relative_to(folder).as_posix()] = path.read_bytes()


def rows(files, windows=False):
    return [{'path': n.replace('/', '\\') if windows else n,
             'bytes': len(raw), 'sha256': sha(raw)} for n, raw in sorted(files.items())]


def main():
    WORK.mkdir(parents=True, exist_ok=True)
    commit = git('rev-parse', 'HEAD')
    assert git('branch', '--show-current') == 'codex/flagship'
    assert not git('status', '--porcelain', '--', 'OptiScaler', 'integration', 'tools/nr_semantic_worker',
                   'apps/NeuRoticHub', 'apps/NeuRoticWindowWorker', 'tools/packaging/customer'), 'Commit shipping source before sealing'
    build = json.loads((EVIDENCE / 'product-build.json').read_bytes())
    assert build['exit_code'] == 0 and build['source_unchanged']
    assert git('rev-parse', 'HEAD:OptiScaler') == build['product_tree']
    product = ROOT / 'builds/ui-release/bin/OptiScaler.dll'
    version = require_product_commit(product, build['source_commit'][:8])
    for row in json.loads((EVIDENCE / 'built-artifacts.json').read_bytes()):
        raw = (ROOT / row['path']).read_bytes()
        assert len(raw) == row['bytes'] and sha(raw) == row['sha256'], row['path']

    files = load_archive('NeuRotic-Vulkan-Multipass-Setup-7d402eae.zip',
                         'c4969958aec5f2c83f5467243889b96ad531473cb5e81d652214224ea12d86da')
    old = json.loads(files['support/BUILD-MANIFEST.json'])
    character = load_archive('NeuRotic-CharacterInspector-Live-33de321f-33de321f.zip',
                             '2134e678109dc93577d57fd09447ba6d8a8fa691cd8284659e0ec81d2a4c9e12')
    worker_prefix = 'payload/OptiScaler/CharacterInspector/'
    for name, raw in character.items():
        if name.startswith(worker_prefix) or name == 'payload/Licenses/CharacterInspector-MIT.txt':
            files[name] = raw
    distribution_name = worker_prefix + 'DISTRIBUTION-MANIFEST.json'
    distribution = json.loads(files.pop(distribution_name))
    for row in distribution['files']:
        raw = files[worker_prefix + row['path']]
        assert len(raw) == row['bytes'] and sha(raw) == row['sha256']
    # Code/models have not changed since the sealed Character donor. Refuse stale
    # worker code rather than silently rebundling old code with a new game DLL.
    for name in git('ls-files', 'tools/nr_semantic_worker').splitlines():
        relative = name.removeprefix('tools/nr_semantic_worker/')
        if relative.startswith('evidence/') or relative == 'acquire_upstream.py':
            continue
        current = (ROOT / name).read_bytes()
        assert files[worker_prefix + relative].replace(b'\r\n', b'\n') == current.replace(b'\r\n', b'\n'), name
        files[worker_prefix + relative] = current
    distribution.update(source_commit=commit, dependency_archive_source='33de321f')
    distribution['files'] = rows({n[len(worker_prefix):]: raw for n, raw in files.items()
                                  if n.startswith(worker_prefix)})
    cohort_pin = re.search(r'InspectorCohortDigest\[\]="([0-9a-f]{64})"',
                          (ROOT / 'OptiScaler/nr/semantic/character/CharacterCohort.h').read_text()).group(1)
    assert sha(json.dumps(distribution['files'], sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()) == cohort_pin, 'Update Inspector host cohort pin and rebuild'
    files[distribution_name] = encode(distribution)

    customer = ROOT / 'tools/packaging/customer'
    for name in ('NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd'):
        files[name] = (customer / name).read_bytes()
    for name in ('NeuRotic-InstallReceipt.ps1', 'NeuRotic-LegacyReceipt.ps1', 'NeuRotic-Uninstall.ps1', 'NeuRotic-Game-Uninstall.cmd', 'NeuRotic-InstallProfiles.ps1', 'NeuRotic-InstallCore.ps1', 'NeuRotic-SimpleFileIo.cs', 'NeuRotic-GameCatalog.json', 'NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1',
                 'NeuRotic-HubProtocol.ps1', 'NeuRotic-HubSettings.ps1','NeuRotic-ObjectRules.ps1','object-rules-v1.schema.json', 'NeuRotic-HubJson.cs',
                 'NeuRotic-HubAntiCheat.ps1', 'neurotic_anticheat_rules.json', 'exe_names.txt',
                 'Start-CandidateCapture.ps1', 'Candidate-Launch.ps1'):
        files['support/' + name] = (customer / 'support' / name).read_bytes()
    files['support/Collect-Diagnostics.ps1'] = (customer / 'support/Collect-NR-Review.ps1').read_bytes().replace(
        b"'reviews'", b"'diagnostics'").replace(b'REVIEW_ZIP=', b'DIAGNOSTICS_ZIP=')
    files['payload/OptiScaler.dll'] = product.read_bytes()
    ini = (ROOT / 'integration/OptiScaler.ini').read_bytes()
    for section, key, value in [('Menu', 'OverlayMenu', 'true'), ('Menu', 'ShortcutKey', '45'),
                                ('Log', 'LogToFile', 'true'), ('Log', 'LogAsync', 'true'),
                                ('DlssNr', 'Enabled', 'false')]:
        match = re.search(rb'(?ms)^\[' + section.encode() + rb'\].*?(?=^\[|\Z)', ini)
        assert match
        fragment, count = re.subn(rb'(?m)^(' + key.encode() + rb'\s*=\s*)[^\r\n]*',
                                  lambda m: m[1] + value.encode(), match[0])
        assert count == 1
        ini = ini[:match.start()] + fragment + ini[match.end():]
    files['payload/OptiScaler.ini'] = ini
    files['READ ME.txt'] = ('''NEUROTIC MERGED FLAGSHIP REVIEW

Extract the COMPLETE ZIP outside your game folder. Close the game.
Run NeuRotic-Manual-Setup.cmd, select its executable and the proxy you normally use.
For an existing managed installation, choose Update. Saved settings are kept.
Launch the game and press Insert to open the merged interface.
Run Collect-Diagnostics.cmd after testing if you want a diagnostic ZIP.
To remove this build, close the game and run NeuRotic-Manual-Uninstall.cmd.

Includes the sleek interface/official branding, observation refresh, Character
Inspector, Passive Discovery, Hub fifth pass and the existing flagship Vulkan,
MFG, multipass, bridge, capability and experimental depth work.

Character Inspector is OFF on a fresh install. On supported D3D12 paths with
frame generation off: Neural Rendering > Character Inspector > Enable, then
Start live inspection. Its offline CPU runtime and models are included.
Box hold defaults to 500ms. Accuracy and gameplay performance remain unfinished.
See CHARACTER-INSPECTOR.txt for the donor's detailed operating limits.

Main launcher: ../../NeuRotic.Hub.exe. It installs THIS merged package.
Keep the complete extracted folder together so the launcher finds its payload.
Optional Window Worker: ../../Tools/WindowWorker/NeuRotic.WindowWorker.exe --control.
The worker remains a separate prototype; Hub-to-worker wiring and audit A1-A9
are open. See ../../Tools/WindowWorker/INTEGRATION.md. No private NR model is bundled.

This combined candidate passed local build and fixture checks. Combined-game
qualification remains open; earlier game acceptance does not transfer to it.

Standard installer guide:

''').replace('\n', '\r\n').encode() + (customer / 'READ ME.txt').read_bytes()
    files['START HERE.txt'] = files['READ ME.txt']
    files['CHARACTER-INSPECTOR.txt'] = character['READ ME.txt'].replace(
        b'   Keep the separate DOOM/Vulkan patching installation unchanged.\r\n', b'')
    files['MERGE-RECORD.md'] = (ROOT / 'docs/ai/live/FLAGSHIP_ALL_MERGES_2026-10-03.md').read_bytes()
    old.update(commit=commit, product_source_commit=build['source_commit'],
               product_sha256=sha(files['payload/OptiScaler.dll']), product_version=version,
               config_sha256=sha(ini), candidate='All merge-flagged builds combined review',
               exact_verification=build, game_runtime_verified=False, game_qualified=False,
               prior_acceptance_transfers_to_this_binary=False, branch='codex/flagship')
    files['support/BUILD-MANIFEST.json'] = encode(old)
    files['support/PRODUCT-BUILD-RECEIPT.json'] = encode(build)
    retain_native_shipping(files)
    version_inspector(files)
    files['support/PACKAGE-MANIFEST.json'] = encode({
        'kind': 'neurotic-customer-candidate', 'name': 'NeuRotic Merged Flagship Review',
        'version': '0.9.6', 'commit': commit, 'product_source_commit': build['source_commit'],
        'components': {'characterInspector': inspector_component()},
        'files': rows(files, windows=True)})
    installer_members = len(files)

    # The Hub points to the single nested installer. No old accepted payload and
    # no second copy of the large offline runtime is shipped.
    files = {'packages/current/' + name: raw for name, raw in files.items()}
    files['START HERE.txt'] = files['READ ME.txt'] = ('''NEUROTIC MERGED FLAGSHIP REVIEW

Extract the COMPLETE ZIP outside your game folder. Open NeuRotic.Hub.exe.
Choose a game in the library or add its executable. Use Installation to
install, update, repair or change proxy. Use Start game to launch through
Steam for Steam titles, or the selected executable for manually added games.
Press Insert in a running game to open the NeuRotic interface.

Game settings can be edited before launch. Full diagnostic ZIP export is on
each game's Diagnostics page. The quick preview remains available separately.
Game profiles, favorites and artwork persist in Local AppData across app
updates. Settings provides folder buttons for inspecting or clearing this data.
Fresh installation previews still inspect the selected game before changes.

Advanced direct installer, uninstall, diagnostics and paired capture tools:
packages/current/. Keep the extracted folder together: the Hub uses that one
combined payload. Existing game settings are retained by update and repair.

Character Inspector and NR are OFF on a fresh install. Character's offline
CPU runtime and models are included; gameplay accuracy/performance remain
unfinished. See packages/current/CHARACTER-INSPECTOR.txt for detailed limits.
The optional Tools/WindowWorker prototype remains standalone; A1-A9 and
Hub-to-worker integration remain open. No private NR model is bundled.

Combined-game qualification remains open. Local fixtures and preview checks
do not transfer historical game acceptance to this combined candidate.
''').replace('\n', '\r\n').encode()
    hub = ''
    files[hub + 'NeuRotic.Hub.exe'] = (ROOT / 'builds/hub/bin/NeuRotic.Hub.exe').read_bytes()
    files[hub + 'discovery/NeuRotic.Discovery.exe'] = (ROOT / 'builds/hub/bin/discovery/NeuRotic.Discovery.exe').read_bytes()
    support_names = ('NeuRotic-InstallReceipt.ps1', 'NeuRotic-LegacyReceipt.ps1', 'NeuRotic-Uninstall.ps1', 'NeuRotic-Game-Uninstall.cmd', 'NeuRotic-InstallProfiles.ps1', 'NeuRotic-InstallCore.ps1', 'NeuRotic-SimpleFileIo.cs', 'NeuRotic-GameCatalog.json', 'NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1',
                     'NeuRotic-HubProtocol.ps1', 'NeuRotic-HubSettings.ps1','NeuRotic-ObjectRules.ps1','object-rules-v1.schema.json', 'NeuRotic-HubJson.cs',
                     'NeuRotic-HubAntiCheat.ps1', 'neurotic_anticheat_rules.json', 'exe_names.txt',
                     'Collect-NR-Review.ps1')
    for name in support_names:
        files[hub + 'support/' + name] = (customer / 'support' / name).read_bytes()
    files[hub + 'support/Hub-Packages.json'] = encode({'schemaVersion': 1, 'packages': [{
        'id': 'flagship-approved', 'name': 'NeuRotic merged flagship review (game qualification pending)',
        'relativeRoot': '../packages/current', 'manifestSha256': sha(files['packages/current/support/PACKAGE-MANIFEST.json'])}]})
    for folder in ('models', 'streamline'):
        files[hub + folder + '/README.txt'] = (ROOT / 'apps/NeuRoticHub/optional' / folder / 'README.txt').read_bytes()
    files[hub + 'README.md'] = (ROOT / 'apps/NeuRoticHub/README.md').read_bytes()
    add_tree(files, ROOT / 'apps/NeuRoticHub/provenance', hub + 'provenance/')
    for name in ('LICENSE.TXT', 'THIRD-PARTY-NOTICES.TXT'):
        files[hub + 'licenses/dotnet-' + name] = (ROOT.parent / '_deps/hub/nuget/microsoft.netcore.app.runtime.win-x64/10.0.12' / name).read_bytes()
    files[hub + 'licenses/ImGui-LICENSE.txt'] = (ROOT / 'OptiScaler/include/imgui/LICENSE.txt').read_bytes()
    files[hub + 'LICENSE'] = (ROOT / 'LICENSE').read_bytes()
    worker = 'Tools/WindowWorker/'
    for name in ('NeuRotic.WindowWorker.exe', 'nvngx.dll_dlssnr.dll'):
        files[worker + name] = (ROOT / 'builds/window-worker/bin' / name).read_bytes()
    files[worker + 'INTEGRATION.md'] = (ROOT / 'apps/NeuRoticWindowWorker/INTEGRATION.md').read_bytes()
    files[worker + 'LICENSE'] = (ROOT / 'LICENSE').read_bytes()
    files[worker + 'RenoDX_ATTRIBUTION.txt'] = (ROOT / 'Licenses/RenoDX_ATTRIBUTION.txt').read_bytes()
    add_tree(files, ROOT / 'references/window-nr', worker + 'references/window-nr/')
    files['ARCHIVE-MANIFEST.json'] = encode({'source_commit': commit, 'files': rows(files)})
    destination = EXPORTS / ('NeuRotic-Flagship-All-Merges-' + commit[:8] + '-review.zip')
    with zipfile.ZipFile(destination, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, raw in sorted(files.items()):
            archive.writestr('NeuRotic/' + name, raw)
    with zipfile.ZipFile(destination) as archive:
        assert archive.testzip() is None
        manifest = json.loads(archive.read('NeuRotic/ARCHIVE-MANIFEST.json'))
        assert {r['path'] for r in manifest['files']} | {'ARCHIVE-MANIFEST.json'} == set(files)
        for row in manifest['files']:
            raw = archive.read('NeuRotic/' + row['path'])
            assert len(raw) == row['bytes'] and sha(raw) == row['sha256']
    receipt = {'path': str(destination), 'bytes': destination.stat().st_size,
               'sha256': sha(destination.read_bytes()), 'members': len(files),
               'installer_members': installer_members, 'all_member_hashes_verified': True,
               'source_commit': commit, 'product_source_commit': build['source_commit'],
               'product_sha256': sha(files['packages/current/payload/OptiScaler.dll']),
               'hub_package_manifest_sha256': sha(files['packages/current/support/PACKAGE-MANIFEST.json']),
               'character_offline_dependencies_bundled': True, 'hub_bundled': True,
               'window_worker_bundled': True, 'private_nr_model_bundled': False,
               'game_files_changed': False, 'game_qualified': False}
    (WORK / 'package-receipt.json').write_bytes(encode(receipt))
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
