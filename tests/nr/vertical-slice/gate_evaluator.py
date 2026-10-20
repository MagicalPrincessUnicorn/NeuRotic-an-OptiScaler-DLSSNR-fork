"""TEST-003 C15 evidence-envelope evaluator, not a renderer or human gate authority.

The required case/axis policy is owned by this reviewed tool, never by the input
manifest. Artifact hashes establish byte identity, not truth of owner assertions.
CPU test bundles must never be presented as real Native evidence.
"""
import datetime
import hashlib
import json
import pathlib
import re
import sys

AXES = ('protocol_semantics', 'gpu_resource_lifetime', 'temporal_iq_visible_behavior', 'performance_overhead')
VERDICTS = {'PASS', 'FAIL', 'SKIP_UNSUPPORTED', 'INCONCLUSIVE', 'INFRA_ERROR'}
EXECUTIONS = {'NOT_RUN', 'ATTEMPTED', 'PARTIAL', 'COMPLETED', 'BLOCKED_BY_ENVIRONMENT'}
IDENTITIES = ('source', 'build', 'configuration', 'environment', 'runtime', 'model', 'provider', 'host')
CASES = {**dict.fromkeys((
    'T27.normal', 'T27.duplicate-seal', 'T27.duplicate-hook', 'T27.competing-evaluations',
    'T27.generated', 'T27.repeated', 'T27.reincarnation', 'T27.unknown-frame', 'T27.node-mutation',
    'T27.stale-generation', 'T28.early-not-final', 'T28.final-lineage', 'T28.no-present-cycle',
    'T28.present-failure', 'T29.before-admission', 'T29.before-rights', 'T29.before-binding',
    'T29.before-record', 'T29.before-model', 'T29.model-advanced', 'T29.resolve-failure',
    'T29.final-boundary-rejection', 'T29.consumer-rejection', 'T29.present-failure',
    'T29.skipped-reset', 'T29.owner-reset-ack', 'T60.coverage'), 'CPU'),
    **dict.fromkeys(('T16.ordered-not-reusable', 'T17.replay', 'T18.cross-queue', 'T19.provider-retention',
                    'T27.resource-retirement', 'T28.no-cpu-wait', 'T28.provider-reuse', 'T29.dependency-failure'), 'WARP'),
    **dict.fromkeys(('T30.native-live', 'T49.aa', 'T49.ab-same-binary', 'T49.ab-pinned-control'), 'Native-Live')}
MATCH_FIELDS = ('scene', 'save', 'procedure', 'runtime', 'model', 'provider', 'settings', 'driver',
                'environment', 'reset', 'warmup', 'capture', 'instrumentation', 'placement',
                'render_raster', 'output_raster', 'working_raster', 'fg_state', 'quality', 'api', 'route', 'color')
MODEL_PROOFS = {'C06', 'C07', 'C03', 'C13', 'capture', 'timing', 'owner-invariants'}


class Invalid(ValueError):
    pass


def need(condition, reason):
    if not condition:
        raise Invalid(reason)


def text(value):
    return isinstance(value, str) and bool(value.strip())


def sha(value):
    return isinstance(value, str) and re.fullmatch('[0-9a-f]{64}', value) is not None


def integer(value):
    return type(value) is int


def object_fields(value, fields, where):
    need(isinstance(value, dict), where + ': object required')
    need(all(k in value for k in fields), where + ': missing required fields')


def unique_object(pairs):
    result = {}
    for key, value in pairs:
        need(key not in result, 'duplicate JSON key: ' + key)
        result[key] = value
    return result


def parse(raw):
    def invalid_constant(value):
        raise Invalid('non-finite JSON number: ' + value)
    return json.loads(raw, object_pairs_hook=unique_object, parse_constant=invalid_constant)


def envelope(value, schema):
    object_fields(value, ('schema_id', 'schema_version'), schema)
    need(value['schema_id'] == schema, 'unsupported schema identity')
    need(integer(value['schema_version']) and value['schema_version'] == 1, 'unsupported schema version')


def relative_path(value):
    return (text(value) and not any(c in value for c in '\\:') and not value.startswith('/') and
            all(p not in ('', '.', '..') and not p.endswith((' ', '.')) for p in value.split('/')))


def refs(values, artifacts, where, nonempty=False):
    need(isinstance(values, list), where + ': reference list required')
    need(not nonempty or bool(values), where + ': evidence missing')
    need(all(text(v) and v in artifacts for v in values), where + ': unresolved evidence')


def required_axes(profile):
    return AXES if profile == 'Native-Live' else AXES[:2 if profile == 'WARP' else 1]


