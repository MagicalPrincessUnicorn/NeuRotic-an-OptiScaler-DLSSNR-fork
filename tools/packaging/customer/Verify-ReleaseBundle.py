"""Verify sealed customer content, then optionally run an inert installer lifecycle.

No game is launched. The optional fixture is removed after its receipt is saved.
"""
import argparse
import configparser
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import shutil
import stat
import subprocess
import sys
import unittest
import zipfile
sys.path.insert(0, str(Path(__file__).parent))
from NativePackagePolicy import require_native_shipping
from LoggingProfile import verify_logging_profile
from InspectorLayout import component as inspector_component
from LanguageResources import resource as pe_resource
from PublicPayload import REMOVED, ROOT_FILES, public_dataset, encode as public_encode, pick

ROOT = Path(__file__).resolve().parents[3]
WORK = ROOT / 'builds/release-bundle'
EVIDENCE = ROOT / 'docs/implementation-evidence/release-readiness-20261004'
PACKAGE = 'packages/current/'
WORKER = PACKAGE + 'payload/' + inspector_component()['relativeRoot'] + '/'
SUPPORT = (
    'NeuRotic-InstallReceipt.ps1', 'NeuRotic-LegacyReceipt.ps1', 'NeuRotic-Uninstall.ps1', 'NeuRotic-Game-Uninstall.cmd', 'NeuRotic-InstallDefaults.ps1', 'NeuRotic-InstallProfiles.ps1', 'NeuRotic-RenderingRequirements.json', 'NeuRotic-InstallCore.ps1',
    'NeuRotic-SimpleFileIo.cs', 'NeuRotic-GameCatalog.json',
    'NeuRotic-Setup-Engine.ps1', 'NeuRotic-Prerequisites.ps1', 'NeuRotic-RuntimeFiles.ps1', 'NeuRotic-HubProtocol.ps1', 'NeuRotic-HubSettings.ps1', 'NeuRotic-ObjectRules.ps1', 'object-rules-v1.schema.json', 'NeuRotic-HubJson.cs',
    'NeuRotic-HubAntiCheat.ps1', 'NeuRotic-AntiCheatCollector.cs',
    'NeuRotic-AntiCheatPolicy.ps1', 'NeuRotic-AntiCheatProviders.json',
    'neurotic_anticheat_rules.json', 'exe_names.txt', 'Collect-NR-Review.ps1')


def require(condition, message):
    if not condition:
        raise ValueError(message)


def sha(raw):
    return hashlib.sha256(raw).hexdigest()


def verify_window_worker_pair(worker, forwarder):
    """Check the actual compiled pin, not a mutable build header or receipt."""
    digest = sha(forwarder)
    require(worker.startswith(b'MZ') and forwarder.startswith(b'MZ'), 'Window Worker pair must contain PE binaries')
    require(b'\0' + digest.encode('ascii') + b'\0' in worker,
            'Window Worker forwarder differs from its compiled pin; rebuild/package the executable and forwarder together')
    return digest


def verify_ingame_window_worker(files):
    root = PACKAGE + 'payload/OptiScaler/NRAnything/'
    binaries = ('NeuRotic.WindowWorker.exe', 'nvngx.dll_dlssnr.dll')
    notices = ('LICENSE', 'RenoDX_ATTRIBUTION.txt', 'Magpie-LICENSE.txt', 'nlohmann-JSON-MIT.txt')
    # New assemblies also carry the FSR1 attribution with retained older workers.
    # It is optional for historical packages, but any included copy must match
    # the desktop notice; the exact inventory still rejects all other additions.
    if root + 'LICENSE-FSR1.txt' in files:
        notices += ('LICENSE-FSR1.txt',)
    require({name[len(root):] for name in files if name.startswith(root)} == set(binaries + notices),
            'Dedicated in-game NR Anything payload is incomplete or contains unexpected files')
    for name in binaries + notices:
        source = 'licenses/' + name if name == 'nlohmann-JSON-MIT.txt' else 'Tools/WindowWorker/' + name
        require(source in files and files[root + name] == files[source],
                'In-game NR Anything differs from verified desktop component: ' + name)
    return verify_window_worker_pair(files[root + binaries[0]], files[root + binaries[1]])


def safe_name(name):
    require(isinstance(name, str) and name and '\\' not in name and ':' not in name
            and '\x00' not in name and not name.startswith('/'), 'Unsafe archive path: ' + repr(name))
    parts = name.split('/')
    require(all(p and p not in ('.', '..') and p == p.rstrip(' .') for p in parts),
            'Unsafe archive path: ' + name)
    require(not any(re.fullmatch(r'(?i)(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\..*)?', p)
                    for p in parts), 'Windows device path: ' + name)
    return name


def verify_inventory(files, manifest_name, prefix='', *, manifest=None):
    external = manifest is not None
    if not external:
        manifest = json.loads(files[manifest_name])
    names = set()
    for row in manifest['files']:
        name = safe_name(row['path'].replace('\\', '/'))
        require(name.casefold() not in names, 'Duplicate manifest member: ' + name)
        names.add(name.casefold())
        full = prefix + name
        require(full in files and len(files[full]) == row['bytes'] and sha(files[full]) == row['sha256'],
                'Manifest content mismatch: ' + full)
    expected = {n[len(prefix):].casefold() for n in files if n.startswith(prefix) and (external or n != manifest_name)}
    require(names == expected, 'Manifest does not cover exact inventory: ' + manifest_name)
    return manifest


