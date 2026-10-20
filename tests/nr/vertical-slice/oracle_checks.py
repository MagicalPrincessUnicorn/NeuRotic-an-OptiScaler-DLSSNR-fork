"""Executed comparison bodies for R03/R07; inner data never claims a product run."""
from __future__ import annotations

import argparse
import copy
import dataclasses
import hashlib
import json
import pathlib
import sys
import tempfile
import types

from oracle_producer import PROFILES

HERE = pathlib.Path(__file__).resolve().parent


def load_subject(path, profile):
    # Execute the exact isolated bytes, while repository-relative data lookups
    # keep their original source location. The physical compile filename remains
    # the isolated source in tracebacks and in the supervising command receipt.
    module = types.ModuleType('nr_oracle_subject')
    module.__file__ = str(HERE / pathlib.Path(PROFILES[profile]['subject']).name)
    sys.modules[module.__name__] = module
    exec(compile(path.read_text(encoding='utf-8'), str(path), 'exec'), module.__dict__)
    return module


def r07(subject, check):
    cases_raw = (HERE / 'spectre_case_map_58.json').read_bytes()
    policy_raw = (HERE / 'spectre_scope_policy.json').read_bytes()
    cases = json.loads(cases_raw)['cases']; policy = json.loads(policy_raw)
    case_sha = hashlib.sha256(cases_raw).hexdigest()
    identities = dict.fromkeys(subject.SPECTRE_IDENTITIES, 'fixture-only')
    identities.update(source_commit='a'*40, source_tree='b'*40, binary_sha256='c'*64,
        schema_identity='spectre-closure-v1-candidate', case_definition_identity=case_sha)
    with tempfile.TemporaryDirectory(prefix='nr-r07-evaluator-fixture-') as directory:
        root = pathlib.Path(directory)
        artifact = root / 'fixture.txt'; artifact.write_text('TEST_ONLY evaluator data; no model/provider/game execution\n')
        descriptor = dict(path='fixture.txt', sha256=hashlib.sha256(artifact.read_bytes()).hexdigest(), kind='LOG')
        obligations = [dict(id=f"{case['id']}:{layer}:{axis}", case_id=case['id'], axis=axis,
            mandatory=True, identities=copy.deepcopy(identities)) for case in cases
            for layer in case['original_layers'] for axis in policy['layer_axes'][layer]]
        results = [dict(id='fixture:'+row['id'], obligation_id=row['id'], status='PASS',
            test_binding='R07 explicit evaluator fixture positive',
            producer=dict(identity='fixture', kind='TEST_ONLY', authenticated_binding_ref=None),
            identities=copy.deepcopy(identities), started=True, oracle_met=True,
            reason='TEST_ONLY evaluator input', artifacts=[copy.deepcopy(descriptor)],
            control_result_id=None, mutant=False) for row in obligations]
        baseline = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
            case_map_sha256=case_sha, scope_policy_sha256=hashlib.sha256(policy_raw).hexdigest(),
            obligations=obligations, results=results, human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)

        def evaluate(document, fixture=True):
            path = root / 'manifest.json'; path.write_text(json.dumps(document), encoding='utf-8')
            return subject.evaluate_spectre_candidate(path, fixture_mode=fixture)

        complete = evaluate(baseline)
        normal = evaluate(baseline, False)
        check('r07_fixture_complete_only', complete['structural_valid'] and
            complete['evidence_state'] == 'FIXTURE SCOPE COMPLETE' and not complete['unsatisfied_obligations'] and
            normal['evidence_state'] == 'SCOPE EVIDENCE INCOMPLETE' and len(normal['unsatisfied_obligations']) == 129)
        missing_checks = []
        for index in range(len(results)):
            value = copy.deepcopy(baseline); removed = value['results'].pop(index)
            outcome = evaluate(value)
            missing_checks.append(outcome['structural_valid'] and outcome['evidence_state'] == 'SCOPE EVIDENCE INCOMPLETE'
                and outcome['unsatisfied_obligations'] == [removed['obligation_id']])
        check('r07_missing_result_retains_axis', len(missing_checks) == 129 and all(missing_checks))
        retained = []
        for status in ('NOT_RUN', 'BLOCKED', 'INFRA_ERROR', 'INCONCLUSIVE', 'FAIL'):
            value = copy.deepcopy(baseline); first = value['results'][0]
            first.update(status=status, started=status not in ('NOT_RUN', 'BLOCKED'),
                oracle_met=False if status == 'FAIL' else None, reason='specified '+status)
            outcome = evaluate(value); key = first['obligation_id']
            retained.append(key in outcome['unsatisfied_obligations'] and
                outcome['result_statuses'][key] == [status] and outcome['result_reasons'][key] == ['specified '+status]
                and outcome['evidence_state'] == 'SCOPE EVIDENCE INCOMPLETE')
        check('r07_nonpass_statuses_retained', all(retained))
        identity_checks = []
        for field in ('source_commit', 'source_tree', 'environment_identity', 'config_identity', 'binary_sha256'):
            for kind in ('unknown', 'stale'):
                value = copy.deepcopy(baseline)
                if kind == 'unknown':
                    value['obligations'][0]['identities'][field] = None
                else:
                    value['results'][0]['identities'][field] = 'd'*40 if field in ('source_commit', 'source_tree') else 'd'*64 if field == 'binary_sha256' else 'another-fixture'
                outcome = evaluate(value)
                identity_checks.append(value['obligations'][0]['id'] in outcome['unsatisfied_obligations'])
        check('r07_required_identity_refused', all(identity_checks))
        conflict = copy.deepcopy(baseline); failure = copy.deepcopy(conflict['results'][0])
        failure.update(id='prior-failure', status='FAIL', oracle_met=False, reason='actual invariant failed')
        conflict['results'].insert(0, failure); outcome = evaluate(conflict)
        check('r07_conflicting_failure_retained', outcome['result_statuses'][failure['obligation_id']] == ['FAIL', 'PASS']
            and failure['obligation_id'] in outcome['unsatisfied_obligations'])
        check('r07_no_machine_gate', all(value['human_g2_granted'] is False and value['w04_authorized'] is False
            for value in (complete, normal, outcome)))
        expected_ids = {prefix+f'{number:02}' for prefix, count in [('N',12),('O',12),('L',15),('F',12),('R',7)]
                        for number in range(1, count+1)}
        check('r07_original_inventory_exact', len(cases) == 58 and {case['id'] for case in cases} == expected_ids and
            len(obligations) == len({row['id'] for row in obligations}) == 129 and
            case_sha == '08a375c7fe6abd3424477fc489421f5ad8025fad5b5919dfbfcb5811835e55a2' and
            policy['case_map_sha256'] == case_sha)
        # Drop BOTH records: orphan-result validation must not mask a removed
        # mandatory-set guard in the semantic mutation campaign.
        pair_checks = []
        for index in range(len(obligations)):
            value = copy.deepcopy(baseline); value['obligations'].pop(index); value['results'].pop(index)
            outcome = evaluate(value)
            pair_checks.append(not outcome['structural_valid'] and outcome['evidence_state'] == 'SCOPE EVIDENCE INCOMPLETE')
        check('r07_mandatory_pair_cannot_be_dropped', len(pair_checks) == 129 and all(pair_checks))


