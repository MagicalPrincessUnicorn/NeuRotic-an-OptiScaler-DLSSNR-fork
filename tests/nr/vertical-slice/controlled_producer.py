"""Authenticate an owned L07 WARP campaign; exported files never enroll authority.

The original mutation cell is CPU-labelled by the immutable scope policy. Its
physical execution substrate here is explicitly software D3D12/WARP.
"""
from __future__ import annotations

import dataclasses
import hashlib
import json
import pathlib
import re

L07_ASSERTIONS = (
    'L07 unsubmitted producer cannot establish cross queue order',
    'L07 wrong exact content or producer ticket refuses before wait',
    'L07 different authentic producer ticket refuses before queue wait',
    'L07 sealed unique producer establishes actual second queue wait before completion',
    'L07 second queue edge binds current recording and exact prior content',
    'L07 every admitted producer read waits on exact owner signal fence value and consumer queue',
    'L07 actual consumer queue waits for exact blocked producer fence value',
    'L07 actual cross queue completion readback and all-ticket retirement succeed',
    'L07 producer queue cannot impersonate distinct consumer queue',
    'L07 unknown device refuses without queue wait',
    'L07 actual E_FAIL from exact consumer Wait refuses admission',
    'L07 actual successful Wait permits retry after earlier refusal',
)
L07_MUTANTS = ('wrong_wait_value', 'wrong_wait_fence', 'wrong_wait_queue', 'ignore_wait_failure')
CONTROLLED_ASSERTIONS = {
    'L07': L07_ASSERTIONS,
    'O05': ('actual upload records source content revision',
            'genuine identity preserves existing content version without commands',
            'Identity refuses a distinct current destination without content or edge changes'),
    'L04': ('recorded commands are not completion or release',
            'reset negative has actual pending commands',
            'observed reset cancels unsubmitted recording without readback success',
            'retired reset generation cannot record another edge',
            'unsubmitted host destruction actually resets recording before releasing physical owners'),
    'L08': ('owned list closes executes and signals once',
            'duplicate submission refuses through public host API',
            'real fence completion and footprint row pitches yield exact two-copy pixel equality',
            'replay negative has actual submitted copied content without owner seal',
            'completed replayable recording cannot grant readback or physical release',
            'replay invalidates unique completion and retains holds',
            'actual reset after all replay completions permits release without qualified readback'),
    'F03': ('revision control records real initial write',
            'second actual destination write advances canonical content revision',
            'published view snapshot stays immutable across later owned writes',
            'old exact content version cannot authorize a later edge',
            'fresh version extends owned chain before authentic reset cancellation'),
    'L06': ('actual upload records source content revision',
            'first real copy records exact owned edge', 'second real owned copy extends chain',
            'second edge consumes exact first destination version on same recording',
            'readback command reserves exact final content', 'owned list closes executes and signals once',
            'real fence completion and footprint row pitches yield exact two-copy pixel equality',
            'D3D12 debug layer reports no error or corruption for controlled commands'),
}
CONTROLLED_OBLIGATIONS = tuple(case + ':controlled D3D12 host:WARP_MECHANICS'
                              for case in CONTROLLED_ASSERTIONS)
PRIMARY_MUTANT = 'wrong_wait_fence'
INTENDED_FAILURE = {
    'wrong_wait_value': L07_ASSERTIONS[3],
    'wrong_wait_fence': L07_ASSERTIONS[3],
    'wrong_wait_queue': L07_ASSERTIONS[5],
    'ignore_wait_failure': L07_ASSERTIONS[10],
}
WAIT = 'const auto result = consumer->Wait(point.timeline->fence.Get(), point.value);'
MUTATIONS = {
    'wrong_wait_value': (WAIT, 'const auto result = consumer->Wait(point.timeline->fence.Get(), 0);'),
    'wrong_wait_fence': (WAIT, 'ComPtr<ID3D12Fence> substituted; if(FAILED(producerDevice->CreateFence(point.value,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&substituted))))return false; const auto result = consumer->Wait(substituted.Get(), point.value);'),
    'wrong_wait_queue': (WAIT, 'ComPtr<ID3D12CommandQueue> substituted; auto description=consumer->GetDesc(); if(FAILED(consumerDevice->CreateCommandQueue(&description,IID_PPV_ARGS(&substituted))))return false; static std::vector<ComPtr<ID3D12CommandQueue>> held; held.push_back(substituted); const auto result = substituted->Wait(point.timeline->fence.Get(), point.value);'),
    'ignore_wait_failure': ('return SUCCEEDED(result);\n}\nbool BindExternalWait', 'return true;\n}\nbool BindExternalWait'),
}


def need(condition, message):
    if not condition:
        raise ValueError(message)


def sha(path):
    need(path.is_file() and not path.is_symlink(), 'regular non-symlink file required')
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(block)
    return result.hexdigest()


