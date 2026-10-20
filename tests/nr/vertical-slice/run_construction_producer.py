"""Execute registered CPU construction oracles and retain run-scoped authority.

Saved artifacts are audit material. Only this actual invocation passes its
in-memory capability to the existing evaluator; JSON cannot recreate authority.
"""
from __future__ import annotations
import argparse
import dataclasses
import hashlib
import json
import os
import pathlib
import sys
import uuid

from construction_profiles import PROFILES, COMPOSITES, COMPILE_PROFILE, COMPILE_CASES, MUTATIONS, members
from construction_producer import (TrustedConstructionRun, ROOT, VS, MSVC, CMD, HELPER,
    compile_command, compiler_oracle, substitute, verify_test_rows, verify_construction_binding,
    developer_environment_script, need, sha, supervised, classify_comparisons)
from owned_process import run as owned_run
from run_supervised_producer import git, working_tree, put

HERE = pathlib.Path(__file__).resolve().parent


class SemanticFailure(ValueError):
    pass


def manifest_bytes(document):
    # Write these exact bytes: text-mode newline conversion must not change the bound.
    return (json.dumps(document, indent=2) + '\n').encode('utf-8')


def batch_manifests(manifest, *, max_bytes=16 * 1024 * 1024):
    """Partition results without changing any obligation, result or proof descriptor."""
    need(type(max_bytes) is int and 0 < max_bytes <= 16 * 1024 * 1024, 'invalid manifest byte bound')
    obligations, results = manifest['obligations'], manifest['results']
    obligation_ids = {row['id'] for row in obligations}
    need(len(obligations) == len(obligation_ids) == 129 and results, 'complete original obligation set required')
    by_id = {row['id']: row for row in results}
    need(len(by_id) == len(results) and len({row['obligation_id'] for row in results}) == len(results),
         'duplicate or conflicting result/control identity')
    for row in results:
        need(row['obligation_id'] in obligation_ids and row['status'] == 'PASS' and
             row['started'] is True and row['oracle_met'] is True, 'nonpositive or unknown construction result')
        control = by_id.get(row['control_result_id'])
        need((row['mutant'] is False and row['control_result_id'] is None) or
             (row['mutant'] is True and control is not None and control['mutant'] is False),
             'missing or conflicting fresh baseline control')
    batches, current = [], []
    for row in results:
        group = [by_id[row['control_result_id']], row] if row['mutant'] else [row]
        current_ids = {value['id'] for value in current}
        candidate = current + [value for value in group if value['id'] not in current_ids]
        if len(manifest_bytes(dict(manifest, results=candidate))) > max_bytes:
            need(len(manifest_bytes(dict(manifest, results=group))) <= max_bytes,
                 'indivisible result/control proof exceeds manifest byte bound')
            if current:
                batches.append(dict(manifest, results=current))
            current = group
        else:
            current = candidate
    if current:
        batches.append(dict(manifest, results=current))
    need({row['id'] for batch in batches for row in batch['results']} == set(by_id),
         'batch construction lost or added results')
    return batches


