"""Trusted host invocation of the existing CPU runner and Spectre evaluator.

The capability exists only inside this process after its own successful run.
Exported receipts/manifest are audit artifacts, never reusable credentials.
"""
from __future__ import annotations

import argparse
import datetime
import hashlib
import json
import os
import pathlib
import subprocess
import sys

from gate_evaluator import (SPECTRE_IDENTITIES, SPECTRE_REVIEWED_RUNNER_BINDINGS,
                            evaluate_spectre_candidate, spectre_runner_covers)
from supervised_producer import TrustedRunnerRun, verify_runner_binding

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/nr/spectre'))
from owned_process import run as owned_run
VS = pathlib.Path(r'C:\Program Files\Microsoft Visual Studio\18\Community')
MSVC = VS / 'VC/Tools/MSVC/14.44.35207/bin/HostX64/x64'
POWERSHELL = pathlib.Path(r'C:\Windows\System32\WindowsPowerShell\v1.0\powershell.exe')
CMD = pathlib.Path(r'C:\Windows\System32\cmd.exe')
SUITES = {
    'initial-profile-preparation': 'initial_profile_preparation.cpp',
    'initial-fixed-native-kernel': 'initial_fixed_native_kernel.cpp',
    'initial-native-admission': 'initial_native_admission.cpp',
    'initial-native-races': 'initial_native_races.cpp',
    'initial-native-protocol': 'initial_native_protocol.cpp',
}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def digest(path):
    require(path.is_file() and not path.is_symlink(), 'regular file required: ' + str(path))
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def hash_receipt(path):
    return dict(path=str(path), state='Recorded', sha256=digest(path), bytes=path.stat().st_size, error=None)


def put(path, document):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(document, stream, indent=2)
        stream.write('\n')


def git(*args):
    return subprocess.check_output(['git', *args], cwd=ROOT, timeout=30)


def git_tree(entries):
    """Compute Git tree identity without writing objects or touching the index."""
    tree = {}
    for name, mode, object_id in entries:
        node = tree
        parts = name.split('/')
        for component in parts[:-1]:
            node = node.setdefault(component, {})
        node[parts[-1]] = (mode, object_id)

    def encode(node):
        records = []
        for name, value in node.items():
            directory = isinstance(value, dict)
            mode, object_id = ('40000', encode(value)) if directory else value
            key = (name + ('/' if directory else '')).encode('utf-8', errors='surrogateescape')
            record = mode.encode() + b' ' + name.encode('utf-8', errors='surrogateescape') + b'\0'
            records.append((key, record + bytes.fromhex(object_id)))
        body = b''.join(record for _, record in sorted(records))
        return hashlib.sha1(b'tree ' + str(len(body)).encode() + b'\0' + body).hexdigest()

    return encode(tree)


def working_tree():
    entries = {}
    for record in git('ls-files', '--stage', '-z').split(b'\0'):
        if not record:
            continue
        header, name = record.split(b'\t', 1)
        mode, object_id, stage = header.decode().split()
        require(stage == '0', 'unmerged index cannot identify an exact source tree')
        entries[os.fsdecode(name)] = (mode, object_id)
    changed = set(os.fsdecode(item) for item in git('diff', '--name-only', '-z').split(b'\0') if item)
    changed.update(os.fsdecode(item) for item in git('ls-files', '--others', '--exclude-standard', '-z').split(b'\0') if item)
    for name in changed:
        path = ROOT / name
        if not path.exists():
            entries.pop(name, None)
            continue
        mode = entries.get(name, ('100644', None))[0]
        require(mode != '160000' and path.is_file(), 'changed submodule or non-file needs separate source identity')
        object_id = git('hash-object', '--', name).decode().strip()
        entries[name] = (mode, object_id)
    return git_tree([(name, mode, object_id) for name, (mode, object_id) in entries.items()])


def sources(*, include_reviewed=True):
    # Preserve the existing runner's closure, and independently retain every
    # additional transitive source in the reviewed oracle closure.
    paths = [path for folder in ('OptiScaler/nr', 'tests/nr') for path in (ROOT / folder).rglob('*')
             if path.is_file() and path.suffix.lower() in ('.h', '.cpp', '.ps1', '.py', '.json')]
    paths += [ROOT / name for name in ('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp',
              'OptiScaler/dlssnr/DlssNrFeature_Dx12.h', 'OptiScaler/OptiScaler.vcxproj',
              'OptiScaler/OptiScaler.vcxproj.filters', 'OptiScaler.ini')]
    if include_reviewed:
        paths += [ROOT / name for mapping in SPECTRE_REVIEWED_RUNNER_BINDINGS.values()
                  for name in mapping['source_sha256']]
    return [hash_receipt(path) for path in sorted(set(paths))]


def ps_quote(value):
    return "'" + str(value).replace("'", "''") + "'"