def supervised(record, expected_exit):
    need(all(record.get(key) is True for key in (
        'process_created', 'user_code_started', 'group_assigned_before_resume',
        'parent_exit_observed', 'group_empty_observed', 'capture_complete')) and
        all(record.get(key) is False for key in ('timed_out', 'termination_requested')) and
        type(record.get('numeric_exit_code')) is int and record['numeric_exit_code'] == expected_exit and
        type(record.get('last_active_processes')) is int and record['last_active_processes'] == 0 and
        type(record.get('process_id')) is int and record['process_id'] > 0 and
        record.get('errors') == [] and record.get('cleanup_errors') == [],
        'failed, timed out, or incompletely supervised command')


def assertions(text, *, allow_failures=False):
    rows = {}
    for line in text.splitlines():
        if not line.startswith('ASSERT '):
            continue
        row = json.loads(line[7:])
        need(set(row) == {'name', 'matched'} and isinstance(row['name'], str) and row['name'] and
             row['name'] not in rows and type(row['matched']) is bool, 'invalid or duplicate assertion')
        rows[row['name']] = row['matched']
    summaries = re.findall(r'^owned-continuation checks=(\d+) failed=(\d+); controlled mechanics/readback only; no NR/provider/game/BeforeFg/seal$', text, re.MULTILINE)
    need(len(summaries) == 1 and rows, 'one complete assertion summary required')
    count, failures = map(int, summaries[0])
    need(count == len(rows) and failures == sum(not value for value in rows.values()),
         'raw assertion and summary counts differ')
    need(allow_failures or failures == 0, 'assertion failed')
    return rows


def environment(text):
    rows = [line for line in text.splitlines() if line.startswith('environment=')]
    need(len(rows) == 1 and rows[0].startswith('environment=WARP_CONTROLLED adapter=') and
         ' vendor=5140 device=140 ' in rows[0], 'exact software D3D12 adapter identity required')
    need(re.search(r' luid=-?\d+:\d+ driver_query_hresult=-?\d+(?: driver=\d+\.\d+\.\d+\.\d+)?$', rows[0]),
         'adapter/driver observation incomplete')
    return rows[0]


@dataclasses.dataclass(frozen=True)
class TrustedControlledCampaign:
    authority_identity: str
    origin: str
    run_directory: pathlib.Path
    repository: pathlib.Path
    campaign_id: str
    source_commit: str
    source_tree: str
    source_hashes: tuple[tuple[str, str], ...]
    tool_hashes: tuple[tuple[str, str], ...]
    environment_identity: str
    driver_identity: str
    config_identity: str
    artifact_hashes: tuple[tuple[str, str], ...]


class Reader:
    def __init__(self, root, trust, artifacts):
        self.root = pathlib.Path(root).resolve()
        self.pins = dict(trust.artifact_hashes)
        self.claimed = {row['path']: row['sha256'] for row in artifacts}
        need(len(self.pins) == len(trust.artifact_hashes) and self.pins and
             len(self.claimed) == len(artifacts), 'duplicate artifact pins')

    def path(self, name):
        need(isinstance(name, str) and name and '\\' not in name and ':' not in name and
             not pathlib.PurePosixPath(name).is_absolute() and
             all(part not in ('.', '..') for part in pathlib.PurePosixPath(name).parts), 'invalid artifact reference')
        path = self.root / name
        need(not any(part.is_symlink() for part in (path, *path.parents)) and
             path.resolve().is_relative_to(self.root) and name in self.pins and
             self.claimed.get(name) == self.pins[name] == sha(path), 'artifact differs from owned campaign')
        return path

    def document(self, name):
        path = self.path(name)
        need(path.stat().st_size <= 16 * 1024 * 1024, 'receipt too large')
        return json.loads(path.read_text(encoding='utf-8-sig'))


def exact_mutation(repository, directory, name, reader):
    """Verify intended isolated implementation mutation plus include relocation only."""
    source = repository / 'OptiScaler/dlssnr/NrGpuSafety.cpp'
    test = repository / 'tests/nr/spectre/owned_continuation.cpp'
    before, after = MUTATIONS[name]
    original = source.read_text()
    need(original.count(before) == 1, 'mutation target not unique')
    overlay = reader.path(name + '/overlay/NrGpuSafety.cpp')
    expected = original.replace(before, after).replace('"NrGpuSafety.h"', '"' + source.with_suffix('.h').as_posix() + '"')
    need(overlay.read_text() == expected, 'not the reviewed single semantic mutation')
    compiled = reader.path(name + '/test.cpp')
    expected_test = test.read_text().replace('"../../../OptiScaler/dlssnr/NrGpuSafety.cpp"', '"' + overlay.as_posix() + '"')
    need(compiled.read_text() == expected_test, 'mutant changed the comparison oracle')
    return compiled


