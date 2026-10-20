"""Own fresh provider-capacity and C03 executions; saved files grant no authority."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
import pathlib
import sys
import uuid

from provider_producer import (TrustedProviderCampaign, PROVIDER_BINDINGS, DRIVER, ROOT, VS, MSVC, CMD,
    adapter, need, sha, verify_provider_binding)
from run_construction_producer import snapshot, execute, invoke, SemanticFailure
from run_supervised_producer import git, working_tree, put

HERE = pathlib.Path(__file__).resolve().parent


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output-parent', required=True, type=pathlib.Path)
    parser.add_argument('--development', action='store_true')
    args = parser.parse_args(argv)
    output = args.output_parent.resolve(); external = pathlib.Path('C:/NeuRotic/builds').resolve()
    need(os.name == 'nt' and output.is_relative_to(external) and output != external and not output.exists(), 'fresh external provider output required')
    output.mkdir(parents=True)
    try:
        run_id = uuid.uuid4().hex
        commit, tree = git('rev-parse', 'HEAD').decode().strip(), working_tree()
        before = snapshot()
        paths = (CMD, pathlib.Path(sys.executable), VS / 'VC/Auxiliary/Build/vcvarsall.bat',
            *(MSVC / name for name in ('cl.exe', 'link.exe', 'lib.exe')),
            *(pathlib.Path('C:/Windows/System32') / name for name in ('d3d12.dll', 'dxgi.dll', 'd3d10warp.dll')),
            ROOT / 'OptiScaler/library/detours/detours.lib')
        tools = {str(path.resolve()): sha(path) for path in paths}
        put(output / 'sources-before.json', before); put(output / 'tools.json', tools)
        execute(output, 'native-same-recording', (), run_id, before)
        child = {key.upper(): value for key, value in os.environ.items()}
        child['VSTEL_EWDK'] = '1'; child.pop('CL', None); child.pop('_CL_', None)
        command = [sys.executable, '-B', str(ROOT / DRIVER), '--mutations', '--output-parent', str(output / 'provider-run')]
        invoke(output, run_id, 'provider-launch', command, child, 3600, (0, 1))
        receipts = json.loads((output / 'provider-run/receipt.json').read_text())
        if any(value['result'] == 'INFRA_ERROR' for value in receipts):
            raise ValueError('provider child reported INFRA_ERROR')
        if any(value['result'] not in ('PASS', 'KILLED') for value in receipts):
            raise SemanticFailure('; '.join(value['case'] + ':' + value['result'] for value in receipts if value['result'] not in ('PASS', 'KILLED')))
        after = snapshot(); put(output / 'sources-after.json', after)
        stable = before == after and commit == git('rev-parse', 'HEAD').decode().strip() and tree == working_tree()
        tool_stable = tools == {name: sha(pathlib.Path(name)) for name in tools}
        if args.development:
            put(output / 'development-receipt.json', dict(kind='EXECUTED_PROVIDER_DEVELOPMENT_NO_AUTHORITY', run_id=run_id,
                source_unchanged=stable, tools_unchanged=tool_stable, cases={r['case']: r['result'] for r in receipts},
                product_obligations_proven=[], human_g2_granted=False))
            return 0
        need(stable and tool_stable, 'provider source/tool drift')
        from gate_evaluator import SPECTRE_IDENTITIES, SPECTRE_REVIEWED_RUNNER_BINDINGS, evaluate_spectre_candidate
        need(all(SPECTRE_REVIEWED_RUNNER_BINDINGS[ident]['source_sha256'] and
             all(before.get(name) == value for name, value in SPECTRE_REVIEWED_RUNNER_BINDINGS[ident]['source_sha256'].items())
             for ident in PROVIDER_BINDINGS), 'reviewed provider closure unavailable')
        env_id = 'sha256:' + sha(output / 'environment.json')
        observation = adapter((output / 'provider-run/baseline/test.stdout.log').read_text())
        driver_id = 'WARP:sha256:' + hashlib.sha256(observation.encode()).hexdigest()
        config = 'sha256:' + hashlib.sha256(json.dumps(dict(profile='provider-capacity-composite',
            c03_positive=True, campaign_cases=[r['case'] for r in receipts], launcher_timeout_seconds=3600,
            contract='SYNTHETIC_CONTROLLED_CALLER_LIST', toolchain='MSVC14.44.35207-x64'), sort_keys=True).encode()).hexdigest()
        put(output / 'provider-binding.json', dict(schema_version='NeuRotic.ProviderCampaignBinding/1', run_id=run_id,
            source_commit=commit, source_tree=tree, environment_identity=env_id, driver_identity=driver_id,
            config_identity=config, python=sys.executable, reusable_authority=False))
        artifacts = tuple((path.relative_to(output).as_posix(), sha(path)) for path in sorted(output.rglob('*')) if path.is_file())
        authority = 'provider-composite:' + run_id
        capability = TrustedProviderCampaign(authority, 'ACTUAL_RUNNER', output, ROOT, run_id, commit, tree,
            tuple(before.items()), tuple(tools.items()), env_id, driver_id, config, artifacts)
        policy = json.loads((HERE / 'spectre_scope_policy.json').read_text())
        cases = json.loads((HERE / 'spectre_case_map_58.json').read_text())['cases']
        identities = dict.fromkeys(SPECTRE_IDENTITIES)
        identities.update(source_commit=commit, source_tree=tree, schema_identity='spectre-closure-v1-candidate',
            case_definition_identity=sha(HERE / 'spectre_case_map_58.json'), environment_identity=env_id,
            driver_identity=driver_id, config_identity=config)
        obligations = []
        for case in cases:
            for layer in case['original_layers']:
                for axis in policy['layer_axes'][layer]:
                    ident = case['id'] + ':' + layer + ':' + axis; values = dict(identities)
                    if ident in PROVIDER_BINDINGS:
                        values['binary_sha256'] = receipts[-1 if PROVIDER_BINDINGS[ident]['mutant'] else 0]['binary_sha256']
                    obligations.append(dict(id=ident, case_id=case['id'], axis=axis, mandatory=True, identities=values))
        descriptors = [dict(path=name, sha256=value, kind='BINARY' if name.endswith('.exe') else
            'LOG' if name.endswith('.log') else 'RESULT') for name, value in artifacts]
        results = []
        for ident, mapping in PROVIDER_BINDINGS.items():
            obligation = next(row for row in obligations if row['id'] == ident)
            result = dict(id=run_id + ':' + ident, obligation_id=ident, status='PASS', test_binding=mapping['test_binding'],
                producer=dict(kind='ACTUAL_RUNNER', identity=authority, authenticated_binding_ref='provider-binding.json'),
                identities=obligation['identities'], started=True, oracle_met=True,
                reason='Actual controlled WARP capacity and complete semantic campaign; original CPU axis retained. Contract is synthetic; Feature18/proprietary provider NOT_RUN.',
                artifacts=descriptors, mutant=mapping['mutant'], control_result_id=run_id + ':L12:unit:CPU' if mapping['mutant'] else None)
            verified = verify_provider_binding(output, result, obligation, {authority: capability})
            need(verified['verified'], 'provider authentication refused: ' + verified['reason'])
            results.append(result)
        manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
            case_map_sha256=sha(HERE / 'spectre_case_map_58.json'), scope_policy_sha256=sha(HERE / 'spectre_scope_policy.json'),
            obligations=obligations, results=results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)
        put(output / 'producer-manifest.json', manifest)
        evaluated = evaluate_spectre_candidate(output / 'producer-manifest.json', trusted_runner_runs={authority: capability})
        put(output / 'producer-evaluation.json', evaluated)
        need(evaluated['structural_valid'] and not evaluated['structural_errors'] and
             len(evaluated['verified_runner_receipts']) == 2 and len(evaluated['unsatisfied_obligations']) == 127 and
             not set(PROVIDER_BINDINGS).intersection(evaluated['unsatisfied_obligations']), 'provider mapping exceeds or misses exact L12 cells')
        replay = evaluate_spectre_candidate(output / 'producer-manifest.json')
        put(output / 'untrusted-replay-evaluation.json', replay)
        need(not replay['verified_runner_receipts'] and len(replay['unsatisfied_obligations']) == 129, 'saved provider authority replay')
        need(before == snapshot() and tree == working_tree() and commit == git('rev-parse', 'HEAD').decode().strip() and
             tools == {name: sha(pathlib.Path(name)) for name in tools}, 'provider final source/tool epoch changed')
        put(output / 'producer-receipt.json', dict(kind='ACTUAL_PROVIDER_COMPOSITE_AUTHENTICATED_IN_PROCESS',
            run_id=run_id, source_commit=commit, source_tree=tree, mapped_obligations=list(PROVIDER_BINDINGS),
            source_unchanged=True, reusable_authority=False, human_g2_granted=False))
        return 0
    except SemanticFailure as error:
        put(output / 'producer-semantic-failure.json', dict(result=str(error), reusable_authority=False)); return 1
    except Exception as error:
        put(output / 'producer-infrastructure-error.json', dict(result='INFRA_ERROR', error=str(error), reusable_authority=False))
        print('INFRA_ERROR: ' + str(error), file=sys.stderr, flush=True); return 2


if __name__ == '__main__':
    raise SystemExit(main())