def prepare_launcher(output, python, timeout):
    launch = output / 'host-launch.ps1'
    environment = output / 'host-environment.json'
    owned = output / 'owned-run'
    script = """$ErrorActionPreference='Stop'
$requiredCompiler=@@COMPILER@@
if ($PSVersionTable.PSEdition -ne 'Desktop' -or $PSVersionTable.PSVersion.Major -ne 5) {throw 'Desktop PowerShell 5 required'}
if ((Get-Command cl.exe -CommandType Application).Source -ne $requiredCompiler -or $env:VSCMD_ARG_TGT_ARCH -ne 'x64' -or $env:CL -or $env:_CL_) {throw 'Exact clean MSVC14.44 x64 environment required'}
$selected=[ordered]@{}
foreach($name in @('VSCMD_VER','VSCMD_ARG_TGT_ARCH','VSCMD_ARG_HOST_ARCH','VCToolsVersion','WindowsSDKVersion','INCLUDE','LIB','LIBPATH','PATH','CL','_CL_','SystemRoot')) {$selected[$name]=[Environment]::GetEnvironmentVariable($name)}
$record=[ordered]@{kind='HOST_CAPTURED_CPU_ENVIRONMENT';edition=$PSVersionTable.PSEdition;version=$PSVersionTable.PSVersion.ToString();is_64bit=[Environment]::Is64BitProcess;selected_variables=$selected}
[IO.File]::WriteAllText(@@ENVIRONMENT@@,($record|ConvertTo-Json -Depth 8),[Text.UTF8Encoding]::new($false))
& @@RUNNER@@ -OutputDirectory @@OWNED@@ -MaintenanceLockPath @@LOCK@@ -PythonExecutable @@PYTHON@@ -TimeoutSeconds @@TIMEOUT@@
exit $LASTEXITCODE
"""
    for token, value in {'COMPILER': MSVC / 'cl.exe', 'ENVIRONMENT': environment,
                         'RUNNER': ROOT / 'tests/nr/orchestration/Run-NrOrchInit.ps1',
                         'OWNED': owned, 'LOCK': output / 'host-maintenance.lock', 'PYTHON': python}.items():
        script = script.replace('@@' + token + '@@', ps_quote(value))
    launch.write_text(script.replace('@@TIMEOUT@@', str(timeout)), encoding='utf-8')
    batch = output / 'host-launch.cmd'
    batch.write_text('@echo off\ncall "' + str(VS / 'VC/Auxiliary/Build/vcvarsall.bat') +
                     '" x64 -vcvars_ver=14.44.35207\nif errorlevel 1 exit /b %errorlevel%\n"' +
                     str(POWERSHELL) + '" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "' +
                     str(launch) + '"\nexit /b %errorlevel%\n', encoding='utf-8')
    command = [str(CMD), '/d', '/c', str(batch)]
    return command, owned, environment