def verify_controlled_binding(root, result, obligation, trusted_runs, *, allow_test_only=False):
    outcome = dict(verified=False, product_obligation_proven=False, reason='', origin=None,
                   suite='owned-continuation', assertion_ids=[], physical_substrate='WARP_CONTROLLED_COPY')
    try:
        producer = result['producer']
        trust = trusted_runs.get(producer['identity'])
        need(producer['kind'] == 'ACTUAL_RUNNER' and type(trust) is TrustedControlledCampaign and
             trust.authority_identity == producer['identity'], 'no independently enrolled controlled authority')
        need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'),
             'test-only capability cannot authenticate product evidence')
        root = pathlib.Path(root).resolve()
        need(not pathlib.Path(trust.run_directory).is_symlink() and root.samefile(trust.run_directory),
             'copied directory cannot reuse authority')
        need(re.fullmatch('[0-9a-f]{32}', trust.campaign_id) and
             re.fullmatch('[0-9a-f]{40}', trust.source_commit) and
             re.fullmatch('[0-9a-f]{40}', trust.source_tree), 'malformed source/campaign identity')
        mutant = result['mutant']
        need(obligation['id'] == 'L07:mutation:CPU' if mutant else
             obligation['id'] in CONTROLLED_OBLIGATIONS,
             'controlled campaign cannot authenticate another original layer')
        reader = Reader(root, trust, result['artifacts'])
        binding = reader.document(producer['authenticated_binding_ref'])
        need(binding['schema_version'] == 'NeuRotic.ControlledCampaignBinding/1' and
             binding['campaign_id'] == trust.campaign_id, 'binding version/campaign mismatch')
        for field in ('source_commit', 'source_tree', 'environment_identity', 'driver_identity', 'config_identity'):
            need(binding[field] == getattr(trust, field) == result['identities'][field] == obligation['identities'][field],
                 'stale or cross-source/environment identity: ' + field)
        source_pins = dict(trust.source_hashes)
        tool_pins = dict(trust.tool_hashes)
        need(source_pins and len(source_pins) == len(trust.source_hashes) and tool_pins and
             len(tool_pins) == len(trust.tool_hashes), 'missing or duplicate source/tool pins')
        for reference in ('sources-before.json', 'sources-after.json'):
            need(reader.document(reference) == source_pins, 'source closure changed during campaign')
        need(reader.document('tools.json') == tool_pins and all(sha(pathlib.Path(path)) == value
             for path, value in tool_pins.items()), 'tool changed during campaign')
        need(all(sha(trust.repository / path) == value for path, value in source_pins.items()),
             'current source differs from enrolled campaign')
        cases = ('baseline', *L07_MUTANTS)
        observed = {}
        for case in cases:
            receipt = reader.document(case + '/receipt.json')
            observed[case] = verify_case(reader, trust, case, receipt)
        baseline = observed['baseline']
        need(set(L07_ASSERTIONS).issubset(baseline['rows']) and all(baseline['rows'].values()),
             'baseline omitted or failed original L07 oracle')
        required = L07_ASSERTIONS if mutant else CONTROLLED_ASSERTIONS[obligation['case_id']]
        need(set(required).issubset(baseline['rows']), 'baseline omitted the requested original oracle')
        need(all(item['environment'] == baseline['environment'] for item in observed.values()),
             'cross-environment mutation control')
        need(binding['environment_observation'] == baseline['environment'] and
             trust.environment_identity == 'sha256:' + hashlib.sha256(baseline['environment'].encode()).hexdigest(),
             'adapter observation differs from enrolled identity')
        for name in L07_MUTANTS:
            need(observed[name]['rows'].get(INTENDED_FAILURE[name]) is False and
                 baseline['rows'].get(INTENDED_FAILURE[name]) is True, 'mutant lacks its same-campaign positive control')
        primary = observed[PRIMARY_MUTANT if mutant else 'baseline']['binary_sha256']
        need(primary == result['identities']['binary_sha256'] == obligation['identities']['binary_sha256'],
             'primary actual executable identity differs')
        outcome.update(verified=True, origin=trust.origin, assertion_ids=list(baseline['rows']),
            source_files=source_pins, campaign_id=trust.campaign_id, mutation_names=list(L07_MUTANTS) if mutant else [],
            baseline_binary_sha256=baseline['binary_sha256'], environment_observation=baseline['environment'],
            reason='actual controlled WARP assertions and same-campaign semantic controls authenticated')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, IndexError, RecursionError, OverflowError) as error:
        outcome['reason'] = str(error)
    return outcome