def r03(subject, path, check):
    payload = json.loads(path.read_text(encoding='utf-8'))
    if payload['kind'] != 'TEST_ONLY_POLICY_INPUT_FROM_ACTUAL_WARP':
        raise ValueError('R03 requires explicitly test-only policy input')
    values = payload['capability']
    if values['origin'] != 'TEST_ONLY':
        raise ValueError('a serialized context cannot enroll actual authority')
    values['run_directory'] = pathlib.Path(values['run_directory'])
    values['repository'] = pathlib.Path(values['repository'])
    for key in ('source_hashes', 'tool_hashes', 'artifact_hashes'):
        values[key] = tuple(tuple(row) for row in values[key])
    trust = subject.TrustedControlledCampaign(**values)
    result = payload['result']; obligation = payload['obligation']

    def verify(candidate, target, fixture=True):
        return subject.verify_controlled_binding(trust.run_directory, candidate, target,
            {trust.authority_identity: trust}, allow_test_only=fixture)

    baseline = verify(result, obligation)
    check('r03_warp_receipt_mechanics_only', baseline['verified'] and baseline['origin'] == 'TEST_ONLY' and
        baseline['physical_substrate'] == 'WARP_CONTROLLED_COPY' and baseline['product_obligation_proven'] is False and
        all(result['identities'][key] is None for key in ('provider_identity', 'model_identity', 'game_identity')))
    for name, axis in [('r03_hardware_relabel_refused', 'HARDWARE_MECHANICS'), ('r03_model_relabel_refused', 'LIVE_GAME')]:
        changed = copy.deepcopy(result); target = copy.deepcopy(obligation)
        target.update(id='R04:live GPU/game/G2:'+axis, case_id='R04', axis=axis)
        changed['obligation_id'] = target['id']
        check(name, not verify(changed, target)['verified'])
    check('r03_synthetic_origin_refused', not verify(result, obligation, False)['verified'])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--profile', choices=PROFILES, required=True)
    parser.add_argument('--run-id', required=True)
    parser.add_argument('--subject', type=pathlib.Path, required=True)
    parser.add_argument('--warp-fixture', type=pathlib.Path)
    parser.add_argument('--syntax-only', action='store_true')
    args = parser.parse_args()
    if args.syntax_only:
        compile(args.subject.read_text(encoding='utf-8'), str(args.subject), 'exec')
        return 0
    subject = load_subject(args.subject, args.profile); rows = []

    def check(name, actual):
        if name not in PROFILES[args.profile]['assertions'] or any(row['id'] == name for row in rows):
            raise ValueError('unregistered or duplicate oracle comparison')
        row = dict(kind='oracle-assertion', run_id=args.run_id, id=name, matched=bool(actual))
        rows.append(row); print(json.dumps(row), flush=True)

    if args.profile == 'r07': r07(subject, check)
    else: r03(subject, args.warp_fixture, check)
    if set(row['id'] for row in rows) != set(PROFILES[args.profile]['assertions']):
        raise ValueError('incomplete comparison execution')
    failures = sum(not row['matched'] for row in rows)
    print(json.dumps(dict(kind='oracle-summary', run_id=args.run_id, profile=args.profile,
        checks=len(rows), failures=failures, scope=PROFILES[args.profile]['scope'])), flush=True)
    return 1 if failures else 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except Exception as error:
        print('ORACLE_INFRA_ERROR: '+repr(error), file=sys.stderr)
        raise SystemExit(2)