def evaluate_manifest_batches(output, manifest, mapped, capabilities, evaluator, *, max_bytes=16 * 1024 * 1024):
    """Union only exact existing-evaluator acceptances, with no saved-file authority."""
    mapped = set(mapped)
    need({row['obligation_id'] for row in manifest['results']} == mapped, 'result set differs from reviewed mappings')
    obligations = {row['id'] for row in manifest['obligations']}
    receipts, authenticated, records = {}, set(), []
    for index, batch in enumerate(batch_manifests(manifest, max_bytes=max_bytes), 1):
        suffix = '-batch-' + str(index).zfill(3)
        path = output / ('producer-manifest.json' if index == 1 else 'producer-manifest' + suffix + '.json')
        payload = manifest_bytes(batch)
        with path.open('xb') as stream:
            stream.write(payload)
        need(path.stat().st_size == len(payload) <= max_bytes, 'serialized manifest exceeded its exact bound')
        expected_cells = {row['obligation_id'] for row in batch['results']}
        expected_ids = {row['id'] for row in batch['results']}
        evaluated = evaluator(path, trusted_runner_runs=capabilities)
        evaluation_path = output / ('producer-evaluation' + suffix + '.json')
        put(evaluation_path, evaluated)
        verified = evaluated['verified_runner_receipts']
        need(evaluated['structural_valid'] and not evaluated['structural_errors'] and
             len(verified) == len(expected_ids) and {row['result_id'] for row in verified} == expected_ids and
             set(evaluated['unsatisfied_obligations']) == obligations - expected_cells and
             not evaluated['human_g2_granted'] and not evaluated['w04_authorized'],
             'batch coverage escaped or missed exact reviewed construction cells')
        replay = evaluator(path)
        replay_path = output / ('untrusted-replay-evaluation' + suffix + '.json')
        put(replay_path, replay)
        need(replay['structural_valid'] and not replay['structural_errors'] and
             not replay['verified_runner_receipts'] and set(replay['unsatisfied_obligations']) == obligations and
             not replay['human_g2_granted'] and not replay['w04_authorized'], 'saved batch authority replay')
        for row in verified:
            need(row['result_id'] not in receipts or receipts[row['result_id']] == row,
                 'repeated baseline authentication conflicts across batches')
            receipts[row['result_id']] = row
        authenticated.update(expected_cells)
        records.append(dict(manifest=path.name, bytes=len(payload), sha256=sha(path),
            evaluation=evaluation_path.name, evaluation_sha256=sha(evaluation_path),
            untrusted_replay=replay_path.name, untrusted_replay_sha256=sha(replay_path),
            mapped_obligations=sorted(expected_cells)))
    need(authenticated == mapped and set(receipts) == {row['id'] for row in manifest['results']},
         'authenticated batch union differs from exact reviewed result set')
    return dict(kind='AUTHENTICATED_CONSTRUCTION_BATCH_UNION', structural_valid=True, structural_errors=[],
        evidence_state='SCOPE EVIDENCE INCOMPLETE', mapped_obligations=sorted(authenticated),
        verified_runner_receipts=list(receipts.values()), unsatisfied_obligations=sorted(obligations - authenticated),
        batches=records, human_g2_granted=False, w04_authorized=False, reusable_authority=False)


def snapshot():
    # Preserve raw bytes of the whole local source population, including every
    # include dependency and producer/verifier. Reviewed authorization remains
    # a separate code-pinned manifest checked by gate_evaluator.
    return {'OptiScaler/library/detours/detours.lib': sha(ROOT / 'OptiScaler/library/detours/detours.lib'), **{path.relative_to(ROOT).as_posix(): sha(path)
        for base in ('OptiScaler', 'tests/nr') for path in sorted((ROOT / base).rglob('*'))
        if path.is_file() and path.suffix.lower() in ('.h', '.hpp', '.c', '.cpp', '.py', '.ps1', '.json')}}


def invoke(output, run_id, name, command, environment, timeout, expected):
    intent = dict(command=command, cwd=str(ROOT), run_id=run_id, timeout_seconds=timeout)
    prefix = output / name
    prefix.parent.mkdir(parents=True, exist_ok=True)
    put(pathlib.Path(str(prefix) + '.intent.json'), intent)
    executable = pathlib.Path(command[0]).resolve()
    before = sha(executable)
    stdout = pathlib.Path(str(prefix) + '.stdout.log')
    stderr = pathlib.Path(str(prefix) + '.stderr.log')
    with stdout.open('xb') as out, stderr.open('xb') as err:
        process = owned_run(command, cwd=ROOT, env=environment, stdout=out, stderr=err, timeout=timeout)
    record = dict(intent=intent, returncode=process.returncode, supervision=process.supervision,
        infrastructure_error=process.infrastructure_error, executable_sha256_before=before,
        executable_sha256_after=sha(executable), stdout_sha256=sha(stdout), stderr_sha256=sha(stderr))
    put(pathlib.Path(str(prefix) + '.result.json'), record)
    accepted = (expected,) if type(expected) is int else expected
    supervised(process.supervision, process.returncode)
    need(process.returncode in accepted and not process.infrastructure_error and
         record['executable_sha256_after'] == before and
         process.supervision.get('supervisor_source_sha256') == sha(ROOT / HELPER),
         'owned stage failed: ' + name)
    return stdout, stderr