def verify_case(reader, trust, case, receipt):
    """Final runner stage checks are kept beside the raw oracle verifier."""
    need(receipt['mutation'] == (None if case == 'baseline' else case) and
         receipt['evidence_layer'] == 'WARP_CONTROLLED_COPY' and
         receipt['result'] == ('PASS' if case == 'baseline' else 'KILLED'), 'case failed or has wrong scope/mutation')
    pins = dict(trust.source_hashes)
    helper = 'tests/nr/spectre/owned_process.py'
    need(receipt['source_unchanged'] is True and
         receipt['runner_sha256'] == pins['tests/nr/spectre/run_owned_continuation.py'] and
         receipt['process_helper_sha256'] == pins[helper], 'runner/source closure mismatch')
    snapshots = [reader.document(case + '/source-' + phase + '.json') for phase in ('before', 'after')]
    need(snapshots[0] == snapshots[1] and all(snapshots[0].get(name) == value for name, value in pins.items()
         if pathlib.Path(name).suffix.lower() in ('.h', '.hpp', '.c', '.cpp', '.py', '.ps1', '.json')),
         'runner source snapshots disagree with independently enrolled source')
    launcher = reader.document(case + '-launcher.json')
    supervised(launcher['supervision'], 0)
    need(launcher['returncode'] == 0 and launcher['infrastructure_error'] is False and
         launcher['timeout_seconds'] == launcher['supervision']['timeout_seconds'] == 2100 and
         launcher['supervision'].get('supervisor_source_sha256') == pins[helper],
         'outer launcher failed or imported a different supervisor')
    stages = {stage['label']: stage for stage in receipt['stages']}
    need(len(receipt['stages']) == len(stages), 'duplicate stage')
    need(set(stages) == {'setup', 'compile', 'test'}, 'complete supervised stage closure required')
    binary = reader.path(case + '/test.exe')
    expected_source = trust.repository / 'tests/nr/spectre/owned_continuation.cpp'
    if case != 'baseline':
        expected_source = exact_mutation(trust.repository, reader.root / case, case, reader)
    for role, stage in stages.items():
        expected_exit = 1 if role == 'test' and case != 'baseline' else 0
        supervised(stage['supervision'], expected_exit)
        need(stage['supervision'].get('supervisor_source_sha256') == pins[helper],
             'stage imported a different supervisor')
        need(stage['returncode'] == expected_exit and stage['cwd'] == str(trust.repository) and
             stage['timeout_seconds'] == stage['supervision']['timeout_seconds'] ==
             (1800 if role == 'compile' else 60), 'stage exit/cwd/bound differs')
        command = stage['command']
        need(isinstance(command, list) and command and all(isinstance(arg, str) for arg in command), 'invalid stage command')
        executable = pathlib.Path(stage['executable']).resolve()
        expected_sha = sha(binary) if role == 'test' else {pathlib.Path(name): value for name, value in trust.tool_hashes}.get(executable)
        need(stage['infrastructure_error'] is False and expected_sha and
             stage['executable_sha256_before'] == expected_sha ==
             stage['executable_sha256_after'], 'stage executable not pinned before and after')
        for stream in ('stdout', 'stderr'):
            capture = pathlib.Path(stage[stream + '_path'])
            need(capture.resolve().parent == (reader.root / case).resolve(), 'capture relocated')
            need(sha(reader.path(case + '/' + capture.name)) == stage[stream + '_sha256'], 'capture hash differs')
        if role == 'test':
            need(command == [str(binary)] and executable == binary.resolve(), 'wrong actual executable/adapter arguments')
        elif role == 'compile':
            need(str(expected_source) in command and '/Fe' + str(binary) in command and
                 receipt['compile_command'] == command, 'compiler source/output differs')
    stdout = reader.path(case + '/' + pathlib.Path(stages['test']['stdout_path']).name).read_text(encoding='utf-8-sig')
    stderr = reader.path(case + '/' + pathlib.Path(stages['test']['stderr_path']).name).read_text(encoding='utf-8-sig')
    text = stdout + stderr
    rows = assertions(text, allow_failures=case != 'baseline')
    expected_count = {'baseline': 100, 'wrong_wait_value': 57, 'wrong_wait_fence': 57,
                      'wrong_wait_queue': 72, 'ignore_wait_failure': 72}[case]
    need(len(rows) == expected_count and receipt['assertion_protocol_valid'] is True and
         receipt['assertions'] == [dict(name=name, matched=value) for name, value in rows.items()] and
         receipt['failed_assertions'] == [name for name, value in rows.items() if not value] and
         [line[5:] for line in stderr.splitlines() if line.startswith('FAIL ')] == receipt['failed_assertions'],
         'raw assertions differ from original evaluated rows')
    need(receipt['binary_sha256'] == sha(binary), 'receipt binary hash differs')
    if case != 'baseline':
        need(rows.get(INTENDED_FAILURE[case]) is False, 'nonzero exit lacks intended semantic detector')
    return dict(rows=rows, environment=environment(text), binary_sha256=sha(binary))
