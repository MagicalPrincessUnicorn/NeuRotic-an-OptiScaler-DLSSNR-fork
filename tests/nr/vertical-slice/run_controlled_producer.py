"""Execute a fresh L07 WARP campaign and evaluate its run-scoped authority.

No file can recreate the authority. This command owns the five real executions
and calls the existing evaluator in the same process after source/tool checks.
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

from controlled_producer import (L07_MUTANTS, PRIMARY_MUTANT, CONTROLLED_OBLIGATIONS, TrustedControlledCampaign,
    environment, need, sha, supervised, verify_controlled_binding)
from gate_evaluator import (SPECTRE_IDENTITIES, SPECTRE_REVIEWED_RUNNER_BINDINGS,
                            evaluate_spectre_candidate)
from run_supervised_producer import HERE, ROOT, MSVC, VS, CMD, git, put, working_tree
from owned_process import run as owned_run

BASELINE = 'L07:controlled D3D12 host:WARP_MECHANICS'
MUTATION = 'L07:mutation:CPU'
MAPPED = (*CONTROLLED_OBLIGATIONS, MUTATION)


def source_snapshot():
    names = SPECTRE_REVIEWED_RUNNER_BINDINGS[BASELINE]['source_sha256']
    need(names, 'reviewed L07 source closure is unavailable')
    result = {name: sha(ROOT / name) for name in names}
    need(result == names, 'current source differs from reviewed L07 closure')
    return result


def main(argv=None, *, on_authenticated=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-parent', required=True, type=pathlib.Path)
    args = parser.parse_args(argv)
    output = args.output_parent.resolve()
    need(os.name == 'nt' and output.is_relative_to(pathlib.Path(r'C:\NeuRotic\builds')) and
         output != pathlib.Path(r'C:\NeuRotic\builds') and not output.exists(),
         'fresh external directory below C:\\NeuRotic\\builds required')
    output.mkdir(parents=True)
    try:
        campaign = uuid.uuid4().hex
        commit = git('rev-parse', 'HEAD').decode().strip()
        tree = working_tree()
        before = source_snapshot()
        tool_paths = [CMD, pathlib.Path(sys.executable), VS / 'VC/Auxiliary/Build/vcvarsall.bat',
                      *(MSVC / name for name in ('cl.exe', 'link.exe', 'lib.exe')),
                      *(pathlib.Path(r'C:\Windows\System32') / name for name in
                        ('d3d12.dll', 'dxgi.dll', 'd3d10warp.dll'))]
        tools = {str(path.resolve()): sha(path) for path in tool_paths}
        put(output / 'sources-before.json', before)
        put(output / 'tools.json', tools)
        driver = {name: value for name, value in tools.items() if name.lower().endswith('.dll')}
        driver_identity = 'WARP-system-libraries:sha256:' + hashlib.sha256(
            json.dumps(driver, sort_keys=True).encode()).hexdigest()
        config = 'sha256:' + hashlib.sha256(json.dumps(dict(
            runner='owned-continuation', baseline=True, mutations=L07_MUTANTS,
            setup_seconds=60, compile_seconds=1800, test_seconds=60,
            toolchain='MSVC14.44.35207-x64', adapter='WARP'), sort_keys=True).encode()).hexdigest()
        child_environment = dict(os.environ)
        child_environment['VSTEL_EWDK'] = '1'
        for name in list(child_environment):
            if name.upper() in ('CL', '_CL_'):
                del child_environment[name]
        for case in ('baseline', *L07_MUTANTS):
            command = [sys.executable, '-B', str(ROOT / 'tests/nr/spectre/run_owned_continuation.py'),
                       '--output-parent', str(output / case)]
            if case != 'baseline':
                command += ['--mutation', case]
            with (output / (case + '-launcher.stdout.log')).open('xb') as stdout, \
                 (output / (case + '-launcher.stderr.log')).open('xb') as stderr:
                run = owned_run(command, cwd=ROOT, env=child_environment, stdout=stdout, stderr=stderr, timeout=2100)
            put(output / (case + '-launcher.json'), dict(command=command, cwd=str(ROOT),
                returncode=run.returncode, infrastructure_error=run.infrastructure_error,
                timeout_seconds=2100, supervision=run.supervision))
            supervised(run.supervision, 0)
            need(run.supervision.get('supervisor_source_sha256') == before['tests/nr/spectre/owned_process.py'],
                 'outer launcher imported a different supervisor')
            need(run.returncode == 0 and not run.infrastructure_error, 'owned campaign child failed: ' + case)
            print(case + ': owned execution completed', flush=True)
        after = source_snapshot()
        put(output / 'sources-after.json', after)
        need(before == after and tree == working_tree() and commit == git('rev-parse', 'HEAD').decode().strip(),
             'source changed during actual campaign; no authority enrolled')
        need(tools == {name: sha(pathlib.Path(name)) for name in tools},
             'tool changed during actual campaign; no authority enrolled')
        observation = environment((output / 'baseline/test.stdout.log').read_text(encoding='utf-8-sig'))
        environment_identity = 'sha256:' + hashlib.sha256(observation.encode()).hexdigest()
        binding = dict(schema_version='NeuRotic.ControlledCampaignBinding/1', campaign_id=campaign,
            source_commit=commit, source_tree=tree, environment_identity=environment_identity,
            driver_identity=driver_identity, config_identity=config, environment_observation=observation,
            physical_substrate='WARP_CONTROLLED_COPY', canonical_mutation_axis='CPU',
            cases=['baseline', *L07_MUTANTS], reusable_authority=False)
        put(output / 'campaign-binding.json', binding)
        artifact_hashes = tuple((path.relative_to(output).as_posix(), sha(path))
            for path in sorted(output.rglob('*')) if path.is_file())
        authority = 'controlled-host:' + campaign
        capability = TrustedControlledCampaign(authority, 'ACTUAL_RUNNER', output, ROOT, campaign,
            commit, tree, tuple(before.items()), tuple(tools.items()), environment_identity,
            driver_identity, config, artifact_hashes)
        policy = json.loads((HERE / 'spectre_scope_policy.json').read_text())
        cases = json.loads((HERE / 'spectre_case_map_58.json').read_text())['cases']
        case_sha = sha(HERE / 'spectre_case_map_58.json')
        identities = dict.fromkeys(SPECTRE_IDENTITIES)
        identities.update(source_commit=commit, source_tree=tree,
            schema_identity='spectre-closure-v1-candidate', case_definition_identity=case_sha,
            environment_identity=environment_identity, driver_identity=driver_identity, config_identity=config)
        obligations = []
        for case in cases:
            for layer in case['original_layers']:
                for axis in policy['layer_axes'][layer]:
                    ident = f"{case['id']}:{layer}:{axis}"
                    values = dict(identities)
                    if ident in MAPPED:
                        values['binary_sha256'] = sha(output / (PRIMARY_MUTANT if ident == MUTATION else 'baseline') / 'test.exe')
                    obligations.append(dict(id=ident, case_id=case['id'], axis=axis, mandatory=True, identities=values))
        artifacts = [dict(path=name, sha256=value, kind='BINARY' if name.endswith('.exe') else
                         'LOG' if name.endswith('.log') else 'RESULT') for name, value in artifact_hashes]
        results = []
        for ident in MAPPED:
            values = next(row['identities'] for row in obligations if row['id'] == ident)
            results.append(dict(id=campaign + ':' + ident,
                obligation_id=ident, status='PASS', test_binding=SPECTRE_REVIEWED_RUNNER_BINDINGS[ident]['test_binding'],
                producer=dict(kind='ACTUAL_RUNNER', identity=authority, authenticated_binding_ref='campaign-binding.json'),
                identities=values, started=True, oracle_met=True,
                reason='Actual controlled software D3D12/WARP signal/wait, blocked consumer and exact pixel readback; '
                       + ('same-campaign baseline; exact required owned-copy/recording oracle rows.' if ident != MUTATION else
                          'four exact Wait mutants killed with matching positive controls. Original mutation axis remains CPU; physical execution is WARP.'),
                artifacts=artifacts, control_result_id=campaign + ':' + BASELINE if ident == MUTATION else None,
                mutant=ident == MUTATION))
        for result in results:
            obligation = next(row for row in obligations if row['id'] == result['obligation_id'])
            verified = verify_controlled_binding(output, result, obligation, {authority: capability})
            need(verified['verified'], 'actual controlled receipt failed verification: ' + verified['reason'])
        negative_checks = {}
        baseline_result = results[0]
        baseline_obligation = next(row for row in obligations if row['id'] == BASELINE)
        for name, changed in {
                'test_only_authority': dataclasses.replace(capability, origin='TEST_ONLY'),
                'stale_source': dataclasses.replace(capability, source_tree='0' * 40),
                'cross_environment': dataclasses.replace(capability, environment_identity='another-environment'),
                'cross_tool': dataclasses.replace(capability, tool_hashes=((str(CMD), '0' * 64),)),
                }.items():
            negative_checks[name] = not verify_controlled_binding(output, baseline_result,
                baseline_obligation, {authority: changed})['verified']
        negative_checks['copied_directory'] = not verify_controlled_binding(output.parent,
            baseline_result, baseline_obligation, {authority: capability})['verified']
        negative_checks['serialized_authority'] = not verify_controlled_binding(output,
            baseline_result, baseline_obligation, {authority: dataclasses.asdict(capability)})['verified']
        need(all(negative_checks.values()), 'authority isolation negative failed')
        put(output / 'authority-negatives.json', negative_checks)
        manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
            case_map_sha256=case_sha, scope_policy_sha256=sha(HERE / 'spectre_scope_policy.json'),
            obligations=obligations, results=results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)
        put(output / 'producer-manifest.json', manifest)
        evaluation = evaluate_spectre_candidate(output / 'producer-manifest.json', trusted_runner_runs={authority: capability})
        put(output / 'producer-evaluation.json', evaluation)
        need(evaluation['structural_valid'] and not evaluation['structural_errors'] and
             len(evaluation['verified_runner_receipts']) == len(MAPPED) and
             len(evaluation['unsatisfied_obligations']) == 129 - len(MAPPED) and
             all(name not in evaluation['unsatisfied_obligations'] for name in MAPPED) and
             not evaluation['human_g2_granted'] and not evaluation['w04_authorized'],
             'controlled coverage did not remain the exact reviewed original obligations')
        replay = evaluate_spectre_candidate(output / 'producer-manifest.json')
        put(output / 'untrusted-replay-evaluation.json', replay)
        need(not replay['verified_runner_receipts'] and len(replay['unsatisfied_obligations']) == 129,
             'exported campaign unexpectedly enrolled reusable authority')
        if on_authenticated is not None:
            on_authenticated(capability, manifest, output)
            need(before == source_snapshot() and tree == working_tree() and
                 commit == git('rev-parse', 'HEAD').decode().strip() and
                 tools == {name: sha(pathlib.Path(name)) for name in tools},
                 'source/tool changed during in-process evidence callback')
        put(output / 'producer-receipt.json', dict(kind='ACTUAL_OWNED_WARP_CAMPAIGN_AUTHENTICATED_IN_PROCESS',
            campaign_id=campaign, source_commit=commit, source_tree=tree, source_files=before,
            tool_hashes=tools, environment_observation=observation, driver_identity=driver_identity,
            mapped_obligations=list(MAPPED), original_obligations=129,
            unsatisfied_obligations=129-len(MAPPED), physical_substrate='WARP_CONTROLLED_COPY',
            canonical_mutation_axis='CPU', source_files_unchanged=True,
            exported_files_can_enroll_authority=False, human_g2=False, w04=False))
        print(json.dumps(dict(output=str(output), campaign_id=campaign, mapped_obligations=list(MAPPED),
                              unsatisfied_obligations=129-len(MAPPED))))
        return 0
    except (OSError, ValueError, TypeError, KeyError, AttributeError) as error:
        put(output / 'producer-failure.json', dict(error=str(error), authority_enrolled=False,
            product_obligations_proven=0, human_g2=False, w04=False))
        print(str(error), file=sys.stderr)
        return 2


if __name__ == '__main__':
    raise SystemExit(main())
