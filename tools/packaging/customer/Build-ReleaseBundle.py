"""Seal current verified binaries with pinned offline dependencies; never build or download."""
import argparse
import importlib.util
import json
from pathlib import Path
import re
import subprocess
import sys
import zipfile
sys.path.insert(0, str(Path(__file__).parent))
from NativePackagePolicy import retain_native_shipping
from LoggingProfile import apply_logging_profile
from PublicPayload import public_payload

ROOT = Path(__file__).resolve().parents[3]
BASE = ROOT.parent / '_exports/NeuRotic-Current-Flagship-4134edeb.zip'
BASE_SHA = '2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67'
EVIDENCE = ROOT / 'docs/implementation-evidence/release-readiness-20261004'
WORK = ROOT / 'builds/release-bundle'
spec = importlib.util.spec_from_file_location('release_verify', Path(__file__).with_name('Verify-ReleaseBundle.py'))
verify = importlib.util.module_from_spec(spec)
spec.loader.exec_module(verify)
require, sha = verify.require, verify.sha
PACKAGE, WORKER, SUPPORT = verify.PACKAGE, verify.WORKER, verify.SUPPORT
LOCAL_WORKER = 'payload/' + verify.inspector_component()['relativeRoot'] + '/'
SCOPES = ('OptiScaler', 'integration', 'apps/NeuRoticHub', 'apps/NeuRoticWindowWorker',
          'tools/nr_semantic_worker', 'tools/packaging/customer', 'tools/window-worker',
          'addons', 'references/prepared-guide-installer',
          'Licenses', 'LICENSE', 'localization', 'tools/localization', 'references/window-nr/capture-donor/Magpie.LICENSE')
COMPONENT_SCOPES = ('apps/NeuRoticHub', 'apps/NeuRoticWindowWorker', 'OptiScaler', 'tools/window-worker', 'addons', 'localization')
COMPONENTS = {
    'NeuRotic.exe': 'builds/hub/bin/NeuRotic.exe',
    **{'discovery/' + n: 'builds/hub/bin/discovery/' + n for n in
       ('NeuRotic.Discovery.exe', 'NeuRotic.Discovery.dll', 'NeuRotic.Discovery.deps.json', 'NeuRotic.Discovery.runtimeconfig.json')},
    'Tools/WindowWorker/NeuRotic.WindowWorker.exe': 'builds/window-worker/bin/NeuRotic.WindowWorker.exe',
    'Tools/WindowWorker/nvngx.dll_dlssnr.dll': 'builds/window-worker/bin/nvngx.dll_dlssnr.dll',
    PACKAGE + 'payload/OptiScaler/NRAnything/NeuRotic.WindowWorker.exe': 'builds/window-worker/bin/NeuRotic.WindowWorker.exe',
    PACKAGE + 'payload/OptiScaler/NRAnything/nvngx.dll_dlssnr.dll': 'builds/window-worker/bin/nvngx.dll_dlssnr.dll',
    PACKAGE + 'payload/NeuRotic.Fsr3.Vulkan.dll': 'builds/native-framegen/bridge/NeuRotic.Fsr3.Vulkan.dll',
    PACKAGE + 'payload/WinPixEventRuntime.dll': 'builds/prepared-guides/WinPixEventRuntime.dll',
}

def universal_module():
    module_spec = importlib.util.spec_from_file_location('universal_package', Path(__file__).with_name('Build-UniversalConnectionsPackage.py'))
    module = importlib.util.module_from_spec(module_spec)
    module_spec.loader.exec_module(module)
    return module