def overlay(output, profile, variant, pins):
    location = output / variant / 'overlay'
    definition = MUTATIONS[profile][variant]
    changes = {path: (before, after) for path, before, after in definition['changes']}
    need(len(changes) == len(definition['changes']) and set(changes).issubset(pins),
         'unregistered or repeated mutation source')
    for name in pins:
        content = (ROOT / name).read_bytes()
        if name in changes:
            content = substitute(content, *changes[name])
        target = location / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(content)
    put(output / variant / 'overlay-files.json', list(pins))
    return location


def execute(output, profile, mutation_names, run_id, pins):
    child = {key.upper(): value for key, value in os.environ.items()}
    child['VSTEL_EWDK'] = '1'
    child.pop('CL', None); child.pop('_CL_', None)
    launcher = output / 'environment.cmd'
    launcher.write_text(developer_environment_script(VS / 'VC/Auxiliary/Build/vcvarsall.bat'), encoding='utf-8')
    stdout, _ = invoke(output, run_id, 'setup', [str(CMD), '/d', '/c', str(launcher)], child, 60, 0)
    captured = {}
    for line in stdout.read_text(encoding='utf-8-sig').splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            captured[key.upper()] = value
    need(captured.get('VCTOOLSVERSION', '').strip('\\/') == '14.44.35207' and
         captured.get('VSCMD_ARG_TGT_ARCH') == 'x64', 'exact MSVC environment missing')
    child.update(captured)
    put(output / 'environment.json', captured)
    binaries, observed = {}, {}
    for variant in ('baseline', *mutation_names):
        directory = output / variant
        directory.mkdir(exist_ok=False)
        source_root = ROOT if variant == 'baseline' else overlay(output, profile, variant, pins)
        rows = {}
        if profile == COMPILE_PROFILE:
            for case, (ident, *_rest) in COMPILE_CASES.items():
                for control in (True, False):
                    name = 'authority-' + str(case) + ('-control' if control else '-negative')
                    command = compile_command(profile, '', directory, source_root, case=case, control=control)
                    out, err = invoke(output, run_id, variant + '/' + name, command, child, 1800, 0 if control else 2)
                    if not control:
                        from construction_profiles import COMPILE_SOURCE
                        need(compiler_oracle(case, ROOT / COMPILE_SOURCE,
                            out.read_text(encoding='utf-8-sig') + err.read_text(encoding='utf-8-sig')),
                            'compiler exit lacks exact intended negative: ' + name)
                rows[ident] = True
            rows['legitimate-api-controls-compile'] = True
            binaries[variant] = sha(MSVC / 'cl.exe')
        else:
            for suite in members(profile):
                label = variant + '/' + suite
                invoke(output, run_id, label + '-compile', compile_command(profile, suite, directory, source_root), child, 1800, 0)
                binary = directory / (suite + '.exe')
                prefix = suite + '/' if profile in COMPOSITES else ''
                failing = variant != 'baseline' and any(name.startswith(prefix)
                    for name in MUTATIONS[profile][variant]['detectors'])
                log, _ = invoke(output, run_id, label + '-test', [str(binary), run_id], child, 60, (0, 1))
                verified, _ = invoke(output, run_id, label + '-verify', [sys.executable, '-B',
                    str(HERE / 'verify_construction.py'), '--suite', suite, '--log', str(log),
                    '--run-dir', str(directory), '--run-id', run_id], child, 60, 0)
                selected, framing = verify_test_rows(profile, suite, log, directory, run_id,
                    json.loads(verified.read_text(encoding='utf-8-sig')))
                need(not set(rows).intersection(selected), 'duplicate comparison identity')
                actual_exit = json.loads((output / (label + '-test.result.json')).read_text())['returncode']
                need((framing['mismatches'] == 0) == (actual_exit == 0), 'framing/exit mismatch')
                rows.update(selected)
                binaries.setdefault(variant, sha(binary))
        detectors = () if variant == 'baseline' else MUTATIONS[profile][variant]['detectors']
        verdict = classify_comparisons(rows, 0 if all(rows.values()) else 1, detectors)
        put(directory / 'comparison-result.json', dict(result=verdict, assertion_ids=rows,
            expected_detectors=detectors, positive_control='THIS_RUN' if variant == 'baseline' else 'baseline'))
        if verdict not in ('PASS', 'KILLED'):
            raise SemanticFailure(variant + ': ' + verdict)
        observed[variant] = rows
        print(variant + ': ' + str(len(rows)) + ' authenticated comparison rows', flush=True)
    return binaries, observed


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=(*PROFILES, *COMPOSITES, COMPILE_PROFILE), required=True)
    parser.add_argument('--output-parent', type=pathlib.Path, required=True)
    parser.add_argument('--development', action='store_true', help='Retain focused command evidence without enrolling authority')
    args = parser.parse_args(argv)
    output = args.output_parent.resolve()
    external = pathlib.Path('C:/NeuRotic/builds').resolve()
    need(os.name == 'nt' and output.is_relative_to(external) and output != external and not output.exists(),
         'fresh external child of C:/NeuRotic/builds required')
    output.mkdir(parents=True)
    try:
        run_id = uuid.uuid4().hex
        commit, tree = git('rev-parse', 'HEAD').decode().strip(), working_tree()
        before = snapshot()
        tools = {str(path.resolve()): sha(path) for path in (CMD, pathlib.Path(sys.executable),
            VS / 'VC/Auxiliary/Build/vcvarsall.bat', *(MSVC / name for name in ('cl.exe', 'link.exe', 'lib.exe')))}
        put(output / 'sources-before.json', before); put(output / 'tools.json', tools)
        mutations = tuple(MUTATIONS.get(args.profile, {}))
        binaries, rows = execute(output, args.profile, mutations, run_id, before)
        after = snapshot(); put(output / 'sources-after.json', after)
        stable = before == after and tree == working_tree() and commit == git('rev-parse', 'HEAD').decode().strip()
        tool_stable = tools == {name: sha(pathlib.Path(name)) for name in tools}
        if args.development:
            put(output / 'development-receipt.json', dict(kind='EXECUTED_DEVELOPMENT_ONLY_NO_AUTHORITY',
                run_id=run_id, profile=args.profile, source_commit=commit, source_tree=tree,
                source_unchanged=stable, tools_unchanged=tool_stable, observations=rows,
                product_obligations_proven=[], human_g2_granted=False))
            return 0
        need(stable and tool_stable, 'source or tool changed; no authority enrolled')
        from gate_evaluator import SPECTRE_IDENTITIES, SPECTRE_REVIEWED_RUNNER_BINDINGS, evaluate_spectre_candidate
        mapped = {ident: mapping for ident, mapping in SPECTRE_REVIEWED_RUNNER_BINDINGS.items()
                  if mapping['suite'] == args.profile}
        need(mapped and all(mapping['source_sha256'] and all(before.get(name) == value
             for name, value in mapping['source_sha256'].items()) for mapping in mapped.values()),
             'no complete current reviewed mapping for this profile')
        env_id = 'sha256:' + sha(output / 'environment.json')
        config = 'sha256:' + hashlib.sha256(json.dumps(dict(profile=args.profile, mutations=mutations,
            setup_seconds=60, compile_seconds=1800, test_seconds=60, verifier_seconds=60,
            toolchain='MSVC14.44.35207-x64'), sort_keys=True).encode()).hexdigest()
        binding = dict(schema_version='NeuRotic.ConstructionBinding/1', run_id=run_id, profile=args.profile,
            python=sys.executable, source_commit=commit, source_tree=tree, environment_identity=env_id,
            config_identity=config, reusable_authority=False)
        put(output / 'construction-binding.json', binding)
        artifacts = tuple((path.relative_to(output).as_posix(), sha(path)) for path in sorted(output.rglob('*')) if path.is_file())
        authority = 'construction:' + run_id
        capability = TrustedConstructionRun(authority, 'ACTUAL_RUNNER', output, ROOT, run_id, args.profile,
            commit, tree, tuple(before.items()), tuple(tools.items()), env_id, config, artifacts, mutations)
        policy = json.loads((HERE / 'spectre_scope_policy.json').read_text())
        cases = json.loads((HERE / 'spectre_case_map_58.json').read_text())['cases']
        identities = dict.fromkeys(SPECTRE_IDENTITIES)
        identities.update(source_commit=commit, source_tree=tree, schema_identity='spectre-closure-v1-candidate',
            case_definition_identity=sha(HERE / 'spectre_case_map_58.json'), environment_identity=env_id, config_identity=config)
        obligations = []
        for case in cases:
            for layer in case['original_layers']:
                for axis in policy['layer_axes'][layer]:
                    ident = case['id'] + ':' + layer + ':' + axis
                    values = dict(identities)
                    if ident in mapped:
                        values['binary_sha256'] = binaries[mapped[ident]['mutation_names'][0] if mapped[ident].get('mutant') else 'baseline']
                    obligations.append(dict(id=ident, case_id=case['id'], axis=axis, mandatory=True, identities=values))
        results = []
        descriptors = [dict(path=name, sha256=value, kind='BINARY' if name.endswith(('.exe', '.obj')) else
            'LOG' if name.endswith('.log') else 'RESULT') for name, value in artifacts]
        for ident, mapping in mapped.items():
            values = next(item['identities'] for item in obligations if item['id'] == ident)
            mutant = mapping.get('mutant', False)
            control = mapping.get('control_obligation_id')
            results.append(dict(id=run_id + ':' + ident, obligation_id=ident, status='PASS',
                test_binding=mapping['test_binding'], producer=dict(kind='ACTUAL_RUNNER', identity=authority,
                    authenticated_binding_ref='construction-binding.json'), identities=values, started=True,
                oracle_met=True, reason=mapping['scope'], artifacts=descriptors, mutant=mutant,
                control_result_id=run_id + ':' + control if mutant and control else None))
        for result in results:
            obligation = next(item for item in obligations if item['id'] == result['obligation_id'])
            verified = verify_construction_binding(output, result, obligation, {authority: capability})
            need(verified['verified'], 'actual construction verification failed: ' + verified['reason'])
        manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
            case_map_sha256=sha(HERE / 'spectre_case_map_58.json'), scope_policy_sha256=sha(HERE / 'spectre_scope_policy.json'),
            obligations=obligations, results=results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)
        evaluation = evaluate_manifest_batches(output, manifest, mapped, {authority: capability},
                                               evaluate_spectre_candidate)
        put(output / 'producer-evaluation.json', evaluation)
        put(output / 'untrusted-replay-evaluation.json', dict(kind='UNTRUSTED_CONSTRUCTION_BATCH_UNION',
            batches=evaluation['batches'], verified_runner_receipts=[],
            unsatisfied_obligations=[row['id'] for row in obligations], human_g2_granted=False, w04_authorized=False))
        need(before == snapshot() and tree == working_tree() and
             commit == git('rev-parse', 'HEAD').decode().strip() and
             tools == {name: sha(pathlib.Path(name)) for name in tools},
             'source or tool changed during evaluator/replay; no final enrollment receipt')
        put(output / 'producer-receipt.json', dict(kind='ACTUAL_CONSTRUCTION_AUTHENTICATED_IN_PROCESS',
            run_id=run_id, source_commit=commit, source_tree=tree, profile=args.profile, mapped_obligations=list(mapped),
            source_unchanged=True, observations=rows, batches=evaluation['batches'],
            reusable_authority=False, human_g2_granted=False))
        return 0
    except SemanticFailure as error:
        put(output / 'producer-semantic-failure.json', dict(result=str(error),
            reusable_authority=False, human_g2_granted=False))
        print(str(error), file=sys.stderr, flush=True)
        return 1
    except Exception as error:
        put(output / 'producer-infrastructure-error.json', dict(result='INFRA_ERROR', error=str(error),
            reusable_authority=False, human_g2_granted=False))
        print('INFRA_ERROR: ' + str(error), file=sys.stderr, flush=True)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
