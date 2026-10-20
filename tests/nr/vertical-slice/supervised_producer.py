"""Verify CPU TEST002 receipts against independently enrolled runner evidence.

TrustedRunnerRun is a host capability, not a document format. The host must retain
these pins from its own supervised run, independently of the candidate bundle.
There is deliberately no JSON loader, CLI trust flag, or auto-enrollment from
run-owner.json. Hashes alone cannot establish who produced an evidence directory.
No product obligation mapping or authority issuer is implemented by this module.
"""
from __future__ import annotations

import contextlib
import dataclasses
import datetime
import hashlib
import importlib.util
import io
import pathlib
import re


@dataclasses.dataclass(frozen=True)
class TrustedRunnerRun:
    authority_identity: str
    origin: str
    run_directory: pathlib.Path
    run_id: str
    source_commit: str
    source_tree: str
    source_hashes: tuple[tuple[str, str], ...]
    tool_hashes: tuple[tuple[str, str], ...]
    environment_identity: str
    environment_sha256: str
    config_identity: str
    artifact_hashes: tuple[tuple[str, str], ...]


_SPEC = importlib.util.spec_from_file_location(
    '_supervised_test002_framing',
    pathlib.Path(__file__).resolve().parents[1] / 'contracts/test002_source_and_evidence.py')
_FRAMING = importlib.util.module_from_spec(_SPEC)
_SPEC.loader.exec_module(_FRAMING)


def _need(condition, message):
    if not condition:
        raise ValueError(message)


def _sha(path):
    _need(path.is_file() and not path.is_symlink(), 'regular non-symlink file required')
    result = hashlib.sha256()
    with path.open('rb') as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b''):
            result.update(chunk)
    return result.hexdigest()


def _pins(items):
    result = dict(items)
    _need(len(result) == len(items) and result and all(
        isinstance(key, str) and re.fullmatch('[0-9a-f]{64}', value)
        for key, value in result.items()), 'invalid or duplicate trusted pins')
    return result


def _when(value):
    result = datetime.datetime.fromisoformat(value.replace('Z', '+00:00'))
    _need(result.tzinfo is not None, 'command timestamp lacks timezone')
    return result


def _path_pins(items):
    pins = _pins(items)
    result = {pathlib.Path(path).resolve(): sha for path, sha in pins.items()}
    _need(len(result) == len(pins) and all(pathlib.Path(path).is_absolute() for path in pins),
          'duplicate or relative enrolled source/tool path')
    return result


def _same_path(left, right):
    left, right = pathlib.Path(left), pathlib.Path(right)
    # The Windows supervisor records volume-GUID paths. pathlib preserves an
    # extended-path prefix after resolving these, unlike a drive-letter path.
    if left.exists() and right.exists():
        return left.samefile(right)
    return left.resolve() == right.resolve()


class _ReceiptReader:
    def __init__(self, root, artifacts, trusted):
        self.root = root
        self.pins = _pins(trusted.artifact_hashes)
        self.claimed = {item['path']: item['sha256'] for item in artifacts}
        _need(len(self.claimed) == len(artifacts), 'duplicate artifact descriptor')

    def path(self, name):
        _need(isinstance(name, str) and name and '\\' not in name and '/' not in name and
              ':' not in name and name not in ('.', '..'), 'receipt must be an immediate child')
        path = self.root / name
        _need(not path.is_symlink() and path.resolve().parent == self.root,
              'receipt outside original run directory')
        _need(name in self.pins and name in self.claimed and
              _sha(path) == self.pins[name] == self.claimed[name],
              'receipt differs from independently enrolled bytes')
        return path

    def document(self, name):
        return _FRAMING.load_json(_FRAMING.bounded_text(self.path(name)))

    def hash_receipt(self, receipt, expected_path, enrolled_sha):
        _need(isinstance(receipt, dict) and receipt.get('state') == 'Recorded' and
              receipt.get('error') is None and receipt.get('sha256') == enrolled_sha and
              pathlib.Path(receipt['path']).resolve() == expected_path.resolve() and
              type(receipt.get('bytes')) is int and receipt['bytes'] == expected_path.stat().st_size and
              _sha(expected_path) == enrolled_sha, 'file hash receipt differs from enrolled file')

    def captured(self, receipt, intent_path):
        path = pathlib.Path(intent_path)
        _need(path.is_absolute() and path.parent.resolve() == self.root,
              'command capture moved from original run directory')
        path = self.path(path.name)
        self.hash_receipt(receipt, path, self.pins[path.name])
        return path