def read_archive(path):
    with zipfile.ZipFile(path) as archive:
        require(archive.testzip() is None, 'ZIP CRC failure')
        names = archive.namelist()
        require(len(names) == len({n.casefold() for n in names}), 'Duplicate ZIP member')
        files = {}
        for item in archive.infolist():
            name = safe_name(item.filename)
            require(name.startswith('NeuRotic/'), 'Unexpected archive root')
            require(not item.is_dir() and not stat.S_ISLNK(item.external_attr >> 16), 'Non-file archive member')
            files[name.removeprefix('NeuRotic/')] = archive.read(item)
    return files


DEVELOPMENT_ONLY_MEMBERS = frozenset({
    'start-dred-capture.cmd', 'support/start-dredcapture.ps1',
    'test-build.txt', 'validation-checklist.md', 'passive-discovery-testing.txt',
    'known-issues.txt', 'known-limits.txt',
}) | frozenset(name.casefold() for name in REMOVED)


def scan_content(files, *, internal_testing=False):
    require_native_shipping(files)
    binary_path_findings = []
    local_path = re.compile(rb'(?i)(?:[a-z]:[\\/]+(?:Users|NeuRotic|agent|a[\\/]+_work)[\\/]+|/Users/|/home/[^/]+/)')
    for name, raw in files.items():
        safe_name(name)
        require(internal_testing or name.casefold() not in DEVELOPMENT_ONLY_MEMBERS,
                'Development-only content in public package: ' + name)
        parts = {p.casefold() for p in name.split('/')}
        require(not parts.intersection({'source', 'verification', 'evidence', 'research', '__pycache__', '.git'}),
                'Internal directory shipped: ' + name)
        require(Path(name).suffix.lower() not in ('.pdb', '.obj', '.pch', '.ilk', '.pyc'),
                'Build intermediate shipped: ' + name)
        require(not any(word in name.casefold() for word in ('merge-record', 'integration.md', 'candidate_observer',
                'candidate-launch', 'candidatecapture', 'start-candidate', 'product-build-receipt', 'local-inputs', 'donor-import-map')),
                'Internal artifact shipped: ' + name)
        require(Path(name).name.casefold() != 'nvngx_dlssnr.dll', 'Private NR model must not ship')
        if raw.startswith(b'MZ'):
            if local_path.search(raw) or local_path.search(raw.replace(b'\x00', b'')):
                binary_path_findings.append(name)
            continue
        # Exact vendor dependency files retain their notices/code. Their original
        # documentation may contain generic examples; authored files must be clean.
        vendor = name.startswith((WORKER + 'runtime/', WORKER + 'packages/', WORKER + 'third_party/', 'discovery/'))
        if not vendor and Path(name).suffix.lower() in ('.json', '.txt', '.md', '.ps1', '.cmd', '.py', '.cs', '.ini', ''):
            require(not local_path.search(raw), 'Local machine path shipped: ' + name)
            if Path(name).suffix.lower() in ('.json', '.txt', '.md'):
                require(not re.search(rb'(?i)(docs/implementation-evidence|references/window-nr|local-inputs\.json|donor-import-map\.json|\bA1.A9\b)', raw),
                        'Internal documentation reference shipped: ' + name)
                require(internal_testing or not re.search(
                    rb'(?i)(\bNR-WO-\d+\b|docs[/\\]ai[/\\]live|\bWORK_LEDGER\b|'
                    rb'\bQA_(?:FINAL|FIRST|SOURCE)_[A-Z0-9_]+\b|'
                    rb'\b(?:source freeze|source pins|work ledger|developer notes|internal (?:test|review) notes)\b|'
                    rb'\bstill needs in-game verification\b)', raw),
                    'Internal work note shipped in public documentation: ' + name)
                require(internal_testing or not re.search(rb'(?i)\bKNOWN-(?:ISSUES|LIMITS)\.txt\b', raw),
                        'Standalone issue-file reference shipped in public documentation: ' + name)
    return binary_path_findings


def verify_layout(files):
    for name in ('NeuRotic.exe', 'NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd'):
        require(name in files, 'Release entry point missing: ' + name)
    for name in files:
        lower = name.casefold()
        require(lower not in ('neurotic.hub.exe', 'neurotic-setup.cmd', 'neurotic-uninstall.cmd'),
                'Superseded release entry point: ' + name)
        require(not lower.startswith(('models/', 'streamline/')), 'Legacy runtime placeholder shipped: ' + name)
        require(not (lower.startswith(PACKAGE) and Path(lower).name in
                ('neurotic-setup.cmd', 'neurotic-uninstall.cmd', 'neurotic-manual-setup.cmd', 'neurotic-manual-uninstall.cmd')),
                'Redundant nested launcher: ' + name)


