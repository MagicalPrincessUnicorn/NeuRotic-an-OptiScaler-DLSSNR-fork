"""Finite direct CPU/WARP regression set; no product/model/game qualification.

The registration below is explicit. Newly added C++ tests require review and a
new registration; discovery is used only to detect omissions, never to compile
unknown files. Direct results supplement, and do not replace, supervised gates.
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import threading
import uuid
from owned_process import run as owned_run, developer_environment_script

ROOT = Path(__file__).resolve().parents[3]
SPECTRE = (
    'contracts', 'controlled_scene_producer', 'encode_binding', 'local_recording', 'materialization',
    'native_adapter_schema', 'native_context_interner', 'native_creation_parameters',
    'native_jitter_basis', 'native_parameter_restore', 'native_representation_attachment',
    'native_representation_purpose', 'native_runtime_attachment', 'native_sample_boundary',
    'native_source_semantics', 'native_unsealed_owner', 'opaque_sr_evaluation_receipt', 'owned_continuation',
    'process_source', 'provider_lifetime', 'sample_context', 'sample_planner',
    'sample_profile', 'sample_scope', 'schema', 'source_enrollment', 'source_owner',
    'stage_contracts', 'stage_schema', 'structural_native_context', 'structural_profile',
)
VERTICAL = (
    'native_allocation_record', 'native_binding_mapper', 'native_c07_result',
    'native_context_chain', 'native_frame_bridge', 'native_history_state',
    'native_lifecycle_integration', 'native_metadata_arena', 'native_model_creation',
    'native_ngx_call_capture', 'native_observation_owner', 'native_owner_set',
    'native_protocol_contract', 'native_recording_binding', 'native_recording_state', 'native_resource_registry',
    'native_same_recording', 'native_session',
)
REPORT_ARGUMENT = frozenset((
    'native_allocation_record', 'native_binding_mapper', 'native_c07_result',
    'native_context_chain', 'native_frame_bridge', 'native_history_state',
    'native_lifecycle_integration', 'native_metadata_arena', 'native_observation_owner',
    'native_owner_set', 'native_protocol_contract', 'native_recording_binding',
    'native_same_recording', 'native_session', 'initial_native_protocol',
    'strict_native_integration', 'fsr3_consumer_adapter',
))
TESTS = tuple((name, 'spectre/' + name + '.cpp') for name in SPECTRE) + tuple(
    (name, 'vertical-slice/' + name + '.cpp') for name in VERTICAL) + (
    ('initial_native_protocol', 'orchestration/initial_native_protocol.cpp'),)
TESTS += (('strict_native_integration', 'finalizer/strict_native_integration.cpp'),)
TESTS += (('fsr3_consumer_adapter', 'finalizer/fsr3_consumer_adapter.cpp'),)
CORE = ('nr/orchestration/StreamCoordinatorKernel.cpp',
        'nr/lifecycle/NativeSessionLifetime.cpp', 'nr/protocol/NativeRouteAdmission.cpp')
SCOPE = {'cpu': 'synthetic fixtures and current callable contracts', 'gpu': 'controlled WARP lifecycle/API only',
         'provider': 'NOT_RUN', 'model': 'NOT_RUN', 'game': 'NOT_RUN', 'iq': 'NOT_RUN',
         'performance': 'NOT_RUN', 'production_strict_enrollment': 'UNAVAILABLE', 'gate_approval': 'NOT_PERFORMED'}


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def save(path, value):
    path.write_text(json.dumps(value, indent=2) + '\n', encoding='utf-8')


def inventory():
    suffixes = {'.h', '.hpp', '.c', '.cpp', '.py', '.ps1', '.json'}
    files = {}
    for folder in ('OptiScaler', 'tests/nr', 'tools/nr'):
        for path in (ROOT / folder).rglob('*'):
            if path.is_file() and path.suffix.lower() in suffixes:
                files[path.relative_to(ROOT).as_posix()] = sha(path)
    return files


def summaries(output):
    rows = []
    for line in output.splitlines():
        if line.startswith('{'):
            try:
                value = json.loads(line)
                if value.get('kind') == 'suite-summary':
                    rows.append(value)
            except (ValueError, AttributeError):
                pass  # The complete raw output remains evidence.
        match = re.search(r'(\d+) checks, (\d+) failures', line)
        if not match:
            match = re.search(r'checks=(\d+)\s+(?:failed|failures)=(\d+)', line)
        if match:
            rows.append({'checks': int(match[1]), 'failures': int(match[2]), 'raw': line})
    return rows


def summary_is_valid(rows):
    if len(rows) != 1:
        return False
    row = rows[0]
    count = row.get('checks', row.get('semantic_assertions'))
    failed = row.get('failures', row.get('mismatches'))
    return type(count) is int and count > 0 and type(failed) is int and 0 <= failed <= count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--vcvars', default=r'C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvarsall.bat')
    parser.add_argument('--only', nargs='+', choices=[name for name, _ in TESTS], help='Run an explicitly selected subset')
    parser.add_argument('--jobs', type=int, choices=(1,), default=1)
    parser.add_argument('--list', action='store_true', help='Validate and list the finite registry without compilation')
    args = parser.parse_args()
    for folder, registered in (('spectre', SPECTRE), ('vertical-slice', VERTICAL)):
        observed = {path.stem for path in (ROOT / 'tests/nr' / folder).glob('*.cpp')}
        if observed != set(registered):
            raise SystemExit('INFRA_ERROR registry mismatch ' + folder + ': unregistered=' + str(sorted(observed - set(registered))) + ', missing=' + str(sorted(set(registered) - observed)))
    selected = [(name, path) for name, path in TESTS if not args.only or name in args.only]
    if args.list:
        for name, path in selected:
            print(name + ' ' + path)
        print('REGISTERED=' + str(len(TESTS)) + ' SELECTED=' + str(len(selected)))
        return 0
    run_id = uuid.uuid4().hex
    run = ROOT / 'x64' / ('spectre-regressions-' + datetime.datetime.now().strftime('%Y%m%d-%H%M%S-%f') + '-' + run_id[:8])
    run.mkdir(parents=True)
    print('RUN=' + str(run), flush=True)
    receipt = {'run_id': run_id, 'run': str(run), 'scope': SCOPE, 'selected_tests': [name for name, _ in selected],
               'source_commit': subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip(),
               'source_tree': subprocess.check_output(['git', 'rev-parse', 'HEAD^{tree}'], cwd=ROOT, text=True).strip(),
               'dirty_state': subprocess.check_output(['git', 'status', '--short'], cwd=ROOT, text=True).splitlines(),
               'registered_count': len(TESTS), 'selected_count': len(selected), 'full_registry_run': args.only is None,
               'compile_timeout_seconds': 1800, 'test_timeout_seconds': 90, 'jobs': args.jobs, 'commands': [], 'tests': [], 'result': 'INFRA_ERROR'}
    lock = threading.Lock()
    before = {}

    def publish():
        save(run / 'receipt.json', receipt)

    def execute(label, command, cwd, timeout, env):
        log = cwd / (label + '.log')
        item = {'label': label, 'command': list(map(str, command)), 'cwd': str(cwd), 'log': str(log),
                'started': datetime.datetime.now(datetime.timezone.utc).isoformat(), 'exit_code': None}
        with log.open('wb') as output:
            try:
                result = owned_run(command, cwd=cwd, env=env, stdout=output, stderr=subprocess.STDOUT, timeout=timeout)
                item['pid'] = result.supervision['process_id']
                item['exit_code'] = result.returncode
                item['supervision'] = result.supervision
                if result.infrastructure_error:
                    item['exception'] = 'owned process supervision failed'
            except (OSError, subprocess.SubprocessError) as error:
                item['exception'] = repr(error)
        item['ended'] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        item['log_sha256'] = sha(log)
        with lock:
            receipt['commands'].append(item)
            publish()
        return item

    try:
        before = inventory()
        save(run / 'sources-before.json', before)
        receipt['sources_before_sha256'] = sha(run / 'sources-before.json')
        env = {key.upper(): value for key, value in os.environ.items()}
        setup = run / 'environment.cmd'
        setup.write_text(developer_environment_script(args.vcvars))
        result = owned_run(['cmd.exe', '/d', '/c', str(setup)], capture_output=True, text=True, timeout=60, env=env)
        receipt['environment_setup'] = {'command': result.args, 'exit_code': result.returncode,
            'stdout': result.stdout, 'stderr': result.stderr, 'supervision': result.supervision}
        if result.returncode:
            raise RuntimeError('developer environment failed: ' + result.stderr)
        for line in result.stdout.splitlines():
            if '=' in line and not line.startswith('='):
                key, value = line.split('=', 1)
                env[key.upper()] = value
        if env.get('VCTOOLSVERSION', '').strip('\\/') != '14.44.35207' or env.get('VSCMD_ARG_TGT_ARCH') != 'x64':
            raise RuntimeError('exact MSVC 14.44.35207 x64 environment unavailable')
        env.pop('CL', None)
        env.pop('_CL_', None)
        env['VSTEL_EWDK'] = '1'
        compiler = Path(env['VCTOOLSINSTALLDIR']) / 'bin/Hostx64/x64/cl.exe'
        receipt['compiler'] = {'path': str(compiler), 'sha256': sha(compiler), 'version': env['VCTOOLSVERSION']}
        receipt['environment'] = {key: env.get(key) for key in ('VSCMD_ARG_TGT_ARCH', 'VISUALSTUDIOVERSION', 'WINDOWSSDKVERSION', 'VSTEL_EWDK')}
        receipt['detours_sha256'] = sha(ROOT / 'OptiScaler/library/detours/detours.lib')
        common = [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/W4', '/Zi', '/bigobj', '/Y-', '/UNDEBUG', '/DNR_ORCH_INIT_TESTING', '/DNR_LAB_WARP_ONLY',
                  '/I' + str(ROOT / 'OptiScaler'), '/I' + str(ROOT / 'OptiScaler/include')]
        core_dir = run / 'core'
        core_dir.mkdir()
        core_command = common + ['/c'] + [str(ROOT / 'OptiScaler' / path) for path in CORE] + ['/Fo' + str(core_dir) + '\\']
        core_result = execute('core-compile', core_command, core_dir, 1800, env)
        if core_result['exit_code'] != 0 or 'exception' in core_result:
            raise RuntimeError('fresh core compilation failed; no test result inferred')
        objects = [core_dir / (Path(path).stem + '.obj') for path in CORE]
        receipt['core_objects'] = {str(path): sha(path) for path in objects}
        supervisor = run / 'attachment-debugger.exe'
        supervisor_source = ROOT / 'tools/nr/attachment_debugger.cpp'
        compiled_supervisor = execute('supervisor-compile', [str(compiler), '/nologo', '/std:c++20', '/EHsc', '/Zi',
            str(supervisor_source), '/Fe' + str(supervisor), '/Fo' + str(run / 'supervisor.obj'),
            '/link', '/DEBUG', 'dbghelp.lib'], run, 1800, env)
        if compiled_supervisor['exit_code'] != 0 or 'exception' in compiled_supervisor:
            raise RuntimeError('crash supervisor compilation failed; no tests launched')
        receipt['crash_supervisor'] = {'path': str(supervisor), 'sha256': sha(supervisor),
            'source_sha256': sha(supervisor_source), 'policy': 'second-chance capture; stop suite after first failed case'}

        def one(name, relative):
            directory = run / name
            directory.mkdir()
            source = ROOT / 'tests/nr' / relative
            executable = directory / 'test.exe'
            item = {'name': name, 'source': relative, 'source_sha256': sha(source), 'result': 'INFRA_ERROR'}
            try:
                command = common + [str(source)] + list(map(str, objects)) + ['/Fe' + str(executable), '/Fo' + str(directory / 'test.obj'),
                    '/link', '/DEBUG', '/LIBPATH:' + str(ROOT / 'OptiScaler/library/detours'), 'd3d12.lib', 'dxgi.lib', 'd3dcompiler.lib', 'ole32.lib', 'detours.lib', '/STACK:8388608']
                compiled = execute('compile', command, directory, 1800, env)
                item['compile'] = compiled
                if compiled['exit_code'] == 0 and 'exception' not in compiled:
                    item['binary_sha256'] = sha(executable)
                    test_command = [str(supervisor), str(executable), str(directory / 'crash.dmp')] + ([run_id] if name in REPORT_ARGUMENT else [])
                    tested = execute('test', test_command, directory, 100, env)
                    item['test'] = tested
                    output = Path(tested['log']).read_text(encoding='utf-8', errors='replace')
                    item['summaries'] = summaries(output)
                    item['raw_output'] = output
                    if tested['exit_code'] in (0, 1) and 'exception' not in tested and 'INFRA_ERROR' not in output and summary_is_valid(item['summaries']):
                        item['result'] = 'PASS' if tested['exit_code'] == 0 else 'FAIL'
                    if item['result'] != 'INFRA_ERROR' and any(row.get('failures', row.get('mismatches', 0)) for row in item['summaries']):
                        item['result'] = 'FAIL'
            except (OSError, RuntimeError, subprocess.SubprocessError) as error:
                item['exception'] = repr(error)
            with lock:
                receipt['tests'].append(item)
                publish()
                print(name + ' ' + item['result'], flush=True)
            return item

        for name, path in selected:
            if one(name, path)['result'] != 'PASS':
                break  # Preserve the first failure; do not launch dependent cases.
        after = inventory()
        save(run / 'sources-after.json', after)
        receipt['source_files_unchanged'] = before == after
        receipt['counts'] = {status: sum(item['result'] == status for item in receipt['tests']) for status in ('PASS', 'FAIL', 'INFRA_ERROR')}
        receipt['result'] = ('INFRA_ERROR' if before != after or receipt['counts']['INFRA_ERROR'] else
            'PASS' if len(receipt['tests']) == len(selected) and all(item['result'] == 'PASS' for item in receipt['tests']) else 'FAIL')
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        receipt['exception'] = repr(error)
        print('INFRA_ERROR ' + repr(error), flush=True)
    finally:
        publish()
        print('RECEIPT=' + str(run / 'receipt.json'), flush=True)
    return int(receipt['result'] != 'PASS')


if __name__ == '__main__':
    raise SystemExit(main())