def _command(reader, refs, role, binding, trust, tools, binary, owner):
    _need(isinstance(refs, dict) and set(refs) == {
        'intent_ref', 'result_ref', 'pre_hash_ref', 'hashes_ref'}, 'command reference fields changed')
    intent = reader.document(refs['intent_ref'])
    result = reader.document(refs['result_ref'])
    pre_hash = reader.document(refs['pre_hash_ref'])
    hashes = reader.document(refs['hashes_ref'])
    attempt = intent['attempt_id']
    _need(isinstance(attempt, str) and attempt and all(
        item.get('run_id') == trust.run_id and item.get('attempt_id') == attempt
        for item in (intent, result, hashes)), 'command run/attempt transplanted')
    _need(result.get('intent') == intent and result.get('error') is None,
          'standalone and recorded command intents differ')
    _need(intent.get('label') == binding['suite'] + '-' + role and
          _same_path(intent['working_directory'], owner['repository_physical']),
          'command label/workspace mismatch')
    process = result['process']
    _need(all(process.get(key) is True for key in (
        'process_created', 'user_code_started', 'group_assigned_before_resume',
        'parent_exit_observed', 'group_empty_observed', 'capture_complete')) and
        all(process.get(key) is False for key in ('timed_out', 'termination_requested')) and
        type(process.get('numeric_exit_code')) is int and process['numeric_exit_code'] == 0 and
        type(process.get('last_active_processes')) is int and process['last_active_processes'] == 0 and
        type(process.get('process_id')) is int and process['process_id'] > 0 and
        process.get('errors') == [] and process.get('cleanup_errors') == [],
        role + ': failed, timed out or incompletely supervised command')
    _need(_when(intent['intent_utc']) <= _when(process['started_utc']) <=
          _when(process['ended_utc']) <= _when(result['ended_utc']), 'command timestamps reversed')
    executable = pathlib.Path(intent['executable'])
    _need(executable.is_absolute(), 'absolute executable identity required')
    executable_sha = reader.pins[binary.name] if role == 'execute' else tools.get(executable.resolve())
    _need(executable_sha is not None and (role != 'execute' or executable.resolve() == binary),
          'unregistered command executable/tool')
    for receipt in (pre_hash, hashes['before'], hashes['after']):
        reader.hash_receipt(receipt, executable, executable_sha)
    stdout = reader.captured(hashes['stdout'], intent['stdout'])
    reader.captured(hashes['stderr'], intent['stderr'])
    args = intent['arguments']
    _need(isinstance(args, list) and all(isinstance(arg, str) for arg in args), 'command argv invalid')
    if role == 'compile':
        _need(binding['test_source_path'] in args and any(
            arg.startswith('/Fe') and pathlib.Path(arg[3:]).resolve() == binary for arg in args),
              'compile does not name registered source and output')
    elif role == 'execute':
        _need(args == [trust.run_id], 'execution does not use exact runner run ID')
    else:
        for flag, value in (('--suite', binding['suite']), ('--run-id', trust.run_id)):
            _need(args.count(flag) == 1 and args.index(flag) + 1 < len(args) and
                  args[args.index(flag) + 1] == value, 'verification argv correlation mismatch')
    return intent, stdout


