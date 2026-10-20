"""Own fresh Python-oracle runs and authenticate them in the invoking process.

R03 calls the existing actual controlled WARP host in-process and retains its
capability through the policy checks. R07 needs no GPU or proprietary runtime.
"""
from __future__ import annotations

import argparse
import dataclasses
import datetime
import hashlib
import json
import os
import pathlib
import platform
import shutil
import sys
import uuid

from oracle_producer import (ORACLE_BINDINGS, PROFILES, MUTATIONS, TrustedPythonOracleRun,
    classify, mutated_source, parse_assertions, stage_command, verify_oracle_binding, warp_fixture_document)
from controlled_producer import need, sha, supervised

HERE = pathlib.Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(ROOT / 'tests/nr/spectre'))
from owned_process import run as owned_run


def put(path, value):
    with path.open('x', encoding='utf-8') as stream:
        json.dump(value, stream, indent=2); stream.write('\n')


def source_snapshot(profile):
    from gate_evaluator import SPECTRE_REVIEWED_RUNNER_BINDINGS
    mapping = next(value for key, value in SPECTRE_REVIEWED_RUNNER_BINDINGS.items()
                   if key in ORACLE_BINDINGS and ORACLE_BINDINGS[key]['profile'] == profile)
    reviewed = mapping['source_sha256']
    need(reviewed, 'Python oracle reviewed closure is unavailable')
    actual = {path: sha(ROOT / path) for path in reviewed}
    need(actual == reviewed, 'current source differs from reviewed Python oracle closure')
    # The evaluator carries the review-manifest digest and cannot be in that
    # manifest. Bind its actual bytes separately across this owned invocation.
    actual['tests/nr/vertical-slice/gate_evaluator.py'] = sha(HERE / 'gate_evaluator.py')
    return actual


def run_cases(output, profile, run_id, controlled_context=None, initial_artifacts=None):
    """Execute finite reviewed cases; callable for focused development evidence."""
    output.mkdir(parents=True, exist_ok=False)
    for name, value in (initial_artifacts or {}).items():
        put(output / name, value)
    fixture = output / 'warp-fixture.json' if controlled_context else None
    if fixture:
        put(fixture, warp_fixture_document(controlled_context))
    checks = HERE / 'oracle_checks.py'
    results = {}
    for case in ('baseline', *PROFILES[profile]['mutations']):
        mutation = None if case == 'baseline' else case
        directory = output / case; directory.mkdir()
        subject = directory / 'subject.py'
        subject.write_text(mutated_source(ROOT, profile, mutation), encoding='utf-8')
        for role in ('syntax', 'execute'):
            command = stage_command(sys.executable, checks, profile, run_id, subject, fixture, role == 'syntax')
            attempt = uuid.uuid4().hex
            # Save intent before starting user code, and final supervision even
            # when launch, timeout, capture or cleanup fails.
            intent = dict(run_id=run_id, attempt_id=attempt, command=command, cwd=str(output), timeout_seconds=90)
            put(directory / (role + '.intent.json'), intent)
            with (directory / (role+'.stdout.log')).open('xb') as stdout, (directory / (role+'.stderr.log')).open('xb') as stderr:
                process = owned_run(command, cwd=output, stdout=stdout, stderr=stderr, timeout=90)
            record = dict(intent, returncode=process.returncode, infrastructure_error=process.infrastructure_error,
                          supervision=process.supervision)
            for stream in ('stdout', 'stderr'):
                capture = directory / (role + '.' + stream + '.log')
                record[stream] = dict(path=capture.relative_to(output).as_posix(), sha256=sha(capture))
            put(directory / (role + '.json'), record)
            if process.infrastructure_error or process.returncode not in ((0,) if role == 'syntax' else (0,1)):
                put(output / 'campaign-failure.json', dict(case=case, role=role, result='INFRA_ERROR'))
                raise ValueError('owned Python command failed: '+case+'/'+role)
            supervised(process.supervision, process.returncode)
        rows = parse_assertions((directory / 'execute.stdout.log').read_text(encoding='utf-8'), run_id, profile)
        result = classify(profile, mutation, process.returncode, rows)
        results[case] = dict(result=result, assertions=rows,
            expected_detector=MUTATIONS[mutation]['detector'] if mutation else None,
            failed_assertions=[name for name, matched in rows.items() if not matched])
        put(directory / 'case-result.json', results[case])
        need(result == ('PASS' if mutation is None else 'KILLED'), 'oracle baseline or named semantic mutant failed: '+case)
    put(output / 'case-results.json', results)
    return results