def verify_public_projection(files, evidence):
    require(evidence.get('schema') == 1, 'Unsupported public verification evidence')
    require({name for name in files if '/' not in name} == ROOT_FILES, 'Public root must contain only customer entry points and notices')
    require(not set(files).intersection(REMOVED), 'Build evidence or redundant documentation shipped')
    distribution = json.loads(files[WORKER + 'DISTRIBUTION-MANIFEST.json'])
    require(distribution == {'files': evidence['distribution']['files']}, 'Inspector public metadata differs from the preserved cohort')
    cohort_pin = re.search(r'InspectorCohortDigest\[\]="([0-9a-f]{64})"',
                          (ROOT / 'OptiScaler/nr/semantic/character/CharacterCohort.h').read_text()).group(1)
    require(sha(json.dumps(distribution['files'], sort_keys=True, separators=(',', ':'), ensure_ascii=False).encode()) == cohort_pin,
            'Inspector cohort differs from the host pin')
    package = json.loads(files[PACKAGE + 'support/PACKAGE-MANIFEST.json'])
    expected = pick(evidence['package'], ('kind', 'version', 'loggingProfile', 'components'))
    expected.update(name='NeuRotic', files=package['files'])
    require(package == expected, 'Public package contains unused provenance or changed runtime metadata')
    for prefix in ('support/', PACKAGE + 'support/'):
        name = prefix + 'NeuRotic-AntiCheatProviders.json'
        expected = {**evidence['warning_data'][name], 'notes': []}
        require(files[name] == public_encode(expected), 'Provider runtime fields changed or research notes remain')
        script = prefix + 'NeuRotic-HubAntiCheat.ps1'
        original = (ROOT / 'tools/packaging/customer/support/NeuRotic-HubAntiCheat.ps1').read_bytes()
        original_data = (ROOT / 'tools/packaging/customer/support/NeuRotic-AntiCheatProviders.json').read_bytes()
        require(original.count(sha(original_data).encode('ascii')) == 1, 'Source provider integrity guard differs')
        require(files[script] == original.replace(sha(original_data).encode('ascii'), sha(files[name]).encode('ascii')),
                'Provider script changed beyond its strict data digest')
        name = prefix + 'neurotic_anticheat_rules.json'
        require(files[name] == public_encode(public_dataset(evidence['warning_data'][name])),
                'Anti-cheat active warning fields changed or research notes remain')