def read_artifacts(manifest, root):
    need(isinstance(manifest['artifacts'], list) and manifest['artifacts'], 'artifact inventory missing')
    artifacts, paths = {}, set()
    for item in manifest['artifacts']:
        object_fields(item, ('id', 'path', 'sha256', 'bytes', 'role', 'collection_state'), 'artifact')
        key = item['id']
        need(text(key) and key not in artifacts, 'duplicate/invalid artifact identity')
        need(relative_path(item['path']), key + ': invalid relative artifact path')
        path = (root / item['path']).resolve()
        need(path.is_relative_to(root) and path.is_file(), key + ': artifact missing or outside bundle')
        need(str(path).casefold() not in paths, key + ': duplicate artifact path')
        paths.add(str(path).casefold())
        need(sha(item['sha256']) and integer(item['bytes']) and item['bytes'] >= 0, key + ': invalid hash/size')
        need(text(item['role']) and item['collection_state'] == 'COLLECTED', key + ': collection incomplete')
        need(path.stat().st_size == item['bytes'], key + ': artifact size mismatch')
        hasher = hashlib.sha256()
        with path.open('rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                hasher.update(chunk)
        need(hasher.hexdigest() == item['sha256'], key + ': artifact hash mismatch')
        artifacts[key] = dict(item, resolved_path=path)
    return artifacts


def document(ref, role, artifacts):
    need(text(ref) and ref in artifacts, role + ': missing reference')
    artifact = artifacts[ref]
    need(artifact['role'] == role, ref + ': incorrect artifact role')
    need(artifact['bytes'] <= 16 * 1024 * 1024, ref + ': structured document too large')
    return parse(artifact['resolved_path'].read_bytes())


def validate_identities(identities, artifacts):
    object_fields(identities, IDENTITIES, 'identity references')
    for kind in IDENTITIES:
        value = document(identities[kind], 'identity', artifacts)
        envelope(value, 'NeuRotic.Test003.Identity')
        object_fields(value, ('kind', 'identity', 'sha256', 'payload_ref'), kind)
        need(value['kind'] == kind and text(value['identity']) and sha(value['sha256']), kind + ': invalid identity')
        refs([value['payload_ref']], artifacts, kind + ': identity payload')
        payload = artifacts[value['payload_ref']]
        need(payload['role'] == 'identity-payload' and payload['sha256'] == value['sha256'], kind + ': unlinked identity hash')
        if kind in ('source', 'build', 'configuration', 'environment'):
            evidence = document(value['payload_ref'], 'identity-payload', artifacts)
            object_fields(evidence, ('kind',), kind + ': identity evidence')
            need(evidence['kind'] == kind, kind + ': identity evidence kind mismatch')
            if kind == 'source':
                object_fields(evidence, ('head', 'branch', 'working_state_sha256', 'index_sha256', 'diff_refs', 'submodules'), 'source evidence')
                need(isinstance(evidence['head'], str) and re.fullmatch('[0-9a-f]{40}', evidence['head']) is not None and
                     text(evidence['branch']) and sha(evidence['working_state_sha256']) and sha(evidence['index_sha256']), 'source identity incomplete')
                refs(evidence['diff_refs'], artifacts, 'source exact differences', nonempty=True)
                need(isinstance(evidence['submodules'], list), 'source submodule inventory missing')
                for submodule in evidence['submodules']:
                    object_fields(submodule, ('path', 'recorded_commit', 'actual_commit', 'status'), 'submodule identity')
                    need(relative_path(submodule['path']) and isinstance(submodule['status'], str), 'submodule state invalid')
                    for key in ('recorded_commit', 'actual_commit'):
                        need(isinstance(submodule[key], str) and re.fullmatch('[0-9a-f]{40}', submodule[key]) is not None, 'submodule commit missing')
            elif kind == 'build':
                object_fields(evidence, ('source_ref', 'exit_code', 'command_ref', 'toolchain_ref', 'output_refs'), 'build evidence')
                need(evidence['source_ref'] == identities['source'] and type(evidence['exit_code']) is int and evidence['exit_code'] == 0,
                     'build did not succeed for the declared source')
                refs([evidence['command_ref'], evidence['toolchain_ref']], artifacts, 'build command/toolchain')
                refs(evidence['output_refs'], artifacts, 'build outputs', nonempty=True)
                need(all(artifacts[k]['role'] == 'built-artifact' for k in evidence['output_refs']), 'build output role invalid')
            elif kind == 'configuration':
                object_fields(evidence, ('raw_ref', 'effective_ref'), 'configuration evidence')
                refs([evidence['raw_ref'], evidence['effective_ref']], artifacts, 'raw/effective configuration')
            else:
                object_fields(evidence, ('os', 'adapter', 'driver', 'api'), 'environment evidence')
                need(all(text(evidence[k]) for k in ('os', 'adapter', 'driver')) and evidence['api'] == 'D3D12', 'environment identity incomplete')


def nonempty_ids(value, where):
    need(isinstance(value, list) and value and all(text(x) for x in value), where + ': IDs missing')
    need(len(value) == len(set(value)), where + ': duplicate IDs')


def report_state(report, where, errors):
    object_fields(report, ('execution_state', 'verdict'), where)
    need(report['execution_state'] in EXECUTIONS and report['verdict'] in VERDICTS, where + ': invalid report state')
    if report['execution_state'] != 'COMPLETED' or report['verdict'] != 'PASS':
        errors.append(where + ': report is not completed PASS')


def validate_owner_reports(case, result, artifacts):
    errors = []
    for key in ('run_ids', 'evaluation_ids', 'base_real_frame_ids'):
        need(key in result, case + ': missing live identity linkage')
        nonempty_ids(result[key], case + '/' + key)
    for ref in result['proof_refs']:
        kind = artifacts[ref]['role']
        if kind not in ('C06', 'C07', 'C03', 'C13'):
            continue
        report = document(ref, kind, artifacts)
        envelope(report, 'NeuRotic.Test003.OwnerValidation')
        keys = ('case_id', 'contract', 'raw_ref', 'raw_sha256', 'validation_ref', 'closure_refs',
                'closure_complete', 'identity_refs', 'run_ids', 'evaluation_ids', 'base_real_frame_ids')
        object_fields(report, keys, ref)
        need(report['case_id'] == case and report['contract'] == kind, ref + ': unrelated owner report')
        for key in ('identity_refs', 'run_ids', 'evaluation_ids', 'base_real_frame_ids'):
            need(report[key] == result[key], ref + ': unrelated source/run/frame/evaluation')
        refs([report['raw_ref']], artifacts, ref + ': raw typed owner record')
        raw = artifacts[report['raw_ref']]
        need(raw['role'] == 'typed-owner-record' and raw['sha256'] == report['raw_sha256'], ref + ': raw owner record hash mismatch')
        refs(report['closure_refs'], artifacts, ref + ': metadata closure', nonempty=True)
        need(type(report['closure_complete']) is bool, ref + ': invalid closure status')
        if not report['closure_complete']: errors.append(ref + ': metadata closure incomplete')
        command = document(report['validation_ref'], 'owner-validator', artifacts)
        envelope(command, 'NeuRotic.Test003.ValidatorReceipt')
        object_fields(command, ('exit_code', 'executable_ref', 'stdout_ref', 'stderr_ref'), ref + ': validator')
        for key in keys:
            if key == 'validation_ref': continue
            need(key in command and command[key] == report[key], ref + ': validation did not cover exact owner report')
        refs([command['executable_ref'], command['stdout_ref'], command['stderr_ref']], artifacts, ref + ': validation command evidence')
        need(integer(command['exit_code']), ref + ': numeric validator exit missing')
        if command['exit_code'] != 0: errors.append(ref + ': validator failed')
        report_state(command, ref + ': validator', errors)
        report_state(report, ref, errors)
    return errors


def validate_result(case, profile, r, artifacts, identities):
    envelope(r, 'NeuRotic.C15.Test003Result')
    object_fields(r, ('fixture_id', 'profile', 'execution_state', 'command', 'identity_refs', 'axes',
                     'expected_invariants', 'observed_invariants', 'missing_evidence', 'proof_refs'), case)
    need(r['fixture_id'] == case and r['profile'] == profile, case + ': wrong fixture/profile')
    need(r['execution_state'] in EXECUTIONS, case + ': invalid execution state')
    validate_identities(r['identity_refs'], artifacts)
    need(r['identity_refs'] == identities, case + ': inconsistent campaign identity')
    cmd = r['command']
    object_fields(cmd, ('executable', 'arguments', 'working_directory', 'started_utc', 'ended_utc',
                        'exit_code', 'stdout_ref', 'stderr_ref'), case + ': command')
    need(text(cmd['executable']) and text(cmd['working_directory']), case + ': missing command identity')
    need(isinstance(cmd['arguments'], list) and all(isinstance(a, str) for a in cmd['arguments']), case + ': invalid argv')
    if r['execution_state'] != 'NOT_RUN':
        need(text(cmd['started_utc']) and text(cmd['ended_utc']), case + ': command time missing')
        start = datetime.datetime.fromisoformat(cmd['started_utc'].replace('Z', '+00:00'))
        end = datetime.datetime.fromisoformat(cmd['ended_utc'].replace('Z', '+00:00'))
        need(start.tzinfo is not None and end.tzinfo is not None and end >= start, case + ': invalid command times')
        need(integer(cmd['exit_code']), case + ': numeric exit missing')
        for key in ('stdout_ref', 'stderr_ref'):
            refs([cmd[key]], artifacts, case + ': command')
            need(artifacts[cmd[key]]['role'] == 'raw-command-output', case + ': raw output role missing')
    object_fields(r['axes'], AXES, case + ': axes')
    for axis in AXES:
        a = r['axes'][axis]
        object_fields(a, ('execution_state', 'applicable', 'required_for_gate', 'verdict', 'reason', 'evidence_refs'), case + ': axis')
        need(type(a['applicable']) is bool and type(a['required_for_gate']) is bool, case + ': invalid applicability')
        required = axis in required_axes(profile)
        need(a['required_for_gate'] == required and (not required or a['applicable']), case + ': axis policy cannot be waived')
        need(a['execution_state'] in EXECUTIONS and a['verdict'] in VERDICTS and text(a['reason']), case + ': invalid axis state')
        need(a['verdict'] != 'PASS' or a['execution_state'] == 'COMPLETED', case + ': unexecuted PASS')
        refs(a['evidence_refs'], artifacts, case + ': axis', nonempty=a['verdict'] == 'PASS')
    expected = r['expected_invariants']
    need(isinstance(expected, list) and expected and all(text(x) for x in expected), case + ': expected invariants missing')
    need(len(set(expected)) == len(expected), case + ': duplicate expected invariant')
    need(expected == [case], case + ': required invariant policy cannot be replaced')
    need(isinstance(r['observed_invariants'], list), case + ': observed invariants missing')
    seen = set()
    for observed in r['observed_invariants']:
        object_fields(observed, ('id', 'verdict', 'evidence_refs'), case + ': observed invariant')
        need(text(observed['id']) and observed['id'] not in seen and observed['id'] in expected, case + ': unknown/duplicate invariant')
        seen.add(observed['id'])
        need(observed['verdict'] in VERDICTS, case + ': invalid invariant verdict')
        refs(observed['evidence_refs'], artifacts, case + ': invariant', nonempty=observed['verdict'] == 'PASS')
    need(seen == set(expected), case + ': missing observed invariant')
    need(isinstance(r['missing_evidence'], list), case + ': missing coverage list')
    refs(r['proof_refs'], artifacts, case + ': owner proofs')
    r['_owner_errors'] = validate_owner_reports(case, r, artifacts) if profile == 'Native-Live' else []


def validate_comparison_report(ref, kind, runs, artifacts, errors):
    r = document(ref, 'T49-report', artifacts)
    envelope(r, 'NeuRotic.Test003.ComparisonReport')
    object_fields(r, ('report_kind', 'valid', 'run_ids', 'identity_refs', 'raw_refs'), 'T49 ' + kind)
    need(r['report_kind'] == kind and type(r['valid']) is bool, 'T49 invalid report kind/status')
    need(r['run_ids'] == [run['run_id'] for run in runs], 'T49 report run membership mismatch')
    need(r['identity_refs'] == {run['run_id']: run['identity_refs'] for run in runs}, 'T49 report identity mismatch')
    refs(r['raw_refs'], artifacts, 'T49 report raw evidence', nonempty=True)
    required = {run[key] for run in runs for key in ('capture_ref', 'timing_ref')}
    need(required.issubset(set(r['raw_refs'])), 'T49 report does not cover raw runs')
    report_state(r, 'T49 ' + kind, errors)
    if not r['valid']: errors.append('T49 ' + kind + ': invalid comparison result')


def validate_comparison(c, artifacts, manifest):
    object_fields(c, ('schema_version', 'unit', 'aa', 'ab_same_binary', 'ab_pinned_control',
                     'intended_differences', 'variance_ref', 'paired_summary_ref'), 'T49')
    need(type(c['schema_version']) is int and c['schema_version'] == 1, 'T49 schema unsupported')
    refs([c['variance_ref'], c['paired_summary_ref']], artifacts, 'T49 summaries')
    need(isinstance(c['intended_differences'], list) and all(text(x) for x in c['intended_differences']), 'T49 differences invalid')
    run_ids, used_commands, used_captures, used_alignment = set(), set(), set(), set()
    errors = []
    for group in ('aa', 'ab_same_binary', 'ab_pinned_control'):
        need(isinstance(c[group], list), 'T49 run list missing')
        for run in c[group]:
            object_fields(run, ('run_id', 'arm', 'execution_state', 'identity', 'match', 'native_protocol',
                               'capture_ref', 'timing_ref', 'command_ref', 'alignment_ref', 'identity_refs', 'artifact_ref'), 'T49 run')
            need(text(run['run_id']) and run['run_id'] not in run_ids, 'T49 run IDs not independent')
            run_ids.add(run['run_id'])
            need(run['arm'] in ('A', 'B') and run['execution_state'] in EXECUTIONS, 'T49 arm/state invalid')
            need(type(run['native_protocol']) is bool, 'T49 opt-in must be explicit')
            object_fields(run['identity'], ('source', 'build', 'artifact'), 'T49 source/build/artifact')
            need(all(sha(run['identity'][key]) for key in ('source', 'build', 'artifact')), 'T49 identity hashes missing')
            is_control = group == 'aa' or (group == 'ab_pinned_control' and run['arm'] == 'A')
            ids = manifest['control_identities'] if is_control else manifest['identities']
            need(run['identity_refs'] == ids, 'T49 arm identity differs from campaign')
            for kind in ('source', 'build'):
                ident = document(ids[kind], 'identity', artifacts)
                need(run['identity'][kind] == ident['sha256'], 'T49 arm hash differs from identity payload')
            binary = manifest['control_artifact_ref'] if is_control else manifest['candidate_artifact_ref']
            need(run['artifact_ref'] == binary and run['identity']['artifact'] == artifacts[binary]['sha256'], 'T49 arm artifact mismatch')
            object_fields(run['match'], MATCH_FIELDS, 'T49 matching conditions')
            need(all(text(run['match'][key]) for key in MATCH_FIELDS), 'T49 matching condition empty')
            for kind in ('runtime', 'model', 'provider', 'environment'):
                need(run['match'][kind] == ids[kind], 'T49 runtime/environment identity mismatch')
            need(run['match']['settings'] == ids['configuration'], 'T49 configuration identity mismatch')
            for key, role in (('capture_ref', 'capture'), ('timing_ref', 'timing'),
                              ('command_ref', 'run-command'), ('alignment_ref', 'T49-report')):
                refs([run[key]], artifacts, 'T49 raw run evidence')
                need(artifacts[run[key]]['role'] == role, 'T49 evidence role mismatch')
            for key, seen in (('command_ref', used_commands), ('capture_ref', used_captures), ('alignment_ref', used_alignment)):
                need(run[key] not in seen, 'T49 reused run receipt/capture/alignment')
                seen.add(run[key])
            command = document(run['command_ref'], 'run-command', artifacts)
            envelope(command, 'NeuRotic.Test003.RunReceipt')
            object_fields(command, ('run_id', 'identity_refs', 'execution_state', 'exit_code', 'stdout_ref',
                                    'stderr_ref', 'capture_ref', 'timing_ref'), 'T49 command')
            for key in ('run_id', 'identity_refs', 'capture_ref', 'timing_ref'):
                need(command[key] == run[key], 'T49 command not associated with exact run')
            need(command['execution_state'] in EXECUTIONS and integer(command['exit_code']), 'T49 invalid command state')
            refs([command['stdout_ref'], command['stderr_ref']], artifacts, 'T49 command outputs')
            if command['execution_state'] != 'COMPLETED' or command['exit_code'] != 0:
                errors.append('T49 required run command did not complete successfully')
            validate_comparison_report(run['alignment_ref'], 'alignment', [run], artifacts, errors)
    validate_comparison_report(c['variance_ref'], 'variance', c['aa'], artifacts, errors)
    validate_comparison_report(c['paired_summary_ref'], 'pairing', c['ab_same_binary'] + c['ab_pinned_control'], artifacts, errors)
    return errors


def comparison_blockers(c):
    errors = []
    if c['unit'] != 'independent-run': errors.append('T49 requires independent run-level analysis')
    if set(c['intended_differences']) != {'native_protocol', 'source', 'build', 'artifact'}:
        errors.append('T49 intended difference set does not match this narrow adapter experiment')
    aa = c['aa']
    if len(aa) < 5 or any(r['arm'] != 'A' or r['native_protocol'] for r in aa):
        errors.append('T49 A/A requires at least five control repetitions')
    if aa and any(r['identity'] != aa[0]['identity'] or r['match'] != aa[0]['match'] for r in aa):
        errors.append('T49 immutable A/A control mismatch')
    for group in ('aa', 'ab_same_binary', 'ab_pinned_control'):
        runs = c[group]
        if any(r['execution_state'] != 'COMPLETED' for r in runs): errors.append('T49 unexecuted run: ' + group)
        if any(r['match']['api'] != 'D3D12' or r['match']['route'] != 'Native' or r['match']['color'] != 'SDR' for r in runs):
            errors.append('T49 wrong Native profile: ' + group)
        if group == 'aa': continue
        if ''.join(r['arm'] for r in runs) != 'ABBABAABABBA':
            errors.append('T49 counterbalanced three-block sequence missing: ' + group)
        arms = {arm: [r for r in runs if r['arm'] == arm] for arm in ('A', 'B')}
        if not arms['A'] or not arms['B']:
            errors.append('T49 missing comparison arm: ' + group)
            continue
        if any(r['match'] != arms['A'][0]['match'] for r in runs): errors.append('T49 unmatched conditions: ' + group)
        for arm, members in arms.items():
            if any(r['identity'] != members[0]['identity'] or r['native_protocol'] != (arm == 'B') for r in members):
                errors.append('T49 identity/opt-in drift within arm: ' + group + '/' + arm)
        if group == 'ab_same_binary' and arms['A'][0]['identity'] != arms['B'][0]['identity']:
            errors.append('T49 OFF/ON must use the same candidate binary')
        if group == 'ab_pinned_control' and aa and (arms['A'][0]['identity'] != aa[0]['identity'] or arms['A'][0]['match'] != aa[0]['match']):
            errors.append('T49 pinned control does not match A/A control')
        # No equality check between pinned A/B source/build/artifact hashes.
    same = c['ab_same_binary']
    pinned_b = [r for r in c['ab_pinned_control'] if r['arm'] == 'B']
    if same and pinned_b and same[0]['identity'] != pinned_b[0]['identity']:
        errors.append('T49 candidate identity changed between comparison forms')
    return errors


# Versioned Spectre construction-scope adapter. This remains inside the
# TEST-003 evaluator and never changes the existing Native G2 case policy.
SPECTRE_IDENTITIES = ('source_commit', 'source_tree', 'schema_identity',
                      'case_definition_identity', 'profile_identity', 'placement',
                      'delivery_contract_identity', 'provider_identity',
                      'environment_identity', 'model_identity', 'game_identity',
                      'config_identity', 'driver_identity', 'binary_sha256')
SPECTRE_STATUSES = {'PASS', 'FAIL', 'NOT_RUN', 'BLOCKED', 'INCONCLUSIVE', 'INFRA_ERROR'}
SPECTRE_PRODUCERS = {'TEST_ONLY', 'ACTUAL_OWNER', 'ACTUAL_RUNNER', 'ACTUAL_REVIEWER'}
SPECTRE_ARTIFACTS = {'LOG', 'SOURCE_MAP', 'BINARY', 'CAPTURE', 'REVIEW', 'ENVIRONMENT', 'RESULT'}
# Code-owned original-oracle mapping. Review binds assertion bodies and the
# actual owner implementation, never a bundle's self-described test_binding.
def _reviewed_source_closure(name, expected_sha256):
    """Load only a code-pinned review manifest; runtime hashes cannot enroll it."""
    try:
        raw = (pathlib.Path(__file__).resolve().parent / name).read_bytes()
        if hashlib.sha256(raw).hexdigest() != expected_sha256:
            return {}
        document = json.loads(raw)
        files = document['source_sha256']
        if (document['schema_version'] != 'NeuRotic.ReviewedSourceClosure/2' or
                not isinstance(files, dict) or not files or
                any(not relative_path(path) or not sha(value) for path, value in files.items())):
            return {}
        review = document['review_record']
        if not relative_path(review['path']) or not sha(review['sha256']):
            return {}
        review_raw = (pathlib.Path(__file__).resolve().parents[3] / review['path']).read_bytes()
        if hashlib.sha256(review_raw).hexdigest() != review['sha256']:
            return {}
        reviewed = json.loads(review_raw)
        if (reviewed['schema_version'] != 'NeuRotic.SourceReviewRecord/1' or
                reviewed['source_sha256'] != files or
                reviewed['decision'] != 'REVIEWED_SOURCE_PROVENANCE_ONLY'):
            return {}
        return files
    except (OSError, ValueError, KeyError, TypeError):
        return {}


from controlled_producer import L07_ASSERTIONS, L07_MUTANTS, CONTROLLED_ASSERTIONS

SPECTRE_REVIEWED_RUNNER_BINDINGS = {
    'N04:unit:CPU': {
        'suite': 'initial-native-admission',
        'test_binding': 'N04.unit.native-sample-entry-gate/v2',
        'assertion_ids': (
            'hostreturn_route_active', 'hostreturn_native_without_final_claim',
            'hostreturn_duplicate_sample_refused', 'hostreturn_duplicate_skips_preparation',
            'hostreturn_owner_guarded_model_start', 'hostreturn_retired_duplicate_refused'),
        'source_sha256': _reviewed_source_closure('n04_reviewed_source_closure.json',
            '275fce2db25ccc510ee1d596882ce35a7757a72646ce92c14bd50ca7dbd9cccf'),
        'scope': 'Synthetic exterior owner; actual production admission/entry gate. No integration or GPU/model claim.',
    },
    'L07:controlled D3D12 host:WARP_MECHANICS': {
        'suite': 'owned-continuation', 'test_binding': 'L07.controlled.exact-signal-wait-readback/v1',
        'assertion_ids': L07_ASSERTIONS,
        'source_sha256': _reviewed_source_closure('l07_reviewed_source_closure.json',
            '0090bc1e2a8f68dfdbe4fb8242e77be7b3f6d346c892007e8cadb10551f8b8fc'),
        'physical_substrate': 'WARP_CONTROLLED_COPY',
        'scope': 'Actual software D3D12 two-queue signal/wait and readback; no provider/model/game/hardware claim.',
    },
    'L07:mutation:CPU': {
        'suite': 'owned-continuation', 'test_binding': 'L07.mutation.exact-wait-controls/v1',
        'assertion_ids': L07_ASSERTIONS, 'mutant': True, 'mutation_names': L07_MUTANTS,
        'source_sha256': _reviewed_source_closure('l07_reviewed_source_closure.json',
            '0090bc1e2a8f68dfdbe4fb8242e77be7b3f6d346c892007e8cadb10551f8b8fc'),
        'physical_substrate': 'WARP_CONTROLLED_COPY',
        'scope': 'Original CPU-labelled mutation cell; actual execution substrate WARP, four exact isolated Wait mutants.',
    },
    'N08:unit:CPU': {
        'suite': 'initial-native-races', 'test_binding': 'N08.unit.actual-start-revoke-linearization/v1',
        'assertion_ids': ('winning_start_retains_exact_owner', 'revoke_wins_no_new_call',
                          'simultaneous_revoke_start_linearized'),
        'source_sha256': _reviewed_source_closure('n08_reviewed_source_closure.json',
            '4d0088a331f3699b17e183be72ea1ff55dc071f1cb506a28377c646361d977a1'),
        'scope': 'Actual coordinator/ledger start-versus-revoke linearization with synthetic exterior invocation; exact retained guard and continuation, no real model/provider/game call.',
    },
    'N08:state-machine:CPU': {
        'suite': 'initial-native-races', 'test_binding': 'N08.state-machine.actual-start-revoke-linearization/v1',
        'assertion_ids': ('winning_start_retains_exact_owner', 'revoke_wins_no_new_call',
                          'simultaneous_revoke_start_linearized'),
        'source_sha256': _reviewed_source_closure('n08_reviewed_source_closure.json',
            '4d0088a331f3699b17e183be72ea1ff55dc071f1cb506a28377c646361d977a1'),
        'scope': 'Actual coordinator/ledger common-barrier race and both forced order controls, guarded started-work continuation; synthetic exterior, no real model/provider/game call.',
    },
}

for _case in ('O05', 'L04', 'L08', 'F03', 'L06'):
    SPECTRE_REVIEWED_RUNNER_BINDINGS[_case + ':controlled D3D12 host:WARP_MECHANICS'] = {
        'suite': 'owned-continuation',
        'test_binding': _case + '.controlled.owned-copy-oracle/v1',
        'assertion_ids': CONTROLLED_ASSERTIONS[_case],
        'source_sha256': _reviewed_source_closure('l07_reviewed_source_closure.json',
            '0090bc1e2a8f68dfdbe4fb8242e77be7b3f6d346c892007e8cadb10551f8b8fc'),
        'physical_substrate': 'WARP_CONTROLLED_COPY',
        'scope': 'Actual owned software D3D12 copy/recording mechanics; exact original oracle rows. '
                 'No NR model, proprietary provider, hardware, game or final delivery claim.',
    }

from construction_profiles import CONSTRUCTION_BINDINGS
from oracle_producer import ORACLE_BINDINGS
from provider_producer import PROVIDER_BINDINGS

for _ident, _mapping in CONSTRUCTION_BINDINGS.items():
    SPECTRE_REVIEWED_RUNNER_BINDINGS[_ident] = dict(_mapping,
        physical_substrate='CPU_CANONICAL_WITH_SYNTHETIC_EXTERIOR',
        source_sha256=_reviewed_source_closure('construction_reviewed_source_closure.json',
            '17b0b59e0b71dbcc0177d209dee25fbe54b447322314f6b761298b9aa8728e0e'))

for _ident, _mapping in ORACLE_BINDINGS.items():
    SPECTRE_REVIEWED_RUNNER_BINDINGS[_ident] = dict(_mapping,
        scope='Actual supervised Python comparison and semantic mutation oracles. Evaluator inputs are explicitly test fixtures; R03 additionally retains the original in-memory actual WARP capability. No real provider, model, game or human gate.',
        source_sha256=_reviewed_source_closure('oracle_reviewed_source_closure.json',
            '7a33dc7745011348e1e97fd344139c1ab1843a898f93b6654aac2b2a5e25f266'))

for _ident, _mapping in PROVIDER_BINDINGS.items():
    SPECTRE_REVIEWED_RUNNER_BINDINGS[_ident] = dict(_mapping,
        scope='Actual WARP capacity and separate C03 control with synthetic supported provider contract. Original CPU labels preserved; no Feature18/provider qualification.',
        source_sha256=_reviewed_source_closure('provider_reviewed_source_closure.json',
            'c80ae811b0e5e11cfb5781622aaa53634e67b399b1cb81db4833a1ed22f07185'))


def spectre_runner_covers(obligation_id, receipt, result):
    mapping = SPECTRE_REVIEWED_RUNNER_BINDINGS.get(obligation_id)
    return bool(mapping and receipt.get('verified') is True and receipt.get('origin') == 'ACTUAL_RUNNER'
        and result['mutant'] == mapping.get('mutant', False) and receipt['suite'] == mapping['suite']
        and result['test_binding'] == mapping['test_binding']
        and set(mapping['assertion_ids']).issubset(receipt['assertion_ids'])
        and (not mapping.get('physical_substrate') or
             receipt.get('physical_substrate') == mapping['physical_substrate'])
        and (not mapping.get('mutation_names') or
             set(receipt.get('mutation_names', ())) == set(mapping['mutation_names']))
        and mapping['source_sha256'] and all(receipt.get('source_files', {}).get(path) == sha
                for path, sha in mapping['source_sha256'].items()))


def _spectre_artifacts(items, root):
    need(isinstance(items, list), 'Spectre artifact list required')
    paths = set()
    for item in items:
        object_fields(item, ('path', 'sha256', 'kind'), 'Spectre artifact')
        need(set(item) == {'path', 'sha256', 'kind'}, 'Spectre artifact fields changed')
        need(relative_path(item['path']) and sha(item['sha256']) and
             item['kind'] in SPECTRE_ARTIFACTS, 'Spectre artifact descriptor invalid')
        path = (root / item['path']).resolve()
        need(path.is_relative_to(root) and path.is_file() and path not in paths,
             'Spectre artifact missing, duplicate or outside bundle')
        paths.add(path)
        hasher = hashlib.sha256()
        with path.open('rb') as source:
            for chunk in iter(lambda: source.read(1024 * 1024), b''):
                hasher.update(chunk)
        need(hasher.hexdigest() == item['sha256'], 'Spectre artifact hash mismatch')


def _spectre_identity_shape(values, case_map_hash):
    object_fields(values, SPECTRE_IDENTITIES, 'Spectre identities')
    need(set(values) == set(SPECTRE_IDENTITIES), 'Spectre identity key mismatch')
    need(all(value is None or text(value) for value in values.values()), 'Spectre identity value invalid')
    need(values['schema_identity'] == 'spectre-closure-v1-candidate' and
         values['case_definition_identity'] == case_map_hash,
         'Spectre schema/case identity mismatch')
    for key in ('source_commit', 'source_tree'):
        need(values[key] is None or re.fullmatch('[0-9a-f]{40}', values[key]) is not None,
             'Spectre source identity malformed')
    need(values['binary_sha256'] is None or sha(values['binary_sha256']),
         'Spectre binary identity malformed')


def evaluate_spectre_candidate(path, fixture_mode=False, *, trusted_runner_runs=None):
    """Report exact original-layer completeness, never human G2 or W04."""
    output = {'evidence_state': 'SCOPE EVIDENCE INCOMPLETE', 'structural_valid': False,
              'unsatisfied_obligations': [], 'unverified_producers': [],
              'verified_runner_receipts': [], 'unmapped_runner_obligations': [],
              'producer_verification_errors': {},
              'result_statuses': {}, 'result_reasons': {},
              'structural_errors': [],
              'whole_series_missing': [], 'human_g2_granted': False,
              'w04_authorized': False, 'provenance_limit':
              'Artifact hashes identify bytes. Runner verification requires an independently enrolled '
              'host context; a verified CPU receipt does not establish a product obligation.'}
    try:
        path = pathlib.Path(path).resolve()
        need(path.is_file() and path.stat().st_size <= 16 * 1024 * 1024, 'Spectre manifest missing or too large')
        manifest = parse(path.read_bytes())
        here = pathlib.Path(__file__).resolve().parent
        case_bytes = (here / 'spectre_case_map_58.json').read_bytes()
        policy_bytes = (here / 'spectre_scope_policy.json').read_bytes()
        case_hash = hashlib.sha256(case_bytes).hexdigest()
        policy_hash = hashlib.sha256(policy_bytes).hexdigest()
        cases = parse(case_bytes)['cases']
        policy = parse(policy_bytes)
        need(len(cases) == 58 and len({case['id'] for case in cases}) == 58,
             'Spectre recovered case inventory invalid')
        need(policy['schema_version'] == 1 and policy['case_map_sha256'] == case_hash,
             'Spectre policy/case map mismatch')
        object_fields(manifest, ('schema_version', 'scope_id', 'case_map_sha256',
                                'scope_policy_sha256', 'obligations', 'results',
                                'human_g2', 'w04_authorized'), 'Spectre manifest')
        need(set(manifest) == {'schema_version', 'scope_id', 'case_map_sha256',
                               'scope_policy_sha256', 'obligations', 'results',
                               'human_g2', 'w04_authorized'}, 'Spectre manifest fields changed')
        need(manifest['schema_version'] == 'spectre-closure-v1-candidate' and
             manifest['scope_id'] == policy['scope_id'] and
             manifest['case_map_sha256'] == case_hash and
             manifest['scope_policy_sha256'] == policy_hash and
             manifest['human_g2'] == 'NOT_GRANTED_BY_EVALUATOR' and
             manifest['w04_authorized'] is False, 'Spectre scope/version/gate mismatch')
        need(isinstance(manifest['obligations'], list) and isinstance(manifest['results'], list),
             'Spectre obligations/results must be lists')
        expected = {}
        for case in cases:
            for layer in case['original_layers']:
                need(layer in policy['layer_axes'], 'Spectre original layer missing from policy')
                for axis in policy['layer_axes'][layer]:
                    key = f"{case['id']}:{layer}:{axis}"
                    need(key not in expected, 'Spectre duplicate recovered layer')
                    expected[key] = (case['id'], axis)
        obligations = {}
        for obligation in manifest['obligations']:
            object_fields(obligation, ('id', 'case_id', 'axis', 'mandatory', 'identities'),
                          'Spectre obligation')
            need(set(obligation) == {'id', 'case_id', 'axis', 'mandatory', 'identities'},
                 'Spectre obligation fields changed')
            key = obligation['id']
            need(text(key) and key not in obligations and key in expected and
                 (obligation['case_id'], obligation['axis']) == expected[key] and
                 obligation['mandatory'] is True, 'Spectre obligation omitted, duplicated or changed')
            _spectre_identity_shape(obligation['identities'], case_hash)
            obligations[key] = obligation
        need(set(obligations) == set(expected), 'Spectre original case/layer obligation missing')
        results = {}
        result_ids = set()
        for result in manifest['results']:
            fields = ('id', 'obligation_id', 'status', 'test_binding', 'producer',
                      'identities', 'started', 'oracle_met', 'reason', 'artifacts',
                      'control_result_id', 'mutant')
            object_fields(result, fields, 'Spectre result')
            need(set(result) == set(fields), 'Spectre result fields changed')
            key = result['obligation_id']
            need(text(result['id']) and result['id'] not in result_ids and key in obligations and
                 result['status'] in SPECTRE_STATUSES and type(result['started']) is bool and
                 type(result['mutant']) is bool and text(result['reason']),
                 'Spectre result identity/status invalid')
            need((result['test_binding'] is None or text(result['test_binding'])) and
                 (result['oracle_met'] is None or type(result['oracle_met']) is bool) and
                 (result['control_result_id'] is None or text(result['control_result_id'])),
                 'Spectre result nullable fields invalid')
            result_ids.add(result['id'])
            _spectre_identity_shape(result['identities'], case_hash)
            object_fields(result['producer'], ('identity', 'kind', 'authenticated_binding_ref'),
                          'Spectre producer')
            need(set(result['producer']) == {'identity', 'kind', 'authenticated_binding_ref'} and
                 (result['producer']['identity'] is None or text(result['producer']['identity'])) and
                 (result['producer']['authenticated_binding_ref'] is None or
                  text(result['producer']['authenticated_binding_ref'])) and
                 result['producer']['kind'] in SPECTRE_PRODUCERS,
                 'Spectre producer kind invalid')
            _spectre_artifacts(result['artifacts'], path.parent)
            if result['status'] == 'PASS':
                need(result['started'] and result['oracle_met'] is True and
                     text(result['test_binding']) and result['artifacts'],
                     'Spectre PASS lacks executed oracle/binding/artifact')
            if result['status'] == 'NOT_RUN':
                need(not result['started'] and result['oracle_met'] is None,
                     'Spectre NOT_RUN cannot contain an execution')
            results.setdefault(key, []).append(result)
        output['structural_valid'] = True
        output['whole_series_missing'] = policy['always_missing_for_whole_series_until_separately_bound']
        by_id = {result['id']: result for group in results.values() for result in group}
        runner_covered = set()
        runner_receipts = {}
        # A real CPU receipt can be authenticated even when its associated
        # original product oracle is honestly NOT_RUN. Authentication must not
        # force a producer to claim an unrelated original oracle passed.
        for key, group in results.items():
            for result in group:
                if result['producer']['kind'] != 'ACTUAL_RUNNER':
                    continue
                from supervised_producer import verify_runner_binding
                from controlled_producer import TrustedControlledCampaign, verify_controlled_binding
                from construction_producer import TrustedConstructionRun, verify_construction_binding
                from oracle_producer import TrustedPythonOracleRun, verify_oracle_binding
                from provider_producer import TrustedProviderCampaign, verify_provider_binding
                verifier = {TrustedControlledCampaign: verify_controlled_binding,
                    TrustedConstructionRun: verify_construction_binding,
                    TrustedPythonOracleRun: verify_oracle_binding,
                    TrustedProviderCampaign: verify_provider_binding}.get(type((trusted_runner_runs or {}).get(
                    result['producer']['identity'])), verify_runner_binding)
                receipt = verifier(path.parent, result, obligations[key],
                                                trusted_runner_runs or {},
                                                allow_test_only=fixture_mode)
                if receipt['verified']:
                    runner_receipts[result['id']] = receipt
                    output['verified_runner_receipts'].append({'result_id': result['id'], **receipt})
                    if spectre_runner_covers(key, receipt, result):
                        runner_covered.add(result['id'])
                    else:
                        output['unmapped_runner_obligations'].append(key)
                else:
                    output['unverified_producers'].append(key)
                    output['producer_verification_errors'][result['id']] = receipt['reason']
        for key, obligation in obligations.items():
            group = results.get(key, [])
            output['result_statuses'][key] = [result['status'] for result in group]
            output['result_reasons'][key] = [result['reason'] for result in group]
            if not group or any(result['status'] != 'PASS' for result in group):
                output['unsatisfied_obligations'].append(key)
                continue
            valid = True
            for result in group:
                producer = result['producer']
                required = ('source_commit', 'source_tree', 'environment_identity',
                            'config_identity', 'binary_sha256')
                if obligation['axis'] in ('WARP_MECHANICS', 'HARDWARE_MECHANICS', 'LIVE_GAME'):
                    required += ('driver_identity',)
                if obligation['axis'] == 'LIVE_GAME':
                    required += ('provider_identity', 'model_identity', 'game_identity')
                if any(not text(result['identities'][field]) or
                       not text(obligation['identities'][field]) for field in required):
                    valid = False
                if any(value is not None and result['identities'][field] != value
                       for field, value in obligation['identities'].items()):
                    valid = False
                if producer['kind'] == 'TEST_ONLY' and not fixture_mode:
                    valid = False
                if producer['kind'] != 'TEST_ONLY':
                    binding = producer['authenticated_binding_ref']
                    if not text(producer['identity']) or not text(binding) or not any(
                            artifact['path'] == binding for artifact in result['artifacts']):
                        valid = False
                    if producer['kind'] == 'ACTUAL_RUNNER':
                        if result['id'] not in runner_covered:
                            valid = False
                    else:
                        # Owner/reviewer authority requires its own trusted issuer.
                        output['unverified_producers'].append(key)
                        valid = False
                if result['mutant']:
                    control = by_id.get(result['control_result_id'])
                    same_identities = bool(control and control['identities'] == result['identities'])
                    # A real compiler mutation necessarily changes the executable.
                    # Only our in-memory, fully checked L07 campaign can associate
                    # those two binaries; fixture controls retain exact equality.
                    current_receipt = runner_receipts.get(result['id'], {})
                    control_receipt = runner_receipts.get(result['control_result_id'], {})
                    actual_control = bool(control and key == 'L07:mutation:CPU' and
                        result['id'] in runner_covered and control['id'] in runner_covered and
                        control['obligation_id'] == 'L07:controlled D3D12 host:WARP_MECHANICS' and
                        current_receipt.get('campaign_id') and
                        current_receipt.get('campaign_id') == control_receipt.get('campaign_id') and
                        current_receipt.get('baseline_binary_sha256') == control['identities']['binary_sha256'] and
                        all(value == control['identities'][field] for field, value in result['identities'].items()
                            if field != 'binary_sha256'))
                    mapping = SPECTRE_REVIEWED_RUNNER_BINDINGS.get(key, {})
                    actual_control = actual_control or bool(control and mapping.get('mutant') and
                        mapping.get('control_obligation_id') == control['obligation_id'] and
                        result['id'] in runner_covered and control['id'] in runner_covered and
                        result['producer']['identity'] == control['producer']['identity'] and
                        current_receipt.get('campaign_kind') in ('CONSTRUCTION', 'PROVIDER') and
                        current_receipt.get('campaign_kind') == control_receipt.get('campaign_kind') and
                        current_receipt.get('campaign_id') and
                        current_receipt.get('campaign_id') == control_receipt.get('campaign_id') and
                        current_receipt.get('baseline_binary_sha256') == control['identities']['binary_sha256'] and
                        all(value == control['identities'][field] for field, value in result['identities'].items()
                            if field != 'binary_sha256'))
                    if (not control or control['mutant'] or control['status'] != 'PASS' or
                            not (same_identities or actual_control) or
                            (control['producer']['kind'] == 'TEST_ONLY' and not fixture_mode) or
                            not {'BINARY', 'LOG'}.issubset({a['kind'] for a in result['artifacts']})):
                        valid = False
            if not valid:
                output['unsatisfied_obligations'].append(key)
        if not output['unsatisfied_obligations']:
            output['evidence_state'] = 'FIXTURE SCOPE COMPLETE' if fixture_mode else 'SCOPE EVIDENCE COMPLETE'
    except (Invalid, OSError, ValueError, TypeError, KeyError, RecursionError, OverflowError) as error:
        output['structural_errors'].append(str(error))
    return output


def evaluate(path):
    try:
        if parse(pathlib.Path(path).read_bytes()).get('schema_version') == 'spectre-closure-v1-candidate':
            return evaluate_spectre_candidate(path)
    except (Invalid, OSError, ValueError, TypeError, AttributeError):
        pass
    output = {'evidence_state': 'G2 EVIDENCE INCOMPLETE', 'structural_valid': False,
              'substantive_evaluated': False, 'blocking_requirements': [], 'human_g2_review_required': True,
              'axes': {a: {'verdict': 'INCONCLUSIVE', 'blocking_cases': []} for a in AXES}}
    try:
        path = pathlib.Path(path).resolve()
        need(path.stat().st_size <= 16 * 1024 * 1024, 'manifest too large')
        manifest = parse(path.read_bytes())
        envelope(manifest, 'NeuRotic.G2.NativeEvidence')
        object_fields(manifest, ('profile', 'artifacts', 'results', 'identities', 'control_identities',
                                'control_artifact_ref', 'candidate_artifact_ref', 'comparison_ref', 'coverage_ref'), 'manifest')
        need(manifest['profile'] == 'D3D12-Native-SDR', 'unsupported gate profile')
        artifacts = read_artifacts(manifest, path.parent)
        validate_identities(manifest['identities'], artifacts)
        validate_identities(manifest['control_identities'], artifacts)
        for key in ('control_artifact_ref', 'candidate_artifact_ref'):
            refs([manifest[key]], artifacts, key)
            need(artifacts[manifest[key]]['role'] == 'built-artifact', key + ': wrong binary artifact role')
        object_fields(manifest['results'], CASES, 'required results')
        results = {}
        for case, profile in CASES.items():
            r = document(manifest['results'][case], 'C15-result', artifacts)
            validate_result(case, profile, r, artifacts, manifest['identities'])
            results[case] = r
        coverage = document(manifest['coverage_ref'], 'T60-coverage', artifacts)
        object_fields(coverage, ('schema_version', 'missing'), 'T60')
        need(type(coverage['schema_version']) is int and coverage['schema_version'] == 1 and isinstance(coverage['missing'], list), 'T60 schema invalid')
        for gap in coverage['missing']:
            object_fields(gap, ('requirement', 'required', 'execution_state', 'reason', 'evidence_refs'), 'T60 gap')
            need(text(gap['requirement']) and type(gap['required']) is bool and gap['execution_state'] in EXECUTIONS and text(gap['reason']), 'T60 gap invalid')
            refs(gap['evidence_refs'], artifacts, 'T60 infrastructure')
        comparison = document(manifest['comparison_ref'], 'T49-comparison', artifacts)
        comparison_errors = validate_comparison(comparison, artifacts, manifest)
        for case, group in (('T49.aa', 'aa'), ('T49.ab-same-binary', 'ab_same_binary'), ('T49.ab-pinned-control', 'ab_pinned_control')):
            need(results[case]['run_ids'] == [run['run_id'] for run in comparison[group]], case + ': result does not cover comparison runs')
        output['structural_valid'] = True
    except (Invalid, OSError, ValueError, TypeError, KeyError, RecursionError, OverflowError) as error:
        output['blocking_requirements'].append('Structural: ' + str(error))
        return output
    output['substantive_evaluated'] = True
    errors = output['blocking_requirements']
    for case, r in results.items():
        case_errors = list(r['_owner_errors'])
        if r['execution_state'] != 'COMPLETED': case_errors.append('required case not completed')
        if r['command']['exit_code'] != 0: case_errors.append('command failed')
        if r['missing_evidence']: case_errors.append('required evidence missing')
        if any(i['verdict'] != 'PASS' for i in r['observed_invariants']): case_errors.append('invariant not PASS')
        if r['profile'] == 'Native-Live' and not MODEL_PROOFS.issubset({artifacts[k]['role'] for k in r['proof_refs']}):
            case_errors.append('live recipe/execution/rights/seal/capture/timing/owner proof missing')
        for axis in required_axes(r['profile']):
            a = r['axes'][axis]
            if a['execution_state'] != 'COMPLETED' or a['verdict'] != 'PASS' or case_errors:
                output['axes'][axis]['blocking_cases'].append(case)
                errors.append(case + '/' + axis + ': ' + (', '.join(case_errors) if case_errors else 'required axis not completed PASS'))
    errors.extend(comparison_blockers(comparison))
    errors.extend(comparison_errors)
    # Any T60 gap in this narrowly scoped bundle is blocking. Caller-controlled
    # required=false cannot silently waive a missing part of this gate profile.
    errors.extend('T60: ' + g['requirement'] + ': ' + g['reason'] for g in coverage['missing'])
    for axis in AXES:
        if not output['axes'][axis]['blocking_cases']:
            output['axes'][axis]['verdict'] = 'PASS'
    if not errors:
        output['evidence_state'] = 'G2 EVIDENCE COMPLETE'
    return output


if __name__ == '__main__':
    if len(sys.argv) != 2:
        print('usage: gate_evaluator.py MANIFEST', file=sys.stderr)
        raise SystemExit(2)
    result = evaluate(sys.argv[1])
    print(json.dumps(result, indent=2))
    raise SystemExit(0 if result['evidence_state'] == 'G2 EVIDENCE COMPLETE' else 1)