def execute_campaign(output, profile, controlled_context=None):
    from gate_evaluator import (SPECTRE_IDENTITIES, SPECTRE_REVIEWED_RUNNER_BINDINGS, evaluate_spectre_candidate)
    from run_supervised_producer import git, working_tree
    before = source_snapshot(profile)
    commit = git('rev-parse', 'HEAD').decode().strip(); tree = working_tree()
    tools = {str(pathlib.Path(sys.executable)): sha(pathlib.Path(sys.executable))}
    run_id = uuid.uuid4().hex
    environment = dict(kind='HOST_CAPTURED_PYTHON_EVALUATOR_ENVIRONMENT', executable=sys.executable,
        python_version=sys.version, platform=platform.platform(), process_bits=64 if sys.maxsize > 2**32 else 32,
        profile=profile, inner_fixture_scope=PROFILES[profile]['scope'])
    config = 'sha256:' + hashlib.sha256(json.dumps(dict(profile=profile, mutations=PROFILES[profile]['mutations'],
        syntax_seconds=90, execute_seconds=90, protocol='PythonOracleBinding/1'), sort_keys=True).encode()).hexdigest()
    results = run_cases(output, profile, run_id, controlled_context, {
        'sources-before.json': before, 'tools.json': tools, 'environment.json': environment,
        'host-prelaunch.json': dict(run_id=run_id, source_commit=commit, source_tree=tree,
            started_utc=datetime.datetime.now(datetime.timezone.utc).isoformat())})
    after = source_snapshot(profile); put(output / 'sources-after.json', after)
    need(before == after and tree == working_tree() and commit == git('rev-parse', 'HEAD').decode().strip(),
         'source tree changed during actual Python oracle campaign')
    need(all(sha(pathlib.Path(path)) == value for path, value in tools.items()), 'Python executable changed')
    environment_identity = 'sha256:' + sha(output / 'environment.json')
    shutil.copyfile(sys.executable, output / 'python.exe')
    binding = dict(schema_version='NeuRotic.PythonOracleBinding/1', run_id=run_id, profile=profile,
        source_commit=commit, source_tree=tree, environment_identity=environment_identity, config_identity=config,
        python_executable=sys.executable)
    put(output / 'oracle-binding.json', binding)
    pins = tuple((path.relative_to(output).as_posix(), sha(path)) for path in sorted(output.rglob('*')) if path.is_file())
    authority = 'python-oracle-host:'+run_id
    capability = TrustedPythonOracleRun(authority, 'ACTUAL_RUNNER', output, ROOT, run_id, profile, commit, tree,
        tuple(before.items()), tuple(tools.items()), environment_identity, config, pins, controlled_context)
    case_path = HERE / 'spectre_case_map_58.json'; policy_path = HERE / 'spectre_scope_policy.json'
    cases = json.loads(case_path.read_text(encoding='utf-8'))['cases']; policy = json.loads(policy_path.read_text())
    identities = dict.fromkeys(SPECTRE_IDENTITIES)
    identities.update(source_commit=commit, source_tree=tree, schema_identity='spectre-closure-v1-candidate',
        case_definition_identity=sha(case_path), environment_identity=environment_identity, config_identity=config,
        profile_identity='TEST003-'+PROFILES[profile]['scope'], binary_sha256=sha(output/'python.exe'))
    if controlled_context:
        identities['driver_identity'] = controlled_context[0].driver_identity
    obligations = [dict(id=f"{case['id']}:{layer}:{axis}", case_id=case['id'], axis=axis, mandatory=True,
        identities=dict(identities)) for case in cases for layer in case['original_layers'] for axis in policy['layer_axes'][layer]]
    selected = [key for key, value in ORACLE_BINDINGS.items() if value['profile'] == profile]
    artifacts = [dict(path=path, sha256=value, kind='BINARY' if path == 'python.exe' else 'LOG' if path.endswith('.log') else 'RESULT')
                 for path, value in pins]
    selected_results = []
    control = 'R07:unit:CPU' if profile == 'r07' else 'R03:controlled D3D12 host:WARP_MECHANICS'
    for key in selected:
        mapping = ORACLE_BINDINGS[key]; mutant = mapping.get('mutant', False)
        selected_results.append(dict(id=run_id+':'+key, obligation_id=key, status='PASS',
            test_binding=mapping['test_binding'], identities=dict(identities), started=True, oracle_met=True,
            producer=dict(identity=authority, kind='ACTUAL_RUNNER', authenticated_binding_ref='oracle-binding.json'),
            reason='Executed Python oracle; '+PROFILES[profile]['scope']+'; no provider/model/game/G2 qualification',
            artifacts=artifacts, control_result_id=run_id+':'+control if mutant else None, mutant=mutant))
    for result in selected_results:
        obligation = next(row for row in obligations if row['id'] == result['obligation_id'])
        proof = verify_oracle_binding(output, result, obligation, {authority: capability})
        need(proof['verified'], 'actual Python oracle authentication failed: '+proof['reason'])
    manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
        case_map_sha256=sha(case_path), scope_policy_sha256=sha(policy_path), obligations=obligations,
        results=selected_results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)
    put(output / 'producer-manifest.json', manifest)
    evaluation = evaluate_spectre_candidate(output/'producer-manifest.json', trusted_runner_runs={authority: capability})
    put(output / 'producer-evaluation.json', evaluation)
    need(evaluation['structural_valid'] and not evaluation['structural_errors'] and
         len(evaluation['unsatisfied_obligations']) == 129-len(selected) and
         all(key not in evaluation['unsatisfied_obligations'] for key in selected) and
         not evaluation['human_g2_granted'] and not evaluation['w04_authorized'], 'coverage exceeds or misses exact oracle profile')
    replay = evaluate_spectre_candidate(output/'producer-manifest.json')
    put(output / 'untrusted-replay-evaluation.json', replay)
    need(not replay['verified_runner_receipts'] and len(replay['unsatisfied_obligations']) == 129,
         'exported oracle receipt unexpectedly admitted reusable authority')
    need(before == source_snapshot(profile) and tree == working_tree() and
         commit == git('rev-parse', 'HEAD').decode().strip(), 'source drift during final authentication')
    need(all(sha(pathlib.Path(path)) == value for path, value in tools.items()), 'tool drift during final authentication')
    put(output / 'producer-receipt.json', dict(kind='ACTUAL_PYTHON_ORACLE_AUTHENTICATED_IN_PROCESS', run_id=run_id,
        source_commit=commit, source_tree=tree, source_files=before, source_files_unchanged=True,
        mapped_obligations=selected, cases=results, original_obligations=129, unsatisfied_obligations=129-len(selected),
        scope=PROFILES[profile]['scope'], physical_substrate=PROFILES[profile]['physical'],
        exported_files_can_enroll_authority=False, human_g2=False, w04=False))
    return output / 'producer-receipt.json'


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    parser.add_argument('--output-parent', type=pathlib.Path, required=True)
    args = parser.parse_args(argv); output = args.output_parent.resolve()
    allowed = pathlib.Path('C:/NeuRotic/builds').resolve()
    need(output.is_relative_to(allowed) and output != allowed and not output.exists(), 'fresh external output directory required')
    try:
        if args.profile == 'r07':
            receipt = execute_campaign(output, 'r07')
        else:
            from run_controlled_producer import main as controlled_main
            output.mkdir(); completed = []
            def receive(capability, manifest, directory):
                key = 'L07:controlled D3D12 host:WARP_MECHANICS'
                result = next(row for row in manifest['results'] if row['obligation_id'] == key)
                obligation = next(row for row in manifest['obligations'] if row['id'] == key)
                completed.append(execute_campaign(output/'oracle', 'r03', (capability, result, obligation)))
            need(controlled_main(['--output-parent', str(output/'warp')], on_authenticated=receive) == 0 and len(completed) == 1,
                 'actual WARP prerequisite or composite oracle failed')
            receipt = completed[0]
        print(json.dumps(dict(receipt=str(receipt), result='PASS')))
        return 0
    except (OSError, ValueError, TypeError, KeyError, AttributeError, StopIteration) as error:
        output.mkdir(parents=True, exist_ok=True)
        put(output/'producer-failure.json', dict(error=str(error), authority_enrolled=False, result='INFRA_ERROR', human_g2=False, w04=False))
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