def verify_release(files, *, verification_evidence=None):
    verify_layout(files)
    for filename, identifier in [('NotoSansCJKsc-Regular.otf',5102),('NotoSans-Regular.ttf',5103),('OFL-NotoSansCJK.txt',5104),('OFL-NotoSans.txt',5105)]:
        expected=(ROOT/'localization/fonts'/filename).read_bytes()
        for binary in ('NeuRotic.exe',PACKAGE+'payload/OptiScaler.dll'):
            require(pe_resource(files[binary],identifier)==expected,'Embedded font/license differs: '+binary+'/'+filename)
        if filename.startswith('OFL-'):
            require(files.get('licenses/'+filename)==expected,'Redistribution license missing or changed: '+filename)
    if verification_evidence is not None:
        verify_public_projection(files, verification_evidence)
        outer = verify_inventory(files, 'external public inventory', manifest=verification_evidence['inventory'])
        original_package = verification_evidence['package']
    else:
        outer = verify_inventory(files, 'ARCHIVE-MANIFEST.json')
        original_package = json.loads(files[PACKAGE + 'support/PACKAGE-MANIFEST.json'])
    package = verify_inventory(files, PACKAGE + 'support/PACKAGE-MANIFEST.json', PACKAGE)
    require(package.get('components', {}).get('characterInspector') == inspector_component(),
            'Package Inspector version differs from the host installation layout')
    distribution = verify_inventory(files, WORKER + 'DISTRIBUTION-MANIFEST.json', WORKER)
    internal_testing = package.get('loggingProfile', 'public') == 'internal-testing'
    findings = scan_content(files, internal_testing=internal_testing)
    components = verification_evidence['components'] if verification_evidence is not None else json.loads(files['COMPONENTS.json'])
    dependency_names = set()
    for row in components['dependencies']['files']:
        name = safe_name(row['path'])
        require(name not in dependency_names and name in files and sha(files[name]) == row['sha256'] and
                len(files[name]) == row['bytes'], 'Inherited dependency differs: ' + name)
        dependency_names.add(name)
    require(components['dependencies']['archive_sha256'] ==
            '2d7618859cccb628b93da9475118440083fe297be24a775b86795a2e1feaae67', 'Dependency archive differs')
    for row in components['app_and_worker']['files']:
        name = safe_name(row['path'])
        require(name in files and sha(files[name]) == row['sha256'] and len(files[name]) == row['bytes'],
                'App/worker provenance differs: ' + name)
    owned_findings = [name for name in findings if name not in dependency_names]
    owned_codeview_paths = []
    for name, raw in files.items():
        if not raw.startswith(b'MZ') or name in dependency_names:
            continue
        require(not re.search(rb'(?i)Josh[ _]Parke', raw.replace(b'\x00', b'')), 'Personal name remains in current binary: ' + name)
        # Inspect the PDB reference separately from legitimate __FILE__ diagnostic
        # strings. Do not edit signed/sealed binaries or remove diagnostic code.
        start = 0
        while (start := raw.find(b'RSDS', start)) >= 0:
            end = raw.find(b'\x00', start + 24)
            path = raw[start + 24:end] if end >= 0 else b''
            if path.lower().endswith(b'.pdb'):
                # .NET's published apphost inherits this Microsoft build path. It
                # matches the hash-pinned baseline and SDK 10.0.12 template;
                # it is not a path from the NeuRotic build machine.
                vendor_apphost = name == 'discovery/NeuRotic.Discovery.exe' and path == (
                    br'D:\a\_work\1\s\src\runtime\artifacts\obj\win-x64.Release\corehost\apphost\standalone\apphost.pdb')
                # Unmodified Microsoft PIX dependency from the pinned FidelityFX
                # SDK retains its vendor build reference, never our machine path.
                vendor_pix = Path(name).name == 'WinPixEventRuntime.dll' and sha(raw) == '81adcfd8253c3489be720da7e30f16004dc9a1f02a8b418c6c3aef4993032e6d'
                require(vendor_apphost or vendor_pix or not re.search(rb'(?i)([a-z]:[\\/]|Users[\\/])', path),
                        'Absolute PDB path in current binary: ' + name)
                owned_codeview_paths.append({'path': name, 'pdb': path.decode('utf-8', errors='replace'), 'vendor_apphost': vendor_apphost})
            start += 4
    for name in ('NeuRotic-Manual-Setup.cmd', 'NeuRotic-Manual-Uninstall.cmd'):
        template = (ROOT / 'tools/packaging/customer' / name).read_bytes()
        expected = template.replace(b'%~dp0support\\NeuRotic-Setup-Engine.ps1',
                                    b'%~dp0packages\\current\\support\\NeuRotic-Setup-Engine.ps1')
        require(files[name] == expected, 'Manual launcher differs: ' + name)
    for name in SUPPORT:
        require(files['support/' + name] == files[PACKAGE + 'support/' + name], 'Support copies differ: ' + name)
    for name in ('Start-DRED-Capture.cmd', 'support/Start-DredCapture.ps1'):
        if internal_testing:
            require(files.get(name) == (ROOT / 'tools/packaging/customer' / name).read_bytes(),
                    'Internal diagnostic launcher missing or differs: ' + name)
        else:
            require(name not in files, 'Development-only launcher in public package: ' + name)
    registry = json.loads(files['support/Hub-Packages.json'])
    require(registry['schemaVersion'] == 1 and len(registry['packages']) == 1, 'Hub registry must contain only the supported native route')
    selected = registry['packages'][0]
    require(selected['relativeRoot'] == '../packages/current' and selected['id'] == 'flagship-approved' and
            selected['manifestSha256'] == sha(files[PACKAGE + 'support/PACKAGE-MANIFEST.json']), 'Hub package binding differs')
    require(PACKAGE + 'payload/NeuRotic.Fsr3.Vulkan.dll' in files, 'Native frame generation component missing')
    require_native_shipping(files)
    build = verification_evidence['build'] if verification_evidence is not None else json.loads(files[PACKAGE + 'support/BUILD-MANIFEST.json'])
    require(build['commit'] == original_package['commit'] == outer['source_commit'] and build['status'] == 'built' and
            build['build']['exit_code'] == 0, 'Source/build identity mismatch')
    require(build['product_sha256'] == sha(files[PACKAGE + 'payload/OptiScaler.dll']) and
            build['forwarder_sha256'] == sha(files[PACKAGE + 'payload/nvngx.dll_dlssnr.dll']) and
            build['sdk_sha256'] == sha(files[PACKAGE + 'payload/OptiScaler/amd_fidelityfx_dx12.dll']), 'Product/dependency mismatch')
    require(not build['provider_model_bundled'] and not build['streamline_provider_bundled'], 'Unexpected proprietary model/provider claim')
    provenance = original_package['component_provenance']['inspector']
    if verification_evidence is not None:
        distribution = verification_evidence['distribution']
    require(distribution['private_protocol_schema'] == provenance['private_protocol_schema'] == 3 and
            provenance['product_sha256'] == build['product_sha256'], 'Inspector schema/product mismatch')
    models = json.loads(files[WORKER + 'models.lock.json'])['models']
    require(len(models) == 3 and len({m['sha256'] for m in models}) == 3, 'Expected three distinct locked Inspector models')
    for model in models:
        raw = files[WORKER + safe_name(model['destination'])]
        require(sha(raw) == model['sha256'] and len(raw) == model['bytes'], 'Inspector model mismatch')
    require([m['sha256'] for m in distribution['models']] == [m['sha256'] for m in models], 'Distribution model identities differ')
    logging_profile = package.get('loggingProfile', 'public')
    require(build.get('loggingProfile', logging_profile) == logging_profile, 'Logging profile manifests differ')
    verify_logging_profile(files[PACKAGE + 'payload/OptiScaler.ini'], logging_profile)
    ini = configparser.ConfigParser(interpolation=None, strict=False)
    ini.read_string(files[PACKAGE + 'payload/OptiScaler.ini'].decode('utf-8-sig'))
    for section, key in [('DlssNr', 'Enabled'), ('CharacterInspector', 'Enabled'),
                         ('DLSSG', 'ExperimentalUnlockRTX30'), ('DLSSG', 'ExperimentalUnlockRTX20')]:
        require(ini.get(section, key, fallback='false').strip().lower() == 'false', 'Experimental default enabled: ' + key)
    require(PACKAGE + 'payload/Licenses/MFGAmpereUnlock-MIT.txt' in files, 'RTX20/30 license missing')
    require('Tools/WindowWorker/NeuRotic.WindowWorker.exe' in files and 'NeuRotic.exe' in files and
            'discovery/NeuRotic.Discovery.dll' in files, 'App/worker component missing')
    require('Tools/WindowWorker/nvngx.dll_dlssnr.dll' in files, 'Window Worker forwarder missing')
    worker_forwarder_sha256 = verify_window_worker_pair(files['Tools/WindowWorker/NeuRotic.WindowWorker.exe'],
                                                       files['Tools/WindowWorker/nvngx.dll_dlssnr.dll'])
    ingame_worker_forwarder_sha256 = verify_ingame_window_worker(files)
    return {'members': len(files), 'all_member_hashes_verified': True, 'private_protocol_schema': 3, 'loggingProfile': logging_profile,
            'window_worker_forwarder_sha256': worker_forwarder_sha256,
            'ingame_window_worker_forwarder_sha256': ingame_worker_forwarder_sha256,
            'models': [m['sha256'] for m in models], 'inherited_binary_path_findings': [n for n in findings if n in dependency_names],
            'owned_binary_diagnostic_path_findings': owned_findings, 'owned_codeview_paths': owned_codeview_paths,
            'source_commit': outer['source_commit'], 'product_source_commit': build['product_source_commit'],
            'product_sha256': build['product_sha256'],
            'hub_package_manifest_sha256': sha(files[PACKAGE + 'support/PACKAGE-MANIFEST.json'])}