def verify_runner_binding(root, result, obligation, trusted_runs, *, allow_test_only=False):
    """Authenticate a CPU receipt only; this never proves a Spectre obligation.

    The evaluator supplies reviewed assertion-to-obligation mapping separately.
    TEST_ONLY authority contexts are admitted only in explicit fixture mode.
    """
    outcome = {'verified': False, 'product_obligation_proven': False, 'reason': '',
               'origin': None, 'suite': None, 'assertion_ids': [], 'evidence_axis': 'CPU'}
    try:
        producer = result['producer']
        _need(producer['kind'] == 'ACTUAL_RUNNER', 'unsupported producer kind')
        trust = trusted_runs.get(producer['identity'])
        _need(type(trust) is TrustedRunnerRun and trust.authority_identity == producer['identity'],
              'no independently enrolled runner authority')
        _need(trust.origin == 'ACTUAL_RUNNER' or (allow_test_only and trust.origin == 'TEST_ONLY'),
              'test-only or unknown authority cannot authenticate product evidence')
        original = pathlib.Path(trust.run_directory)
        _need(not original.is_symlink() and _same_path(root, original),
              'copied run directory cannot reuse authority')
        root = pathlib.Path(root).resolve()
        _need(re.fullmatch('[0-9a-f]{32}', trust.run_id) and
              re.fullmatch('[0-9a-f]{40}', trust.source_commit) and
              re.fullmatch('[0-9a-f]{40}', trust.source_tree), 'trusted source/run identity malformed')
        _need(obligation['axis'] == 'CPU', 'CPU receipt cannot prove another original layer')
        reader = _ReceiptReader(root, result['artifacts'], trust)
        binding = reader.document(producer['authenticated_binding_ref'])
        fields = {'schema_version', 'run_id', 'suite', 'assertion_ids', 'source_commit', 'source_tree',
                  'environment_identity', 'config_identity', 'run_owner_ref', 'sources_before_ref',
                  'sources_after_ref', 'inventory_ref', 'environment_ref', 'binary_ref',
                  'test_source_path', 'inventory_source_path', 'runner', 'commands'}
        _need(isinstance(binding, dict) and set(binding) == fields and
              binding['schema_version'] == 'NeuRotic.SupervisedRunnerBinding/1' and
              binding['run_id'] == trust.run_id, 'binding fields/version/run mismatch')
        for name in ('source_commit', 'source_tree', 'environment_identity', 'config_identity'):
            _need(binding[name] == result['identities'][name] == obligation['identities'][name] ==
                  getattr(trust, name), 'stale or cross-source/environment/config identity: ' + name)
        reader.path(binding['environment_ref'])
        _need(reader.pins[binding['environment_ref']] == trust.environment_sha256,
              'environment differs from independently enrolled environment')
        owner = reader.document(binding['run_owner_ref'])
        _need(owner['run_id'] == trust.run_id and
              _same_path(owner['physical_directory'], root), 'run owner mismatch')
        source_pins = _path_pins(trust.source_hashes)
        snapshots = []
        for reference in ('sources_before_ref', 'sources_after_ref'):
            records = reader.document(binding[reference])
            if isinstance(records, dict):
                records = records['sources_after' if reference == 'sources_after_ref' else 'sources_before']
            _need(isinstance(records, list) and len(records) == len(source_pins) and
                  {pathlib.Path(row['path']).resolve() for row in records} == set(source_pins),
                  'source closure differs')
            for row in records:
                _need(row.get('state') == 'Recorded' and row.get('error') is None and
                      pathlib.Path(row['path']).is_absolute() and
                      row.get('sha256') == source_pins.get(pathlib.Path(row['path']).resolve()) and
                      type(row.get('bytes')) is int and row['bytes'] >= 0,
                      'source before/after differs from enrolled closure')
            snapshots.append({pathlib.Path(row['path']).resolve(): (row['sha256'], row['bytes'])
                              for row in records})
        _need(snapshots[0] == snapshots[1], 'source before/after byte identity changed')
        _need(pathlib.Path(binding['test_source_path']).resolve() in source_pins,
              'test source absent from enrolled closure')
        inventory_path = reader.path(binding['inventory_ref'])
        _need(source_pins.get(pathlib.Path(binding['inventory_source_path']).resolve()) ==
              reader.pins[inventory_path.name],
              'inventory absent from exact source closure')
        inventory = reader.document(binding['inventory_ref'])
        tools = _path_pins(trust.tool_hashes)
        runner = pathlib.Path(binding['runner']['path'])
        _need(runner.resolve() in tools, 'runner not independently enrolled')
        reader.hash_receipt(binding['runner'], runner, tools[runner.resolve()])
        binary = reader.path(binding['binary_ref'])
        _need(reader.pins[binary.name] == result['identities']['binary_sha256'] ==
              obligation['identities']['binary_sha256'], 'binary identity mismatch')
        commands = binding['commands']
        _need(isinstance(commands, dict) and set(commands) == {'compile', 'execute', 'verify'},
              'compile/execute/verify command closure required')
        observed = {role: _command(reader, refs, role, binding, trust, tools, binary, owner)
                    for role, refs in commands.items()}
        _need(len({item[0]['attempt_id'] for item in observed.values()}) == 3,
              'command attempt reused across roles')
        log_path = observed['execute'][1]
        args = observed['verify'][0]['arguments']
        _need(args.count('--log') == 1 and args.index('--log') + 1 < len(args) and
              pathlib.Path(args[args.index('--log') + 1]).resolve() == log_path,
              'verifier consumed another log')
        capture = io.StringIO()
        with contextlib.redirect_stdout(capture):
            _FRAMING.verify_log(inventory, binding['suite'], log_path, root, trust.run_id, False)
        framing = _FRAMING.load_json(capture.getvalue())
        _need(framing == _FRAMING.load_json(_FRAMING.bounded_text(observed['verify'][1])) and
              framing['mismatches'] == 0, 'framing report mismatched or assertion failed')
        selected = binding['assertion_ids']
        _need(isinstance(selected, list) and selected and all(isinstance(item, str) for item in selected)
              and len(set(selected)) == len(selected), 'assertion selection invalid')
        rows = {row['id']: row for row in inventory['suites'][binding['suite']]}
        decoded = [_FRAMING.load_json(line) for line in _FRAMING.bounded_text(log_path).splitlines()]
        raw = {row['id']: row for row in decoded if row.get('kind') == 'assertion'}
        _need(all(ident in rows and rows[ident]['binding'] == 'CURRENT_CALLABLE' and
                  raw[ident]['matched'] is True and raw[ident]['execution'] == 'COMPLETED'
                  for ident in selected), 'selected case is absent, failed, or only a precondition')
        repository = pathlib.Path(observed['compile'][0]['working_directory']).resolve()
        source_files = {path.relative_to(repository).as_posix(): sha for path, sha in source_pins.items()
                        if path.is_relative_to(repository)}
        outcome.update(verified=True, origin=trust.origin, suite=binding['suite'], assertion_ids=selected,
                       source_files=source_files,
                       reason='receipt verified; no reviewed product obligation mapping supplied')
    except (OSError, ValueError, TypeError, KeyError, AttributeError, IndexError,
            RecursionError, OverflowError) as error:
        outcome['reason'] = str(error)
    return outcome