def run_existing_runner(output, python, timeout):
    command, owned, environment = prepare_launcher(output, python, timeout)
    child_environment = dict(os.environ)
    child_environment['VSTEL_EWDK'] = '1'
    for name in list(child_environment):
        if name.upper() in ('CL', '_CL_'):
            del child_environment[name]
    with (output / 'host.stdout.log').open('xb') as stdout, (output / 'host.stderr.log').open('xb') as stderr:
        process = owned_run(command, cwd=ROOT, env=child_environment, stdout=stdout, stderr=stderr, timeout=3600)
    put(output / 'host-launch-result.json', dict(command=command, exit_code=process.returncode,
        supervision=process.supervision, infrastructure_error=process.infrastructure_error,
        child_environment_overrides=dict(VSTEL_EWDK='1', CL=None, _CL_=None)))
    require(process.returncode == 0 and not process.infrastructure_error and
            process.supervision.get('group_empty_observed') is True and
            process.supervision.get('capture_complete') is True and
            process.supervision.get('supervisor_source_sha256') == digest(ROOT / 'tests/nr/spectre/owned_process.py'),
            'existing supervised runner failed or left descendants; preserve evidence without enrollment')
    return owned, json.loads(environment.read_text(encoding='utf-8-sig'))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-parent', required=True, type=pathlib.Path)
    parser.add_argument('--python-executable', type=pathlib.Path, default=pathlib.Path(sys.executable))
    parser.add_argument('--suite', choices=SUITES, default='initial-native-admission')
    parser.add_argument('--timeout-seconds', type=int, default=1800)
    args = parser.parse_args(argv)
    args.python_executable = args.python_executable.resolve()
    output = args.output_parent.resolve()
    require(os.name == 'nt' and output.is_relative_to(pathlib.Path(r'C:\NeuRotic\builds')) and
            not output.exists(), 'fresh directory under C:\\NeuRotic\\builds required')
    require(not any(character in str(output) for character in '%!\r\n&|<>^'), 'unsafe launcher path')
    require(10 <= args.timeout_seconds <= 1800, 'bounded command timeout required')
    output.mkdir(parents=True)
    try:
        commit = git('rev-parse', 'HEAD').decode().strip()
        base_tree = git('rev-parse', 'HEAD^{tree}').decode().strip()
        source_tree = working_tree()
        runner_before = sources(include_reviewed=False)
        before = sources()
        inventory_path = ROOT / 'tests/nr/orchestration/initial_inventory.json'
        inventory_bytes = inventory_path.read_bytes()
        tools = [hash_receipt(path) for path in (CMD, POWERSHELL, args.python_executable,
                    MSVC / 'cl.exe', MSVC / 'lib.exe', MSVC / 'link.exe',
                    VS / 'VC/Auxiliary/Build/vcvarsall.bat')]
        put(output / 'host-prelaunch.json', dict(source_commit=commit, base_tree=base_tree,
            source_tree=source_tree, source_tree_kind='Git tree of current index plus filtered working changes and untracked nonignored files; no Git objects written',
            source_files=before, tool_hashes=tools, started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat()))
        owned, environment = run_existing_runner(output, args.python_executable, args.timeout_seconds)
        after = sources()
        require(before == after and source_tree == working_tree() and commit == git('rev-parse', 'HEAD').decode().strip(),
                'source changed during owned execution; no authority enrolled')
        require(tools == [hash_receipt(pathlib.Path(row['path'])) for row in tools],
                'tool changed during owned execution; no authority enrolled')
        summary = json.loads((owned / 'command-evidence.json').read_text(encoding='utf-8-sig'))
        expected_sources = {pathlib.Path(row['path']).resolve(): row['sha256'] for row in runner_before}
        require(summary['owned_checks_passed'] is True and summary['source_files_unchanged'] is True and all(
            {pathlib.Path(row['path']).resolve(): row['sha256'] for row in summary[key]} == expected_sources
            for key in ('sources_before', 'sources_after')), 'runner and host source/check closure differs')
        run_id = summary['run_id']
        inventory = json.loads(inventory_bytes)
        selected = [row['id'] for row in inventory['suites'][args.suite] if row['binding'] == 'CURRENT_CALLABLE']
        (owned / 'producer-inventory.json').write_bytes(inventory_bytes)
        put(owned / 'producer-sources-before.json', before)
        put(owned / 'producer-sources-after.json', after)
        put(owned / 'producer-environment.json', environment)
        environment_sha = digest(owned / 'producer-environment.json')
        config_identity = 'sha256:' + hashlib.sha256(json.dumps(dict(suite=args.suite,
            toolchain='MSVC14.44.35207-x64', timeout_seconds=args.timeout_seconds), sort_keys=True).encode()).hexdigest()
        commands = {}
        for role in ('compile', 'execute', 'verify'):
            matching = [path for path in owned.glob('*.intent.json')
                        if json.loads(path.read_text(encoding='utf-8-sig'))['label'] == args.suite + '-' + role]
            require(len(matching) == 1, 'exact command stage not found: ' + role)
            stem = matching[0].name[:-len('.intent.json')]
            commands[role] = dict(intent_ref=stem + '.intent.json', result_ref=stem + '.result.json',
                                 pre_hash_ref=stem + '.pre-hash.json', hashes_ref=stem + '.hashes.json')
        binary = owned / (args.suite + '.exe')
        binding = dict(schema_version='NeuRotic.SupervisedRunnerBinding/1', run_id=run_id,
            suite=args.suite, assertion_ids=selected, source_commit=commit, source_tree=source_tree,
            environment_identity='sha256:' + environment_sha, config_identity=config_identity,
            run_owner_ref='run-owner.json', sources_before_ref='producer-sources-before.json',
            sources_after_ref='producer-sources-after.json', inventory_ref='producer-inventory.json',
            inventory_source_path=str(inventory_path), environment_ref='producer-environment.json',
            binary_ref=binary.name, test_source_path=str(ROOT / 'tests/nr/orchestration' / SUITES[args.suite]),
            runner=hash_receipt(POWERSHELL), commands=commands)
        put(owned / 'producer-binding.json', binding)
        artifact_hashes = tuple((path.name, digest(path)) for path in owned.iterdir() if path.is_file())
        authority = 'supervised-host:' + run_id
        capability = TrustedRunnerRun(authority_identity=authority, origin='ACTUAL_RUNNER',
            run_directory=owned, run_id=run_id, source_commit=commit, source_tree=source_tree,
            source_hashes=tuple((row['path'], row['sha256']) for row in before),
            tool_hashes=tuple((row['path'], row['sha256']) for row in tools),
            environment_identity=binding['environment_identity'], environment_sha256=environment_sha,
            config_identity=config_identity, artifact_hashes=artifact_hashes)
        case_sha = digest(HERE / 'spectre_case_map_58.json')
        policy = json.loads((HERE / 'spectre_scope_policy.json').read_text(encoding='utf-8'))
        cases = json.loads((HERE / 'spectre_case_map_58.json').read_text(encoding='utf-8'))['cases']
        identities = dict.fromkeys(SPECTRE_IDENTITIES)
        identities.update(source_commit=commit, source_tree=source_tree,
            schema_identity='spectre-closure-v1-candidate', case_definition_identity=case_sha,
            environment_identity=binding['environment_identity'], config_identity=config_identity,
            binary_sha256=digest(binary))
        obligations = [dict(id=f"{case['id']}:{layer}:{axis}", case_id=case['id'], axis=axis,
                           mandatory=True, identities=identities) for case in cases
                       for layer in case['original_layers'] for axis in policy['layer_axes'][layer]]
        mapping_ids = [key for key, mapping in SPECTRE_REVIEWED_RUNNER_BINDINGS.items()
                       if mapping['suite'] == args.suite] or ['N01:unit:CPU']
        results, covered = [], []
        for mapping_id in mapping_ids:
            mapping = SPECTRE_REVIEWED_RUNNER_BINDINGS.get(mapping_id)
            if mapping and mapping['suite'] != args.suite:
                mapping = None
            result = dict(id='actual-cpu-receipt:' + run_id + ':' + mapping_id,
                obligation_id=mapping_id, status='NOT_RUN',
                test_binding=mapping['test_binding'] if mapping else args.suite + ':enrolled-current-callable-rows',
                producer=dict(identity=authority, kind='ACTUAL_RUNNER', authenticated_binding_ref='producer-binding.json'),
                identities=identities, started=False, oracle_met=None,
                reason='Actual CPU suite rows are authenticated separately; original oracle coverage has not yet been established.',
                artifacts=[dict(path=name, sha256=sha, kind='RESULT') for name, sha in artifact_hashes],
                control_result_id=None, mutant=False)
            obligation = next(item for item in obligations if item['id'] == mapping_id)
            verified = verify_runner_binding(owned, result, obligation, {authority: capability})
            if spectre_runner_covers(mapping_id, verified, result):
                covered.append(mapping_id)
                result.update(status='PASS', started=True, oracle_met=True, reason=mapping['scope'])
            else:
                result['reason'] = 'Actual CPU receipt only; no exact reviewed source/assertion mapping for this original oracle, which remains NOT_RUN.'
            results.append(result)
        manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
            case_map_sha256=case_sha, scope_policy_sha256=digest(HERE / 'spectre_scope_policy.json'),
            obligations=obligations, results=results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)
        put(owned / 'producer-manifest.json', manifest)
        evaluation = evaluate_spectre_candidate(owned / 'producer-manifest.json', trusted_runner_runs={authority: capability})
        put(output / 'producer-evaluation.json', evaluation)
        require(len(evaluation['verified_runner_receipts']) == len(results) and all(
                item['assertion_ids'] == selected for item in evaluation['verified_runner_receipts']),
                'actual runner receipt did not verify: ' + str(evaluation['producer_verification_errors']))
        require(len(evaluation['unsatisfied_obligations']) == 129 - len(covered) and
                all(item not in evaluation['unsatisfied_obligations'] for item in covered) and not evaluation['human_g2_granted'] and
                not evaluation['w04_authorized'], 'unmapped original scope unexpectedly promoted')
        put(output / 'producer-receipt.json', dict(kind='ACTUAL_OWNED_CPU_RECEIPT_AUTHENTICATED_IN_PROCESS',
            run_id=run_id, original_directory=str(owned), source_commit=commit, source_tree=source_tree,
            base_tree=base_tree, suite=args.suite, verified_assertion_ids=selected,
            verified_assertion_count=len(selected), product_obligations_proven=len(covered), mapped_obligations=covered,
            source_files_before=before, source_files_after=after, source_files_unchanged=True,
            tool_hashes=tools, artifacts=dict(artifact_hashes), environment_sha256=environment_sha,
            evaluation_sha256=digest(output / 'producer-evaluation.json'),
            exported_files_can_enroll_authority=False, human_g2=False, w04=False))
        print(json.dumps(dict(output=str(output), run_id=run_id, verified_rows=len(selected),
                              product_obligations_proven=len(covered), mapped_obligations=covered,
                              unsatisfied_obligations=len(evaluation['unsatisfied_obligations']))))
        return 0
    except (ValueError, OSError, subprocess.SubprocessError, KeyError, TypeError) as error:
        put(output / 'producer-failure.json', dict(error=str(error), authority_enrolled=False,
            product_obligations_proven=0, human_g2=False, w04=False))
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