def lifecycle_source():
    """Reuse the existing real installer checks with explicit, checked adapters."""
    source_path = ROOT / 'tools/character-inspector/verify_live_package.py'
    source = source_path.read_text(encoding='utf-8')
    def replace(old, new):
        nonlocal source
        require(source.count(old) == 1, 'Lifecycle adapter source changed: ' + old[:70])
        source = source.replace(old, new)
    replace("fixture=root.parent/'_inspector-motion-fixture'", "fixture=work/'fixture'")
    replace("assert fixture.resolve()==root.resolve().parent/'_inspector-motion-fixture'", "assert fixture.resolve()==work/'fixture'")
    replace("    package = fixture / 'package' / folder", "    hub_root = fixture / 'package' / folder\n    package = hub_root / 'packages/current'")
    replace("    worker=package/'payload/OptiScaler/CharacterInspector'",
            "    worker=package/" + repr('payload/' + inspector_component()['relativeRoot']))
    replace("        manifest=json.loads((package/'support/PACKAGE-MANIFEST.json').read_text())\n"
            "        provenance=manifest['component_provenance']['inspector']\n"
            "        check('current Inspector provenance matches packaged schema and DLL',\n"
            "            provenance['private_protocol_schema']==3 and provenance['product_sha256']==product_hash and\n"
            "            provenance['product_source_commit']==receipt_data['product_source_commit'])",
            "        check('verified Inspector receipt matches packaged schema and DLL',\n"
            "            receipt_data['private_protocol_schema']==3 and\n"
            "            receipt_data['product_sha256']==product_hash and\n"
            "            sha((package/'payload/OptiScaler.dll').read_bytes())==product_hash)")
    replace("    check('original pre-App Setup launcher bytes',sha((package/'NeuRotic-Setup.cmd').read_bytes())==\n"
            "        '9c6158036ef49f8c487b192c59d5e9b5587c8ec8196f5da10b9262d501b41a79')",
            "    check('single root manual launcher pair', (hub_root/'NeuRotic-Manual-Setup.cmd').is_file() and "
            "(hub_root/'NeuRotic-Manual-Uninstall.cmd').is_file() and not list(package.glob('*.cmd')))")
    replace("'/d', '/c', 'NeuRotic-Setup.cmd',", "'/d', '/c', 'call', str(hub_root/'NeuRotic-Manual-Setup.cmd'),")
    # Preview is metadata-only for payloads. Tamper refusal belongs at the
    # actual install boundary and must still leave every game file unchanged.
    replace("'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly'],",
            "'-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-ConfirmInstall'],")
    replace("logs.append('tampered model preflight\\n'", "logs.append('tampered model installation\\n'")
    # Use the current protocol, including hold age; the historical verifier used
    # schema2 despite accepting a schema3 motion verifier as an optional step.
    source = source.replace("'schema':2", "'schema':3")
    replace("'pose_requested':False,'detect_objects':False}", "'pose_requested':False,'detect_objects':False,'hold_ms':80}")
    replace("    env['PSModulePath'] = ''", "    env['PSModulePath'] = ''\n    env['NEUROTIC_HUB_FIXTURE_ROOT'] = str(fixture / 'direct-user-data')")
    marker = "    game = fixture / 'Other D3D12 Game'"
    replace(marker, '''    hub_game=fixture/'Hub inert game';hub_game.mkdir()
    hub_exe=hub_game/'FixtureGame.exe';shutil.copyfile('C:/Windows/System32/cmd.exe',hub_exe)
    hub_request=fixture/'hub-request.json';hub_result=fixture/'hub-result.json'
    hub_request.write_text(json.dumps({'protocolVersion':1,'requestId':'2'*32,'kind':'Plan',
        'gameExecutable':str(hub_exe),'operation':'Install','packageId':'flagship-approved',
        'proxyName':'dxgi.dll','continueWithoutRuntime':True}),encoding='utf-8')
    result=subprocess.run(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
        '-NoProfile','-ExecutionPolicy','Bypass','-File',str(hub_root/'support/NeuRotic-Setup-Engine.ps1'),
        '-HubNonInteractive','-HubRequestPath',str(hub_request),'-HubResultPath',str(hub_result)],
        cwd=fixture,capture_output=True,text=True,encoding='utf-8',timeout=300,
        env={**os.environ,'PSModulePath':'','NEUROTIC_HUB_FIXTURE_ROOT':str(fixture/'hub-user-data')})
    logs.append('packaged Hub plan\\n'+result.stdout+result.stderr)
    check('packaged Hub plan exit',result.returncode==0)
    planned=json.loads(hub_result.read_text(encoding='utf-8-sig'))
    check('packaged Hub resolves current payload',planned['status']=='Planned' and
        planned['packageDigest'].lower()==receipt_data['hub_package_manifest_sha256'])
    check('Hub plan writes no game DLL',not (hub_game/'dxgi.dll').exists())

''' + marker)
    return source_path, source