def encode(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


def git(*args):
    return subprocess.check_output(['git', '-C', str(ROOT), *args], text=True).strip()


def rows(files, windows=False):
    return [{'path': n.replace('/', '\\') if windows else n, 'bytes': len(raw), 'sha256': sha(raw)}
            for n, raw in sorted(files.items())]


def clean_source():
    require(git('branch', '--show-current') == 'codex/flagship', 'Select the flagship branch')
    require(not git('status', '--porcelain', '--', *SCOPES), 'Commit shipping source before sealing')
    return git('rev-parse', 'HEAD')


def verify_built_file(row, expected=None):
    path = (ROOT / row['path']).resolve()
    require(path.is_relative_to((ROOT / 'builds').resolve()), 'Build artifact must be in this checkout builds directory')
    if expected:
        require(path == (ROOT / expected).resolve(), 'Unexpected artifact path: ' + str(path))
    raw = path.read_bytes()
    require(len(raw) == row['bytes'] and sha(raw) == row['sha256'], 'Stale/mismatched artifact: ' + str(path))
    return raw


def load_builds(build_path, components_path):
    build = json.loads(build_path.read_bytes())
    require(build['exit_code'] == 0 and build['source_unchanged'], 'Unsuccessful/changed product build')
    require(git('rev-parse', 'HEAD:OptiScaler') == build['product_tree'] ==
            git('rev-parse', build['source_commit'] + ':OptiScaler'), 'Product source differs from exact build')
    raw = verify_built_file(build['product'])
    sys.path.insert(0, str(ROOT / 'tools/character-inspector'))
    from package_checks import require_product_commit
    version = require_product_commit((ROOT / build['product']['path']).resolve(), build['source_commit'][:8])
    components = json.loads(components_path.read_bytes())
    require(components['exit_code'] == 0 and components['source_unchanged'], 'Unsuccessful/changed component build')
    for scope in COMPONENT_SCOPES:
        require(components['source_trees'][scope] == git('rev-parse', 'HEAD:' + scope) ==
                git('rev-parse', components['source_commit'] + ':' + scope), 'Component source changed: ' + scope)
    by_path = {}
    for row in components['files']:
        key = (ROOT / row['path']).resolve()
        require(key not in by_path, 'Duplicate component receipt path')
        by_path[key] = row
    files = {}
    for destination, path in COMPONENTS.items():
        key = (ROOT / path).resolve()
        require(key in by_path, 'Component missing from receipt: ' + path)
        files[destination] = verify_built_file(by_path[key], path)
    verify.verify_window_worker_pair(files['Tools/WindowWorker/NeuRotic.WindowWorker.exe'],
                                     files['Tools/WindowWorker/nvngx.dll_dlssnr.dll'])
    # The preserved .NET runtime must match the freshly rebuilt discovery app.
    runtime = json.loads(files['discovery/NeuRotic.Discovery.runtimeconfig.json'])['runtimeOptions']
    frameworks = runtime.get('includedFrameworks', runtime.get('frameworks', [runtime.get('framework', {})]))
    require(any(f.get('name') == 'Microsoft.NETCore.App' and f.get('version') == '10.0.12' for f in frameworks),
            'Current Discovery does not target the pinned .NET 10.0.12 runtime')
    return build, components, raw, version, files


def baseline_dependencies():
    require(sha(BASE.read_bytes()) == BASE_SHA, 'Baseline archive identity differs')
    selected, provenance = {}, []
    with zipfile.ZipFile(BASE) as archive:
        require(archive.testzip() is None, 'Baseline ZIP CRC failure')
        names = archive.namelist()
        require(len(names) == len({n.casefold() for n in names}), 'Duplicate baseline ZIP member')
        manifest = json.loads(archive.read('NeuRotic/ARCHIVE-MANIFEST.json'))
        manifest_rows = {r['path'].replace('\\', '/'): r for r in manifest['files']}
        donor_worker = WORKER if WORKER + 'DISTRIBUTION-MANIFEST.json' in manifest_rows else PACKAGE + 'payload/OptiScaler/CharacterInspector/'
        require(len(manifest_rows) == len(manifest['files']) and
                set(names) == {'NeuRotic/' + n for n in manifest_rows} | {'NeuRotic/ARCHIVE-MANIFEST.json'},
                'Baseline archive inventory differs')
        for full in names:
            verify.safe_name(full)
            name = full.removeprefix('NeuRotic/')
            keep = name.startswith(('licenses/', 'discovery/')) and name not in COMPONENTS and not name.endswith('.pdb')
            keep |= name.startswith(donor_worker + 'runtime/') or name.startswith(donor_worker + 'packages/') or name.startswith(donor_worker + 'models/')
            keep |= name.startswith(PACKAGE + 'payload/Licenses/')
            keep |= name in {PACKAGE + n for n in (
                      'payload/NeuRotic-LICENSE.txt', 'payload/nvngx.dll_dlssnr.dll', 'payload/OptiScaler/amd_fidelityfx_dx12.dll')}
            if not keep:
                continue
            require(not full.endswith('/') and not name.endswith(('.pyc', '.pdb')), 'Unwanted dependency artifact')
            raw = archive.read(full)
            row = manifest_rows[name]
            require(len(raw) == row['bytes'] and sha(raw) == row['sha256'], 'Baseline dependency hash differs: ' + name)
            destination = WORKER + name[len(donor_worker):] if name.startswith(donor_worker) else name
            selected[destination] = raw
            provenance.append({'path': destination, 'donorPath': name, 'bytes': len(raw), 'sha256': sha(raw)})
        oldbuild = json.loads(archive.read('NeuRotic/' + PACKAGE + 'support/BUILD-MANIFEST.json'))
        olddistribution = json.loads(archive.read('NeuRotic/' + donor_worker + 'DISTRIBUTION-MANIFEST.json'))
    return selected, provenance, oldbuild, olddistribution


def setting(ini, section, key, value):
    match = re.search(rb'(?ms)^\[' + section.encode() + rb'\][^\r\n]*\r?\n.*?(?=^\[|\Z)', ini)
    require(match is not None, 'INI section missing: ' + section)
    fragment, count = re.subn(rb'(?m)^(' + key.encode() + rb'\s*=\s*)[^\r\n]*',
                              lambda m: m[1] + value.encode(), match[0])
    require(count <= 1, 'Duplicate INI setting: ' + key)
    if not count:
        fragment = fragment.rstrip(b'\r\n') + b'\r\n' + key.encode() + b' = ' + value.encode() + b'\r\n\r\n'
    return ini[:match.start()] + fragment + ini[match.end():]


def text_file(value):
    return value.strip().replace('\n', '\r\n').encode('utf-8') + b'\r\n'


def manual_launchers():
    files = {}
    for name in ('NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd'):
        raw = (ROOT / 'tools/packaging/customer' / name).read_bytes()
        old = b'%~dp0support\\NeuRotic-Setup-Engine.ps1'
        require(raw.count(old) == 1, 'Manual launcher engine path changed: ' + name)
        files[name] = raw.replace(old, b'%~dp0packages\\current\\support\\NeuRotic-Setup-Engine.ps1')
    return files


README = '''NEUROTIC

Extract the complete ZIP outside your game folder. Open NeuRotic.exe.
Keep the packages, support, discovery and Tools folders with the App.

To install, select or add a game in the App, close the game, and open
Installation. Install preserves saved settings. Fresh Install
resets settings to the package defaults. Use Uninstall to remove a managed
installation. You can also use the two NeuRotic-Manual scripts.

Start the game and press Insert to open the in-game interface. Neural
rendering starts off; enable it when your model and rendering settings are
ready. NR Anything is available from the desktop App. If in-game setup
reports an incompatible executable, use NR Anything.

The neural-rendering model is supplied separately. The bundled forwarder
is not that model. Use the App's model setup with a compatible model you
supply. Streamline provider files may also be required for your selected
features. No model is downloaded automatically.

The offline Inspector includes its runtime and three models. It starts off
and can be enabled explicitly. Experimental hardware options also start off.

For a problem report, open Installation Library, select your game, and use
its Diagnostics tab to export a diagnostic ZIP.
Licenses and third-party notices are included with their components.
'''

CHANGES = '''NEUROTIC — RELEASE NOTES

- Improved rendering synchronization when NR is stopped or changed.
- Preserved rendering resources while submitted GPU work is pending.
- Corrected launch-failure status and frame-generation availability.
- Kept NR Anything in the desktop App's workflow.
- Expanded translations and corrected language changes in status text.
- Improved compact header and settings layouts.
- Fresh installs use normal logging defaults. Updating preserves saved settings.
- Corrected DLSS quality-change recovery in Monster Hunter Wilds.
- Kept unsaved settings when Save or Refresh finds outside changes.
- Prevented settings edits while a save or refresh is in progress.
- Made settings conflict recovery actions visible.
'''

INSTALL_CHECKLIST = '''NEUROTIC — INSTALLATION CHECKLIST

1. Extract the whole ZIP and open NeuRotic.exe.
2. Select or add your game. Close it before installation or removal.
3. Review the installation plan. Use Install to keep your settings.
   Use Fresh Install only when you want the package defaults.
4. Start the game and press Insert. Confirm the menu and your display mode.
5. Configure your compatible model before enabling neural rendering.
6. Check image quality, input and frame-generation behavior in your game.

To remove NeuRotic, close the game and use Uninstall or
NeuRotic-Manual-Uninstall.cmd. Keep your personal files and save backups.
For a problem, open Installation Library, select your game, and use its
Diagnostics tab to export a report.
'''

NOTICES = '''NEUROTIC — THIRD-PARTY NOTICES

NeuRotic/OptiScaler and the App: GPLv3; see LICENSE.
Dear ImGui and embedded STB code: see licenses/ImGui-LICENSE.txt.
nlohmann JSON, copyright 2013–2025 Niels Lohmann: see licenses/nlohmann-JSON-MIT.txt.
Hack font: see licenses/Hack-font-notices.txt. Segoe UI is supplied by Windows.
Noto Sans and Noto Sans CJK: see licenses/OFL-NotoSans.txt,
licenses/OFL-NotoSansCJK.txt and licenses/Language-fonts-NOTICES.md.
The interface theme preserves Moonlight / Madam-Herta attribution.
Official NeuRotic branding accompanies the application.
Microsoft .NET 10.0.12: license and third-party notices are in discovery/ and licenses/.

The Anything capture implementation derives from Blinue/Magpie, revision
69d6105d56a74d9160716adb4359c3f80909f971, https://github.com/Blinue/Magpie.
See Tools/WindowWorker/Magpie-LICENSE.txt and Tools/WindowWorker/LICENSE.
Spatial super resolution uses AMD FidelityFX FSR1 EASU under the MIT license;
see Tools/WindowWorker/LICENSE-FSR1.txt.
Neural-rendering color composition credits RenoDX by Carlos Lopez Jr. /
clshortfuse; see Tools/WindowWorker/RenoDX_ATTRIBUTION.txt and payload notices.

The offline Inspector uses Python, NumPy, OpenCV and OpenCV Zoo. Their licenses
remain beside their runtime/package/source files under
packages/current/payload/OptiScaler/CharacterInspector/Version 1/. The lock files identify
the original sources and model hashes. Product component licenses, including
experimental RTX 20/30 and RTX 40 unlock notices, are in
packages/current/payload/Licenses/.
'''


def public_documents():
    """Use current customer text for full builds and retained-binary reseals."""
    return {'README.txt': text_file(README)}


def assemble(build, components, product, version, current, commit, reshade_setup=None, *, internal_testing=False):
    files, dependencies, oldbuild, olddistribution = baseline_dependencies()
    files.update(current)
    files[PACKAGE + 'payload/OptiScaler.dll'] = product
    ini = (ROOT / 'integration/OptiScaler.ini').read_bytes()
    for section, key, value in [('Menu', 'OverlayMenu', 'true'), ('Menu', 'ShortcutKey', '45'),
                               ('Log', 'LogAsync', 'true'),
                               ('DlssNr', 'Enabled', 'false'), ('CharacterInspector', 'Enabled', 'false'),
                               ('DLSSG', 'ExperimentalUnlockRTX30', 'false'), ('DLSSG', 'ExperimentalUnlockRTX20', 'false')]:
        ini = setting(ini, section, key, value)
    logging_profile = 'internal-testing' if internal_testing else 'public'
    ini = apply_logging_profile(ini, logging_profile)
    files[PACKAGE + 'payload/OptiScaler.ini'] = ini
    universal = universal_module()
    for name in SUPPORT:
        raw = (ROOT / 'tools/packaging/customer/support' / name).read_bytes()
        files['support/' + name] = files[PACKAGE + 'support/' + name] = raw
    # One root pair opens the same verified package engine as the App.
    files.update(manual_launchers())
    # Development capture tools stay in explicitly requested internal packages.
    if internal_testing:
        for name in ('Start-DRED-Capture.cmd', 'support/Start-DredCapture.ps1'):
            files[name] = (ROOT / 'tools/packaging/customer' / name).read_bytes()
    for name in ('RenoDX_ATTRIBUTION.txt', 'RTX40MFG-Unlock-MIT.txt', 'MFGAmpereUnlock-MIT.txt'):
        files[PACKAGE + 'payload/Licenses/' + name] = (ROOT / 'Licenses' / name).read_bytes()
    files[PACKAGE + 'payload/Licenses/THIRD_PARTY_MFGAdaUnlock_LICENSE.txt'] = (ROOT / 'OptiScaler/mfg/THIRD_PARTY_MFGAdaUnlock_LICENSE').read_bytes()
    for name in git('ls-files', 'tools/nr_semantic_worker').splitlines():
        relative = name.removeprefix('tools/nr_semantic_worker/')
        if relative.startswith('evidence/') or relative in ('acquire_upstream.py', 'README.md'):
            continue
        require(not relative.endswith(('.onnx', '.pyc')), 'Unexpected tracked worker binary')
        files[WORKER + relative] = (ROOT / name).read_bytes()
    files[WORKER + 'README.txt'] = text_file('Offline Inspector runtime. Start Inspector explicitly from NeuRotic.\n'
        'This distribution uses protocol schema 3. Models are bundled and pinned in\nmodels.lock.json. '
        'Preserve the licenses in runtime/, packages/ and third_party/.')
    models = json.loads(files[WORKER + 'models.lock.json'])['models']
    require(len(models) == 3, 'Expected three Inspector models')
    for model in models:
        raw = files[WORKER + model['destination']]
        require(sha(raw) == model['sha256'] and len(raw) == model['bytes'], 'Pinned model missing/mismatched')
    upstream = json.loads(files[WORKER + 'upstream.lock.json'])
    for row in upstream['sources']:
        raw = files[WORKER + row['destination']]
        # Git may have materialized CRLF on Windows; source files remain semantically
        # identical while the distribution hashes always describe the actual bytes.
        require(sha(raw) == row['sha256'] or sha(raw.replace(b'\r\n', b'\n')) == row['sha256'],
                'Upstream source identity differs: ' + row['destination'])
    distribution = {k: olddistribution[k] for k in ('schema', 'python', 'numpy', 'opencv')}
    distribution.update(source_commit=commit, private_protocol_schema=3, models=models,
                        network_required_at_runtime=False, packaged_models=True, dependency_archive_sha256=BASE_SHA)
    distribution['files'] = rows({n[len(WORKER):]: b for n, b in files.items() if n.startswith(WORKER)})
    cohort_pin = re.search(r'InspectorCohortDigest\[\]="([0-9a-f]{64})"',
                          (ROOT / 'OptiScaler/nr/semantic/character/CharacterCohort.h').read_text()).group(1)
    require(sha(json.dumps(distribution['files'], sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()) == cohort_pin,
            'Inspector inventory changed: update the host cohort pin and rebuild before sealing')
    files[WORKER + 'DISTRIBUTION-MANIFEST.json'] = encode(distribution)
    files['LICENSE'] = files['Tools/WindowWorker/LICENSE'] = (ROOT / 'LICENSE').read_bytes()
    files['Tools/WindowWorker/RenoDX_ATTRIBUTION.txt'] = (ROOT / 'Licenses/RenoDX_ATTRIBUTION.txt').read_bytes()
    files['Tools/WindowWorker/Magpie-LICENSE.txt'] = (ROOT / 'references/window-nr/capture-donor/Magpie.LICENSE').read_bytes()
    files['Tools/WindowWorker/LICENSE-FSR1.txt'] = (ROOT / 'apps/NeuRoticWindowWorker/sr/LICENSE-FSR1.txt').read_bytes()
    files['licenses/ImGui-LICENSE.txt'] = (ROOT / 'OptiScaler/include/imgui/LICENSE.txt').read_bytes()
    for font in ('NotoSans', 'NotoSansCJK'):
        files['licenses/OFL-' + font + '.txt'] = (ROOT / 'localization/fonts' / ('OFL-' + font + '.txt')).read_bytes()
    files['licenses/Language-fonts-NOTICES.md'] = (ROOT / 'localization/fonts/NOTICES.md').read_bytes()
    for name in ('SOURCE.json','SOURCE-NotoSans.json'):
        files['licenses/' + name] = (ROOT / 'localization/fonts' / name).read_bytes()
    files['licenses/Hack-font-notices.txt'] = (ROOT / 'apps/NeuRoticHub/provenance/Hack-font-notices.txt').read_bytes()
    mit = (ROOT / 'Licenses/MFGAmpereUnlock-MIT.txt').read_text(encoding='utf-8')
    files['licenses/nlohmann-JSON-MIT.txt'] = text_file('MIT License\n\nCopyright (c) 2013-2025 Niels Lohmann\n\n' +
        mit[mit.index('Permission is hereby granted'):])
    # The installed fallback owns a dedicated copy of the receipt-verified desktop
    # pair. Never reuse the game-root forwarder, which has a separate build pin.
    for name in ('LICENSE', 'RenoDX_ATTRIBUTION.txt', 'Magpie-LICENSE.txt', 'LICENSE-FSR1.txt'):
        files[PACKAGE + 'payload/OptiScaler/NRAnything/' + name] = files['Tools/WindowWorker/' + name]
    files[PACKAGE + 'payload/OptiScaler/NRAnything/nlohmann-JSON-MIT.txt'] = files['licenses/nlohmann-JSON-MIT.txt']
    verify.verify_ingame_window_worker(files)
    files.update(public_documents())
    files['THIRD-PARTY-NOTICES.txt'] = text_file(NOTICES)
    files[PACKAGE + 'README.txt'] = text_file('Use NeuRotic-Manual-Setup.cmd in the release root for direct installation.\n'
        'The App, full instructions and change notes are two folders above.\n'
        'Keep the entire extracted release together. Close the game before changes.')
    provenance = {
        'product': {'source_commit': build['source_commit'], 'source_tree': build['product_tree'], 'sha256': sha(product)},
        'app_and_worker': {'source_commit': components['source_commit'], 'source_trees': components['source_trees'], 'files': rows(current)},
        'inspector': {'source_commit': commit, 'product_source_commit': build['source_commit'],
                      'product_sha256': sha(product), 'private_protocol_schema': 3, 'game_qualified': False},
        'dependencies': {'archive_sha256': BASE_SHA, 'files': [row for row in dependencies
                         if sha(files[row['path']]) == row['sha256']]},
    }
    retain_native_shipping(files)
    metadata = {k: oldbuild[k] for k in ('status', 'version', 'architecture', 'runtime_prerequisites')}
    metadata.update(commit=commit, package_source_commit=commit, product_source_commit=build['source_commit'],
                    build={'exit_code': 0}, product_sha256=sha(product), product_version=version, config_sha256=sha(ini), loggingProfile=logging_profile,
                    forwarder_sha256=sha(files[PACKAGE + 'payload/nvngx.dll_dlssnr.dll']),
                    sdk_sha256=sha(files[PACKAGE + 'payload/OptiScaler/amd_fidelityfx_dx12.dll']),
                    provider_model_bundled=False, streamline_provider_bundled=False, game_qualified=False,
                    game_runtime_verified=False, prior_acceptance_transfers_to_this_binary=False, base_archive_sha256=BASE_SHA)
    files[PACKAGE + 'support/BUILD-MANIFEST.json'] = encode(metadata)
    files[PACKAGE + 'support/PACKAGE-MANIFEST.json'] = encode({
        'kind': 'neurotic-customer-candidate', 'name': 'NeuRotic Internal Test' if internal_testing else 'NeuRotic Release Candidate', 'version': '0.9.8', 'loggingProfile': logging_profile,
        'commit': commit, 'product_source_commit': build['source_commit'], 'component_provenance': {'inspector': provenance['inspector']},
        'components': {'characterInspector': verify.inspector_component()},
        'files': rows({n[len(PACKAGE):]: raw for n, raw in files.items() if n.startswith(PACKAGE)}, windows=True)})
    universal.refresh_hub_packages(files)
    # Local absolute paths and historical evidence receipts never enter this provenance.
    files['COMPONENTS.json'] = encode(provenance)
    files['ARCHIVE-MANIFEST.json'] = encode({'source_commit': commit, 'files': rows(files)})
    return public_payload(files, public_documents()) if not internal_testing else (files, None)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-receipt', type=Path, required=True)
    parser.add_argument('--components-receipt', type=Path, required=True)
    parser.add_argument('--output', type=Path, help='Explicit distribution ZIP destination; omit for local verification')
    parser.add_argument('--verify-only', action='store_true', help='Verify complete in-memory assembly without making a ZIP')
    parser.add_argument('--reshade-setup', type=Path, help='Deprecated compatibility argument; ReShade is not shipped')
    parser.add_argument('--internal-testing', action='store_true', help='Internal Josh/Codex test defaults: Trace to file; public logging is off by default')
    args = parser.parse_args()
    require(args.verify_only != bool(args.output), 'Choose local --verify-only or an explicit --output ZIP')
    commit = clean_source()
    build, components, raw, version, current = load_builds(args.build_receipt, args.components_receipt)
    files, verification_evidence = assemble(build, components, raw, version, current, commit, args.reshade_setup, internal_testing=args.internal_testing)
    details = verify.verify_release(files, verification_evidence=verification_evidence)
    require(clean_source() == commit, 'Source changed while assembling')
    # Recheck exact inputs after assembly; source cleanliness does not prove that
    # another build did not replace the binaries during compression preparation.
    load_builds(args.build_receipt, args.components_receipt)
    if args.verify_only:
        receipt = {**details, 'source_commit': commit, 'archive_created': False, 'game_files_changed': False, 'game_qualified': False, 'files': rows(files)}
        WORK.mkdir(parents=True, exist_ok=True)
        (WORK / 'verified-assembly.json').write_bytes(encode(receipt))
        if verification_evidence:
            (WORK / 'public-verification-evidence.json').write_bytes(encode(verification_evidence))
        print(json.dumps({k: v for k, v in receipt.items() if k != 'files'}, indent=2))
        return
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(args.output, 'x', zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
        for name, data in sorted(files.items()):
            archive.writestr('NeuRotic/' + name, data)
    sealed = verify.verify_release(verify.read_archive(args.output), verification_evidence=verification_evidence)
    require(clean_source() == commit, 'Source changed while sealing')
    receipt = {'path': str(args.output.resolve()), 'bytes': args.output.stat().st_size, 'sha256': sha(args.output.read_bytes()),
               **sealed, 'base_archive_sha256': BASE_SHA, 'game_files_changed': False,
               'game_qualified': False, 'private_nr_model_bundled': False}
    WORK.mkdir(parents=True, exist_ok=True)
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (WORK / 'package-receipt.json').write_bytes(encode(receipt))
    (EVIDENCE / 'release-package-receipt.json').write_bytes(encode(receipt))
    if verification_evidence:
        (WORK / 'public-verification-evidence.json').write_bytes(encode(verification_evidence))
        (EVIDENCE / 'public-verification-evidence.json').write_bytes(encode(verification_evidence))
    print(json.dumps(receipt, indent=2))


if __name__ == '__main__':
    main()
