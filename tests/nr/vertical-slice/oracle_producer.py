"""Run-scoped authentication of executed TEST-003 evaluator/scope oracles.

R07's inner positive is explicitly synthetic evaluator data. R03 additionally
requires the original in-memory capability for a fresh actual WARP campaign.
No exported document can recreate either capability or grant a human gate.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import pathlib
import re

from controlled_producer import Reader, need, sha, supervised

R07_ASSERTIONS = (
    'r07_fixture_complete_only', 'r07_missing_result_retains_axis',
    'r07_nonpass_statuses_retained', 'r07_required_identity_refused',
    'r07_conflicting_failure_retained', 'r07_no_machine_gate',
    'r07_original_inventory_exact', 'r07_mandatory_pair_cannot_be_dropped',
)
R03_ASSERTIONS = (
    'r03_warp_receipt_mechanics_only', 'r03_hardware_relabel_refused',
    'r03_model_relabel_refused', 'r03_synthetic_origin_refused',
)
PROFILES = {
    'r07': dict(assertions=R07_ASSERTIONS, mutations=('drop_mandatory_cell', 'ignore_required_identity', 'grant_machine_g2'),
                subject='tests/nr/vertical-slice/gate_evaluator.py', scope='EVALUATOR_TEST_ONLY', physical='CPU_EVALUATOR_FIXTURE'),
    'r03': dict(assertions=R03_ASSERTIONS, mutations=('accept_cross_layer', 'accept_synthetic_origin'),
                subject='tests/nr/vertical-slice/controlled_producer.py', scope='POLICY_TEST_ON_AUTHENTICATED_WARP', physical='WARP_CONTROLLED_COPY'),
}
MUTATIONS = {
    'drop_mandatory_cell': dict(before="need(set(obligations) == set(expected), 'Spectre original case/layer obligation missing')",
        after="need(True, 'Spectre original case/layer obligation missing')", detector='r07_mandatory_pair_cannot_be_dropped'),
    'ignore_required_identity': dict(before="if any(not text(result['identities'][field]) or",
        after="if False and any(not text(result['identities'][field]) or", detector='r07_required_identity_refused'),
    'grant_machine_g2': dict(before="'whole_series_missing': [], 'human_g2_granted': False,",
        after="'whole_series_missing': [], 'human_g2_granted': True,", detector='r07_no_machine_gate'),
    'accept_cross_layer': dict(before="need(obligation['id'] == 'L07:mutation:CPU' if mutant else\n             obligation['id'] in CONTROLLED_OBLIGATIONS,",
        after="need(True,", secondary=("CONTROLLED_ASSERTIONS[obligation['case_id']]",
            "CONTROLLED_ASSERTIONS.get(obligation['case_id'], L07_ASSERTIONS)"), detector='r03_hardware_relabel_refused'),
    'accept_synthetic_origin': dict(before="need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'),",
        after="need(trust.origin in ('ACTUAL_RUNNER', 'TEST_ONLY'),", detector='r03_synthetic_origin_refused'),
}
ORACLE_BINDINGS = {
    'R07:unit:CPU': dict(profile='r07', suite='spectre-python-oracles', test_binding='R07.unit.required-evidence/v1',
        assertion_ids=R07_ASSERTIONS, physical_substrate='CPU_EVALUATOR_FIXTURE'),
    'R07:mutation:CPU': dict(profile='r07', suite='spectre-python-oracles', test_binding='R07.mutation.required-evidence/v1',
        assertion_ids=R07_ASSERTIONS, physical_substrate='CPU_EVALUATOR_FIXTURE', mutant=True,
        mutation_names=PROFILES['r07']['mutations']),
    'R07:structural:SOURCE_ANALYSIS': dict(profile='r07', suite='spectre-python-oracles', test_binding='R07.structural.original-scope/v1',
        assertion_ids=R07_ASSERTIONS, physical_substrate='CPU_EVALUATOR_FIXTURE'),
    'R03:controlled D3D12 host:WARP_MECHANICS': dict(profile='r03', suite='spectre-python-oracles', test_binding='R03.controlled.mechanics-scope/v1',
        assertion_ids=R03_ASSERTIONS, physical_substrate='WARP_CONTROLLED_COPY'),
    'R03:mutation:CPU': dict(profile='r03', suite='spectre-python-oracles', test_binding='R03.mutation.scope-promotion/v1',
        assertion_ids=R03_ASSERTIONS, physical_substrate='WARP_CONTROLLED_COPY', mutant=True,
        mutation_names=PROFILES['r03']['mutations']),
}


def parse_assertions(raw, run_id, profile):
    rows = {}; summaries = []
    for line in raw.splitlines():
        row = json.loads(line)
        need(isinstance(row, dict) and row.get('run_id') == run_id, 'raw row run identity differs')
        if row.get('kind') == 'oracle-assertion':
            need(set(row) == {'kind', 'run_id', 'id', 'matched'} and row['id'] not in rows and
                 type(row['matched']) is bool, 'duplicate or invalid raw comparison')
            rows[row['id']] = row['matched']
        else:
            need(row.get('kind') == 'oracle-summary', 'unknown raw row kind')
            summaries.append(row)
    need(set(rows) == set(PROFILES[profile]['assertions']) and len(summaries) == 1,
         'exact nonempty comparison set and one summary required')
    summary = summaries[0]
    need(set(summary) == {'kind', 'run_id', 'profile', 'checks', 'failures', 'scope'} and
         summary['profile'] == profile and summary['scope'] == PROFILES[profile]['scope'] and
         type(summary['checks']) is int and summary['checks'] == len(rows) and
         type(summary['failures']) is int and summary['failures'] == sum(not v for v in rows.values()),
         'raw comparisons and summary disagree')
    return rows


def classify(profile, mutation, exit_code, rows):
    if exit_code not in (0, 1) or set(rows) != set(PROFILES[profile]['assertions']):
        return 'INFRA_ERROR'
    failures = sum(not value for value in rows.values())
    if (exit_code == 0) != (failures == 0):
        return 'INFRA_ERROR'
    if mutation is None:
        return 'PASS' if failures == 0 else 'FAIL'
    return 'KILLED' if exit_code == 1 and rows[MUTATIONS[mutation]['detector']] is False else 'SURVIVED'


def mutated_source(repository, profile, mutation):
    original = (repository / PROFILES[profile]['subject']).read_text(encoding='utf-8-sig')
    if mutation is None:
        return original
    change = MUTATIONS[mutation]
    need(mutation in PROFILES[profile]['mutations'] and original.count(change['before']) == 1,
         'reviewed semantic mutation anchor is not unique')
    altered = original.replace(change['before'], change['after'])
    if 'secondary' in change:
        before, after = change['secondary']
        need(altered.count(before) == 1, 'reviewed scope dispatch mutation is not unique')
        altered = altered.replace(before, after)
    return altered


@dataclasses.dataclass(frozen=True)
class TrustedPythonOracleRun:
    authority_identity: str
    origin: str
    run_directory: pathlib.Path
    repository: pathlib.Path
    run_id: str
    profile: str
    source_commit: str
    source_tree: str
    source_hashes: tuple[tuple[str, str], ...]
    tool_hashes: tuple[tuple[str, str], ...]
    environment_identity: str
    config_identity: str
    artifact_hashes: tuple[tuple[str, str], ...]
    # Original owner object and original result/obligation, never loaded from JSON.
    controlled_context: tuple | None = None


def stage_command(python, checks, profile, run_id, subject, fixture, syntax):
    result = [str(python), '-B', str(checks), '--profile', profile, '--run-id', run_id,
              '--subject', str(subject)]
    if fixture is not None:
        result += ['--warp-fixture', str(fixture)]
    if syntax:
        result += ['--syntax-only']
    return result


def warp_fixture_document(context):
    capability, result, obligation = context
    values = dataclasses.asdict(capability)
    values['origin'] = 'TEST_ONLY'
    return json.loads(json.dumps(dict(kind='TEST_ONLY_POLICY_INPUT_FROM_ACTUAL_WARP',
        capability=values, result=result, obligation=obligation), default=str))


def verify_oracle_binding(root, result, obligation, trusted_runs, *, allow_test_only=False):
    outcome = dict(verified=False, product_obligation_proven=False, reason='', origin=None,
                   suite='spectre-python-oracles', assertion_ids=[])
    try:
        producer = result['producer']; trust = trusted_runs.get(producer['identity'])
        need(producer['kind'] == 'ACTUAL_RUNNER' and type(trust) is TrustedPythonOracleRun and
             trust.authority_identity == producer['identity'], 'no independently owned Python oracle capability')
        need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'),
             'fixture capability cannot authenticate a live oracle run')
        root = pathlib.Path(root).resolve()
        need(not pathlib.Path(trust.run_directory).is_symlink() and root.samefile(trust.run_directory),
             'copied oracle directory cannot reuse authority')
        need(re.fullmatch('[0-9a-f]{32}', trust.run_id) and re.fullmatch('[0-9a-f]{40}', trust.source_commit)
             and re.fullmatch('[0-9a-f]{40}', trust.source_tree), 'invalid run/source identity')
        mapping = ORACLE_BINDINGS[obligation['id']]
        need(mapping['profile'] == trust.profile and result['mutant'] == mapping.get('mutant', False)
             and result['test_binding'] == mapping['test_binding'], 'wrong original Python oracle layer')
        reader = Reader(root, trust, result['artifacts'])
        binding = reader.document(producer['authenticated_binding_ref'])
        need(binding['schema_version'] == 'NeuRotic.PythonOracleBinding/1' and
             binding['run_id'] == trust.run_id and binding['profile'] == trust.profile, 'binding profile/run mismatch')
        for key in ('source_commit', 'source_tree', 'environment_identity', 'config_identity'):
            need(binding[key] == getattr(trust, key) == result['identities'][key] == obligation['identities'][key],
                 'source/environment/config identity mismatch: ' + key)
        sources = dict(trust.source_hashes); tools = dict(trust.tool_hashes)
        need(sources and tools and len(sources) == len(trust.source_hashes) and len(tools) == len(trust.tool_hashes),
             'missing or duplicated source/tool pins')
        need(reader.document('sources-before.json') == reader.document('sources-after.json') == sources and
             all(sha(trust.repository / path) == value for path, value in sources.items()), 'source drift')
        need(reader.document('tools.json') == tools and all(sha(pathlib.Path(p)) == h for p, h in tools.items()), 'tool drift')
        python = pathlib.Path(binding['python_executable'])
        need(str(python) in tools and tools[str(python)] == sha(reader.path('python.exe')) ==
             result['identities']['binary_sha256'] == obligation['identities']['binary_sha256'], 'actual Python executable identity differs')
        need(trust.environment_identity == 'sha256:' + sha(reader.path('environment.json')), 'environment capture differs')
        if trust.profile == 'r03':
            from controlled_producer import TrustedControlledCampaign, verify_controlled_binding
            need(trust.controlled_context is not None and len(trust.controlled_context) == 3, 'actual WARP capability absent')
            warp, warp_result, warp_obligation = trust.controlled_context
            need(type(warp) is TrustedControlledCampaign and warp.origin == 'ACTUAL_RUNNER', 'actual WARP owner required')
            proof = verify_controlled_binding(warp.run_directory, warp_result, warp_obligation,
                {warp.authority_identity: warp})
            need(proof['verified'] and proof['physical_substrate'] == 'WARP_CONTROLLED_COPY', 'actual WARP receipt failed revalidation')
            need(warp.source_commit == trust.source_commit and warp.source_tree == trust.source_tree and
                 result['identities']['driver_identity'] == obligation['identities']['driver_identity'] == warp.driver_identity,
                 'WARP/CPU oracle source or driver mismatch')
            need(all(result['identities'][field] is None for field in ('provider_identity', 'model_identity', 'game_identity')),
                 'mechanics evidence cannot declare provider/model/game identity')
            need(reader.document('warp-fixture.json') == warp_fixture_document(trust.controlled_context),
                 'policy fixture differs from the original in-memory WARP capability')
        observed = {}
        checks = trust.repository / 'tests/nr/vertical-slice/oracle_checks.py'
        fixture = reader.path('warp-fixture.json') if trust.profile == 'r03' else None
        helper_sha = sources['tests/nr/spectre/owned_process.py']
        attempts = set()
        for case in ('baseline', *PROFILES[trust.profile]['mutations']):
            mutation = None if case == 'baseline' else case
            subject = reader.path(case + '/subject.py')
            need(subject.read_text(encoding='utf-8') == mutated_source(trust.repository, trust.profile, mutation),
                 'isolated source is not the exact reviewed mutation')
            stages = {}
            for role in ('syntax', 'execute'):
                item = reader.document(case + '/' + role + '.json')
                need(reader.document(case + '/' + role + '.intent.json') == {key: item[key] for key in
                     ('run_id', 'attempt_id', 'command', 'cwd', 'timeout_seconds')}, 'recorded command intent differs')
                need(item['run_id'] == trust.run_id and item['attempt_id'] not in attempts, 'run/attempt replay')
                attempts.add(item['attempt_id'])
                expected = stage_command(python, checks, trust.profile, trust.run_id, subject, fixture, role == 'syntax')
                need(item['command'] == item['supervision']['command'] == expected and
                     item['cwd'] == item['supervision']['cwd'] == str(root) and
                     item['timeout_seconds'] == item['supervision']['timeout_seconds'] == 90 and
                     item['supervision']['supervisor_source_sha256'] == helper_sha, 'exact oracle command or supervisor differs')
                expected_exit = 0 if role == 'syntax' or mutation is None else 1
                need(item['returncode'] == expected_exit and not item['infrastructure_error'], 'syntax/execute infrastructure or unexpected exit')
                supervised(item['supervision'], expected_exit)
                for stream in ('stdout', 'stderr'):
                    need(item[stream]['path'] == case + '/' + role + '.' + stream + '.log' and
                         item[stream]['sha256'] == sha(reader.path(item[stream]['path'])), 'capture mismatch')
                stages[role] = item
            raw = reader.path(stages['execute']['stdout']['path']).read_text(encoding='utf-8')
            rows = parse_assertions(raw, trust.run_id, trust.profile)
            need(classify(trust.profile, mutation, stages['execute']['returncode'], rows) == ('PASS' if mutation is None else 'KILLED'),
                 'missing positive control or intended semantic detector')
            observed[case] = rows
        baseline = observed['baseline']
        need(all(baseline.values()), 'baseline oracle failed')
        outcome.update(verified=True, origin=trust.origin, assertion_ids=list(baseline), source_files=sources,
            physical_substrate=PROFILES[trust.profile]['physical'], campaign_id=trust.run_id,
            mutation_names=list(PROFILES[trust.profile]['mutations']) if result['mutant'] else [],
            reason='Owned Python oracle comparisons and exact semantic controls; inner evaluator data remains fixture-only')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, RecursionError, OverflowError) as error:
        outcome['reason'] = str(error)
    return outcome