def lifecycle(archive, receipt, motion_image):
    WORK.mkdir(parents=True, exist_ok=True)
    fixture = WORK / 'fixture'
    require(not fixture.exists(), 'Previous release fixture exists; inspect before retrying')
    source_path, source = lifecycle_source()
    (WORK / 'live-package-receipt.json').write_text(json.dumps(receipt), encoding='utf-8')
    old_argv = sys.argv
    sys.argv = [str(source_path), '--work', str(WORK)]
    if motion_image:
        sys.argv += ['--motion-image', str(motion_image)]
    try:
        exec(compile(source, str(source_path), 'exec'), {'__file__': str(source_path), '__name__': '__main__'})
    finally:
        sys.argv = old_argv
        for name in ('live-installer-tests.json', 'live-installer-tests.txt', 'live-installer-failure-records.json', 'packaged-motion-worker.json'):
            path = WORK / name
            if path.exists():
                shutil.copyfile(path, EVIDENCE / ('release-' + name))


class ContractTests(unittest.TestCase):
    @unittest.skipUnless(os.name == 'nt', 'Manual CMD entry points require Windows')
    def test_root_manual_launchers_forward_paths_mode_and_exit_code(self):
        spec = importlib.util.spec_from_file_location('release_build', Path(__file__).with_name('Build-ReleaseBundle.py'))
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        fixture = WORK / 'manual launcher fixture'
        require(not fixture.exists(), 'Prior manual launcher fixture exists')
        for parent in (fixture, *fixture.parents):
            require(not parent.exists() or not (parent.stat(follow_symlinks=False).st_file_attributes & 0x400),
                    'Linked manual launcher fixture path')
        support = fixture / 'packages/current/support'
        support.mkdir(parents=True)
        try:
            (support / 'NeuRotic-Setup-Engine.ps1').write_text(
                "param([string]$GameExecutable,[switch]$Uninstall)\n"
                "[Console]::WriteLine('FORWARDED=' + $GameExecutable + ';UNINSTALL=' + $Uninstall.IsPresent)\nexit 17\n",
                encoding='utf-8')
            for name, raw in builder.manual_launchers().items():
                path = fixture / name
                path.write_bytes(raw)
                result = subprocess.run([str(Path(os.environ['SystemRoot']) / 'System32/cmd.exe'),
                                         '/d', '/c', 'call', str(path), '-GameExecutable', 'path with spaces/game.exe'],
                                        cwd=ROOT, input='', capture_output=True, text=True, timeout=30,
                                        creationflags=subprocess.CREATE_NO_WINDOW)
                self.assertEqual(result.returncode, 17, result.stdout + result.stderr)
                mode = 'True' if 'Uninstall' in name else 'False'
                self.assertIn('FORWARDED=path with spaces/game.exe;UNINSTALL=' + mode, result.stdout)
        finally:
            for base, dirs, names in os.walk(fixture, followlinks=False):
                for name in dirs + names:
                    require(not ((Path(base) / name).stat(follow_symlinks=False).st_file_attributes & 0x400),
                            'Linked manual launcher fixture member')
            shutil.rmtree(fixture)

    def test_release_layout_requires_single_root_manual_launchers(self):
        files = {'NeuRotic.exe': b'MZ', 'NeuRotic-Manual-Setup.cmd': b'setup',
                 'NeuRotic-Manual-Uninstall.cmd': b'uninstall'}
        verify_layout(files)
        for obsolete in ('NeuRotic.Hub.exe', 'NeuRotic-Setup.cmd', 'NeuRotic-Uninstall.cmd',
                         PACKAGE + 'NeuRotic-Setup.cmd', PACKAGE + 'NeuRotic-Manual-Uninstall.cmd',
                         'Models/README.txt', 'streamline/README.txt'):
            with self.assertRaises(ValueError): verify_layout({**files, obsolete: b'obsolete'})
        for required in files:
            with self.assertRaises(ValueError): verify_layout({n: v for n, v in files.items() if n != required})

    def test_manifest_rejects_tampered_missing_extra_and_duplicate_files(self):
        files = {'payload.bin': b'good'}
        files['manifest.json'] = json.dumps({'files': [{'path': 'payload.bin', 'bytes': 4, 'sha256': sha(b'good')}]}).encode()
        verify_inventory(files, 'manifest.json')
        for changed in ({**files, 'payload.bin': b'evil'}, {'manifest.json': files['manifest.json']}, {**files, 'extra': b'x'}):
            with self.assertRaises(ValueError): verify_inventory(changed, 'manifest.json')
        duplicate = json.loads(files['manifest.json']); duplicate['files'] *= 2
        with self.assertRaises(ValueError): verify_inventory({**files, 'manifest.json': json.dumps(duplicate).encode()}, 'manifest.json')

    def test_archive_paths_are_windows_safe(self):
        for name in ('../escape', '/absolute', 'a\\b', 'C:/drive', 'a//b', 'a/../b', 'AUX.txt', 'a/name.'):
            with self.assertRaises(ValueError): safe_name(name)
        self.assertEqual(safe_name('packages/current/a.txt'), 'packages/current/a.txt')

    def test_worker_forwarder_pair_uses_compiled_pin(self):
        forwarder = b'MZstandalone forwarder'
        worker = b'MZworker\0' + sha(forwarder).encode('ascii') + b'\0'
        self.assertEqual(verify_window_worker_pair(worker, forwarder), sha(forwarder))
        with self.assertRaises(ValueError):
            verify_window_worker_pair(worker, b'MZdifferent valid PE forwarder')
        with self.assertRaises(ValueError):
            verify_window_worker_pair(b'MZstale worker', forwarder)
        with self.assertRaises(ValueError):
            verify_window_worker_pair(b'MZworker\0' + sha(forwarder).encode('ascii') + b'extended\0', forwarder)

    def test_ingame_worker_reuses_verified_desktop_pair(self):
        forwarder = b'MZstandalone forwarder'
        worker = b'MZworker\0' + sha(forwarder).encode('ascii') + b'\0'
        desktop = {'Tools/WindowWorker/NeuRotic.WindowWorker.exe': worker,
                   'Tools/WindowWorker/nvngx.dll_dlssnr.dll': forwarder}
        self.assertTrue(callable(globals().get('verify_ingame_window_worker')),
                        'Release verification must require the dedicated in-game worker pair')
        root = PACKAGE + 'payload/OptiScaler/NRAnything/'
        files = {**desktop, root + 'NeuRotic.WindowWorker.exe': worker,
                 root + 'nvngx.dll_dlssnr.dll': forwarder}
        for name in ('LICENSE', 'RenoDX_ATTRIBUTION.txt', 'Magpie-LICENSE.txt'):
            files['Tools/WindowWorker/' + name] = files[root + name] = b'license'
        files['licenses/nlohmann-JSON-MIT.txt'] = files[root + 'nlohmann-JSON-MIT.txt'] = b'json license'
        self.assertEqual(verify_ingame_window_worker(files), sha(forwarder))
        for name in tuple(files):
            if name.startswith(root):
                with self.assertRaises(ValueError):
                    verify_ingame_window_worker({n: raw for n, raw in files.items() if n != name})
                with self.assertRaises(ValueError):
                    verify_ingame_window_worker({**files, name: b'MZchanged'})
        with self.assertRaises(ValueError):
            verify_ingame_window_worker({**files, root + 'unexpected.dll': b'MZextra'})
        with self.assertRaises(ValueError):
            verify_ingame_window_worker({**files, root + 'nvngx.dll_dlssnr.dll': b'MZsame but unpinned',
                                         'Tools/WindowWorker/nvngx.dll_dlssnr.dll': b'MZsame but unpinned'})
        spec = importlib.util.spec_from_file_location('payload_builder', Path(__file__).with_name('Build-ReleaseBundle.py'))
        builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(builder)
        for name in ('NeuRotic.WindowWorker.exe', 'nvngx.dll_dlssnr.dll'):
            self.assertEqual(builder.COMPONENTS[root + name], builder.COMPONENTS['Tools/WindowWorker/' + name])

    def test_internal_and_private_content_is_refused(self):
        for name in ('Source/Hub/a.cpp', 'verification/a.json', 'a.pdb', 'Tools/WindowWorker/INTEGRATION.md',
                     'packages/current/MERGE-RECORD.md', 'nvngx_dlssnr.dll', 'support/candidate_observer.py'):
            with self.assertRaises(ValueError): scan_content({name: b'content'})
        with self.assertRaises(ValueError): scan_content({'README.txt': b'C:\\Users\\Josh Parke\\private'})
        with self.assertRaises(ValueError): scan_content({'receipt.json': json.dumps({'path': 'C:\\Users\\Josh Parke\\private'}).encode()})
        self.assertEqual(scan_content({'README.txt': b'Open NeuRotic.Hub.exe.'}), [])
        self.assertEqual(scan_content({'manifest.json': b'{"sha256":"f97a10a9bbb"}'}), [])

    def test_public_surface_rejects_developer_launchers_and_notes(self):
        for name in ('Start-DRED-Capture.cmd', 'support/Start-DredCapture.ps1',
                     'TEST-BUILD.txt', 'VALIDATION-CHECKLIST.md', 'PASSIVE-DISCOVERY-TESTING.txt',
                     'KNOWN-ISSUES.txt', 'KNOWN-LIMITS.txt'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                scan_content({name: b'Internal testing material'})
        for raw in (b'NR-WO-0034: source freeze pending.',
                    b'Internal review notes: QA_FINAL_SOURCE_REVIEW_04.json',
                    b'See docs/ai/live/WORK_LEDGER.md for developer notes.',
                    b'Monster Hunter Wilds\nVisual recovery still needs in-game verification.'):
            with self.subTest(note=raw), self.assertRaises(ValueError):
                scan_content({'README.txt': raw})
        for name in ('KNOWN-ISSUES.txt', 'KNOWN-LIMITS.txt'):
            with self.subTest(reference=name), self.assertRaises(ValueError):
                scan_content({'START HERE.txt': ('See ' + name).encode()})

    def test_public_package_rejects_redundant_documents_and_build_provenance(self):
        for name in ('ARCHIVE-MANIFEST.json', 'COMPONENTS.json', 'CHANGES.txt',
                     'INSTALL-CHECKLIST.txt', 'START HERE.txt', PACKAGE + 'README.txt',
                     PACKAGE + 'support/BUILD-MANIFEST.json'):
            with self.subTest(name=name), self.assertRaises(ValueError):
                scan_content({name: b'{}'})

    def test_external_inventory_covers_every_public_member(self):
        files = {'payload.bin': b'good'}
        manifest = {'files': [{'path': 'payload.bin', 'bytes': 4, 'sha256': sha(b'good')}]}
        self.assertIn('manifest', __import__('inspect').signature(verify_inventory).parameters,
                      'Public archives need a separate verification inventory')
        verify_inventory(files, 'external inventory', manifest=manifest)
        for changed in ({'payload.bin': b'evil'}, {}, {**files, 'extra': b'x'}):
            with self.assertRaises(ValueError):
                verify_inventory(changed, 'external inventory', manifest=manifest)

    def test_internal_profile_retains_explicit_development_content(self):
        files = {'Start-DRED-Capture.cmd': b'Internal launch',
                 'support/Start-DredCapture.ps1': b'Internal capture',
                 'TEST-BUILD.txt': b'Internal instructions',
                 'KNOWN-ISSUES.txt': b'NR-WO-0034: Internal review notes'}
        self.assertEqual(scan_content(files, internal_testing=True), [])

    def test_public_policy_preserves_diagnostics_and_dependency_tests(self):
        files = {'support/Collect-NR-Review.ps1': b'Export requested diagnostic reports',
                 WORKER + 'runtime/DLLs/_testmultiphase.pyd': b'Unchanged vendor dependency',
                 WORKER + 'packages/upstream/README.md': b'Upstream developer notes and source freeze examples',
                 'README.txt': b'Compatibility varies by game and GPU. Use NR Anything when in-game setup is incompatible.',
                 'licenses/THIRD-PARTY-NOTICES.txt': b'Copyright Josh Triplett'}
        self.assertEqual(scan_content(files), [])

    def test_installer_adapter_compiles_with_current_fixture(self):
        path, source = lifecycle_source()
        compile(source, str(path), 'exec')

    def test_motion_adapter_uses_verified_receipt_and_current_worker_layout(self):
        import ast
        import tempfile
        _, source = lifecycle_source()
        tree = ast.parse(source)
        branch = next(node for node in ast.walk(tree) if isinstance(node, ast.If)
                      and ast.unparse(node.test) == 'args.motion_image')
        prefix = []
        for node in branch.body:
            if isinstance(node, ast.Assign) and any(isinstance(target, ast.Name) and target.id == 'motion' for target in node.targets):
                break
            prefix.append(node)
        worker_assignment = next(node for node in ast.walk(tree) if isinstance(node, ast.Assign)
                                 and any(isinstance(target, ast.Name) and target.id == 'worker' for target in node.targets))
        with tempfile.TemporaryDirectory(prefix='neurotic-motion-metadata-') as temporary:
            package = Path(temporary)
            (package / 'support').mkdir()
            (package / 'support/PACKAGE-MANIFEST.json').write_text(json.dumps({'components': {'characterInspector': inspector_component()}}))
            (package / 'payload').mkdir()
            (package / 'payload/OptiScaler.dll').write_bytes(b'verified product')
            receipt = {'private_protocol_schema': 3, 'product_sha256': sha(b'verified product'), 'product_source_commit': 'source'}
            def check(label, condition):
                if not condition:
                    raise ValueError(label)
            environment = {'package': package, 'receipt_data': receipt, 'product_hash': receipt['product_sha256'],
                           'sha': sha, 'json': json, 'check': check}
            snippet = ast.Module(body=[worker_assignment, *prefix], type_ignores=[])
            exec(compile(ast.fix_missing_locations(snippet), '<motion metadata contract>', 'exec'), environment)
            self.assertEqual(environment['worker'], package / 'payload' / inspector_component()['relativeRoot'])
            (package / 'payload/OptiScaler.dll').write_bytes(b'tampered product')
            with self.assertRaises(ValueError):
                exec(compile(snippet, '<motion metadata contract>', 'exec'), environment)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--archive', type=Path)
    parser.add_argument('--sha256')
    parser.add_argument('--verification-evidence', type=Path, help='Separate evidence inventory for a minimal public archive')
    parser.add_argument('--lifecycle', action='store_true')
    parser.add_argument('--motion-image', type=Path)
    parser.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        result = unittest.TextTestRunner(verbosity=2).run(unittest.defaultTestLoader.loadTestsFromTestCase(ContractTests))
        return 0 if result.wasSuccessful() else 1
    require(args.archive and args.sha256, '--archive and --sha256 are required')
    require(sha(args.archive.read_bytes()) == args.sha256.lower(), 'Archive identity differs')
    evidence = json.loads(args.verification_evidence.read_bytes()) if args.verification_evidence else None
    details = verify_release(read_archive(args.archive), verification_evidence=evidence)
    receipt = {'path': str(args.archive.resolve()), 'sha256': args.sha256.lower(), 'bytes': args.archive.stat().st_size, **details}
    EVIDENCE.mkdir(parents=True, exist_ok=True)
    (EVIDENCE / 'release-archive-verification.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2), flush=True)
    if args.lifecycle:
        lifecycle(args.archive, receipt, args.motion_image)
    return 0


if __name__ == '__main__':
    sys.exit(main())
