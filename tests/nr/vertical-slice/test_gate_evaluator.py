"""Behavioral TEST-003 gate tests. All positive evidence here is SYNTHETIC."""
import copy
import hashlib
import importlib.util
import json
import pathlib
import subprocess
import sys
import tempfile
import unittest

HERE = pathlib.Path(__file__).resolve().parent


class GateTests(unittest.TestCase):
    def setUp(self):
        self.assertTrue((HERE / 'gate_evaluator.py').is_file(), 'TEST-003 evaluator is missing')
        spec = importlib.util.spec_from_file_location('gate_evaluator', HERE / 'gate_evaluator.py')
        self.gate = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(self.gate)
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = pathlib.Path(self.tmp.name)
        self.manifest = {'schema_id': 'NeuRotic.G2.NativeEvidence', 'schema_version': 1,
                         'profile': 'D3D12-Native-SDR', 'artifacts': [], 'results': {},
                         'identities': {}, 'comparison_ref': 'comparison', 'coverage_ref': 'coverage'}
        self.add('raw', b'SYNTHETIC ONLY: no model or GPU ran', 'raw-command-output')
        for role in ('source', 'build', 'configuration', 'environment', 'runtime', 'model', 'provider', 'host'):
            self.add(role, {'kind': role, 'identity': 'synthetic-' + role, 'sha256': 'a' * 64}, 'identity')
            self.manifest['identities'][role] = role
        for name, role in [('recipe', 'C06'), ('execution', 'C07'), ('lease', 'C03'),
                           ('seal', 'C13'), ('capture', 'capture'), ('timing', 'timing'), ('invariants', 'owner-invariants')]:
            self.add(name, {'synthetic': True, 'role': role}, role)
        # Independent literal requirement list: dropping a requirement in the SUT
        # must not make the expected coverage shrink with it.
        self.cases = {
            'T16.ordered-not-reusable': 'WARP', 'T17.replay': 'WARP',
            'T18.cross-queue': 'WARP', 'T19.provider-retention': 'WARP',
            'T27.normal': 'CPU', 'T27.duplicate-seal': 'CPU', 'T27.duplicate-hook': 'CPU',
            'T27.competing-evaluations': 'CPU', 'T27.generated': 'CPU', 'T27.repeated': 'CPU',
            'T27.reincarnation': 'CPU', 'T27.unknown-frame': 'CPU', 'T27.node-mutation': 'CPU',
            'T27.stale-generation': 'CPU', 'T27.resource-retirement': 'WARP',
            'T28.early-not-final': 'CPU', 'T28.final-lineage': 'CPU', 'T28.no-present-cycle': 'CPU',
            'T28.no-cpu-wait': 'WARP', 'T28.present-failure': 'CPU', 'T28.provider-reuse': 'WARP',
            'T29.before-admission': 'CPU', 'T29.before-rights': 'CPU', 'T29.before-binding': 'CPU',
            'T29.before-record': 'CPU', 'T29.before-model': 'CPU', 'T29.model-advanced': 'CPU',
            'T29.resolve-failure': 'CPU', 'T29.dependency-failure': 'WARP',
            'T29.final-boundary-rejection': 'CPU', 'T29.consumer-rejection': 'CPU',
            'T29.present-failure': 'CPU', 'T29.skipped-reset': 'CPU', 'T29.owner-reset-ack': 'CPU',
            'T30.native-live': 'Native-Live', 'T49.aa': 'Native-Live',
            'T49.ab-same-binary': 'Native-Live', 'T49.ab-pinned-control': 'Native-Live', 'T60.coverage': 'CPU'}
        self.axes = ['protocol_semantics', 'gpu_resource_lifetime', 'temporal_iq_visible_behavior', 'performance_overhead']
        for case, profile in self.cases.items():
            required = self.axes if profile == 'Native-Live' else self.axes[:2 if profile == 'WARP' else 1]
            record = {'schema_id': 'NeuRotic.C15.Test003Result', 'schema_version': 1,
                      'fixture_id': case, 'profile': profile, 'execution_state': 'COMPLETED',
                      'command': {'executable': 'synthetic.exe', 'arguments': [], 'working_directory': 'synthetic',
                                  'started_utc': '2026-09-21T00:00:00Z', 'ended_utc': '2026-09-21T00:00:01Z',
                                  'exit_code': 0, 'stdout_ref': 'raw', 'stderr_ref': 'raw'},
                      'identity_refs': copy.deepcopy(self.manifest['identities']),
                      'axes': {a: {'execution_state': 'COMPLETED' if a in required else 'NOT_RUN',
                                   'applicable': a in required, 'required_for_gate': a in required,
                                   'verdict': 'PASS' if a in required else 'INCONCLUSIVE',
                                   'reason': 'Synthetic evaluator fixture', 'evidence_refs': ['raw'] if a in required else []}
                               for a in self.axes},
                      'expected_invariants': ['literal expected invariant'],
                      'observed_invariants': [{'id': 'literal expected invariant', 'verdict': 'PASS', 'evidence_refs': ['invariants']}],
                      'missing_evidence': [],
                      'proof_refs': ['recipe', 'execution', 'lease', 'seal', 'capture', 'timing', 'invariants']}
            self.add(case, record, 'C15-result')
            self.manifest['results'][case] = case
        self.add('coverage', {'schema_version': 1, 'missing': []}, 'T60-coverage')
        identity_a = {'source': 'a' * 64, 'build': 'b' * 64, 'artifact': 'c' * 64}
        identity_b = {'source': 'd' * 64, 'build': 'e' * 64, 'artifact': 'f' * 64}
        match = {name: 'synthetic' for name in ('scene', 'save', 'procedure', 'runtime', 'model', 'provider',
                 'settings', 'driver', 'environment', 'reset', 'warmup', 'capture', 'instrumentation',
                 'placement', 'render_raster', 'output_raster', 'working_raster', 'fg_state', 'quality')}
        match.update(api='D3D12', route='Native', color='SDR')
        self.comparison = {'schema_version': 1, 'unit': 'independent-run',
                           'aa': [], 'ab_same_binary': [], 'ab_pinned_control': [],
                           'intended_differences': ['native_protocol', 'source', 'build', 'artifact'],
                           'variance_ref': 'timing', 'paired_summary_ref': 'timing'}
        for group, order in [('aa', 'AAAAA'), ('ab_same_binary', 'ABBA' + 'BAAB' + 'ABBA'),
                             ('ab_pinned_control', 'ABBA' + 'BAAB' + 'ABBA')]:
            for i, arm in enumerate(order):
                ident = identity_b if group == 'ab_same_binary' or (group == 'ab_pinned_control' and arm == 'B') else identity_a
                self.comparison[group].append({'run_id': group + str(i), 'arm': arm,
                    'execution_state': 'COMPLETED', 'identity': copy.deepcopy(ident), 'match': copy.deepcopy(match),
                    'native_protocol': arm == 'B', 'capture_ref': 'capture', 'timing_ref': 'timing',
                    'command_ref': 'raw', 'alignment_ref': 'invariants'})
        self.add('comparison', self.comparison, 'T49-comparison')
        self.link_reports()

    def link_reports(self):
        # These are synthetic *validated-report shapes*, not runtime evidence.
        for kind in ('source', 'build', 'configuration', 'environment', 'runtime', 'model', 'provider', 'host'):
            payload = ('SYNTHETIC payload ' + kind).encode()
            if kind == 'source':
                payload = json.dumps({'kind': 'source', 'head': '1' * 40, 'branch': 'synthetic',
                    'working_state_sha256': '2' * 64, 'index_sha256': '3' * 64,
                    'diff_refs': ['raw'], 'submodules': []}).encode()
            if kind == 'build':
                payload = json.dumps({'kind': 'build', 'source_ref': 'source', 'exit_code': 0,
                    'command_ref': 'raw', 'toolchain_ref': 'raw', 'output_refs': ['candidate-binary']}).encode()
            if kind == 'configuration':
                payload = json.dumps({'kind': kind, 'raw_ref': 'raw', 'effective_ref': 'raw'}).encode()
            if kind == 'environment':
                payload = json.dumps({'kind': kind, 'os': 'synthetic', 'adapter': 'synthetic',
                                      'driver': 'synthetic', 'api': 'D3D12'}).encode()
            self.add(kind + '-payload', payload, 'identity-payload')
            ident = {'schema_id': 'NeuRotic.Test003.Identity', 'schema_version': 1,
                     'kind': kind, 'identity': 'synthetic-' + kind, 'payload_ref': kind + '-payload',
                     'sha256': hashlib.sha256(payload).hexdigest()}
            self.add(kind, ident, 'identity')
        self.add('candidate-binary', b'SYNTHETIC candidate binary', 'built-artifact')
        self.add('control-binary', b'SYNTHETIC control binary', 'built-artifact')
        self.manifest['control_identities'] = copy.deepcopy(self.manifest['identities'])
        for kind in ('source', 'build'):
            value = json.loads((self.root / (kind + '-payload.json')).read_bytes())
            if kind == 'source': value['head'] = '4' * 40
            if kind == 'build': value.update(source_ref='control-source', output_refs=['control-binary'])
            payload = json.dumps(value).encode()
            self.add('control-' + kind + '-payload', payload, 'identity-payload')
            self.add('control-' + kind, {'schema_id': 'NeuRotic.Test003.Identity', 'schema_version': 1,
                     'kind': kind, 'identity': 'synthetic-control-' + kind, 'payload_ref': 'control-' + kind + '-payload',
                     'sha256': hashlib.sha256(payload).hexdigest()}, 'identity')
            self.manifest['control_identities'][kind] = 'control-' + kind
        self.manifest['candidate_artifact_ref'] = 'candidate-binary'
        self.manifest['control_artifact_ref'] = 'control-binary'
        for group in ('aa', 'ab_same_binary', 'ab_pinned_control'):
            for run in self.comparison[group]:
                control = group == 'aa' or (group == 'ab_pinned_control' and run['arm'] == 'A')
                ids = self.manifest['control_identities'] if control else self.manifest['identities']
                run['identity_refs'] = copy.deepcopy(ids)
                run['identity'] = {k: self.record(ids[k])['sha256'] for k in ('source', 'build')}
                binary = 'control-binary' if control else 'candidate-binary'
                run['artifact_ref'] = binary
                run['identity']['artifact'] = next(a['sha256'] for a in self.manifest['artifacts'] if a['id'] == binary)
                for key in ('runtime', 'model', 'provider', 'environment'):
                    run['match'][key] = ids[key]
                run['match']['settings'] = ids['configuration']
                run_id = run['run_id']
                for key, role in [('capture_ref', 'capture'), ('timing_ref', 'timing')]:
                    ref = run_id + '-' + role
                    self.add(ref, ('SYNTHETIC ' + ref).encode(), role)
                    run[key] = ref
                run['command_ref'] = run_id + '-command'
                self.add(run['command_ref'], {'schema_id': 'NeuRotic.Test003.RunReceipt', 'schema_version': 1,
                    'run_id': run_id, 'identity_refs': ids, 'execution_state': 'COMPLETED', 'exit_code': 0,
                    'stdout_ref': 'raw', 'stderr_ref': 'raw', 'capture_ref': run['capture_ref'],
                    'timing_ref': run['timing_ref']}, 'run-command')
                run['alignment_ref'] = run_id + '-alignment'
                self.add(run['alignment_ref'], self.report('alignment', [run]), 'T49-report')
        all_runs = self.comparison['aa'] + self.comparison['ab_same_binary'] + self.comparison['ab_pinned_control']
        self.add('variance', self.report('variance', self.comparison['aa']), 'T49-report')
        self.add('paired-summary', self.report('pairing', all_runs[5:]), 'T49-report')
        self.comparison['variance_ref'] = 'variance'
        self.comparison['paired_summary_ref'] = 'paired-summary'
        self.add('comparison', self.comparison, 'T49-comparison')
        for case, profile in self.cases.items():
            r = self.record(case)
            r['expected_invariants'] = [case]
            r['observed_invariants'][0]['id'] = case
            if profile == 'Native-Live':
                r['run_ids'] = [case + '-run']
                group = {'T49.aa': 'aa', 'T49.ab-same-binary': 'ab_same_binary',
                         'T49.ab-pinned-control': 'ab_pinned_control'}.get(case)
                if group: r['run_ids'] = [run['run_id'] for run in self.comparison[group]]
                r['evaluation_ids'] = [case + '-evaluation']
                r['base_real_frame_ids'] = [case + '-frame']
                r['proof_refs'] = ['capture', 'timing', 'invariants']
                for kind in ('C06', 'C07', 'C03', 'C13'):
                    raw_ref, command_ref, proof_ref = case + '-' + kind + '-raw', case + '-' + kind + '-validation', case + '-' + kind
                    self.add(raw_ref, {'synthetic': True, 'canonical_contract': kind}, 'typed-owner-record')
                    context = {key: copy.deepcopy(r[key]) for key in ('identity_refs', 'run_ids', 'evaluation_ids', 'base_real_frame_ids')}
                    payload_hash = next(a['sha256'] for a in self.manifest['artifacts'] if a['id'] == raw_ref)
                    validated = dict(context, schema_id='NeuRotic.Test003.OwnerValidation', schema_version=1,
                        case_id=case, contract=kind, raw_ref=raw_ref, raw_sha256=payload_hash,
                        execution_state='COMPLETED', verdict='PASS', closure_refs=[raw_ref], closure_complete=True)
                    self.add(command_ref, dict(validated, schema_id='NeuRotic.Test003.ValidatorReceipt',
                             exit_code=0, executable_ref='host-payload', stdout_ref='raw', stderr_ref='raw'), 'owner-validator')
                    self.add(proof_ref, dict(validated, validation_ref=command_ref), kind)
                    r['proof_refs'].append(proof_ref)
            self.add(case, r, 'C15-result')

    def report(self, kind, runs):
        return {'schema_id': 'NeuRotic.Test003.ComparisonReport', 'schema_version': 1,
                'report_kind': kind, 'execution_state': 'COMPLETED', 'verdict': 'PASS', 'valid': True,
                'run_ids': [r['run_id'] for r in runs],
                'identity_refs': {r['run_id']: r['identity_refs'] for r in runs},
                'raw_refs': [r['capture_ref'] for r in runs] + [r['timing_ref'] for r in runs]}

    def add(self, name, value, role):
        raw = value if isinstance(value, bytes) else json.dumps(value).encode()
        path = self.root / (name + '.json')
        path.write_bytes(raw)
        self.manifest['artifacts'] = [x for x in self.manifest['artifacts'] if x['id'] != name]
        self.manifest['artifacts'].append({'id': name, 'path': path.name, 'sha256': hashlib.sha256(raw).hexdigest(),
                                         'bytes': len(raw), 'role': role, 'collection_state': 'COLLECTED'})

    def record(self, case):
        return json.loads((self.root / (case + '.json')).read_text())

    def evaluate(self):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.manifest))
        return self.gate.evaluate(path)

    def incomplete(self, structural=None):
        result = self.evaluate()
        self.assertEqual('G2 EVIDENCE INCOMPLETE', result['evidence_state'])
        self.assertTrue(result['blocking_requirements'])
        self.assertTrue(result['human_g2_review_required'])
        if structural is not None:
            self.assertEqual(structural, result['structural_valid'])

    def test_complete_fixture_accepts_different_ab_builds_without_approving_g2(self):
        result = self.evaluate()
        self.assertEqual('G2 EVIDENCE COMPLETE', result['evidence_state'])
        self.assertEqual([], result['blocking_requirements'])
        self.assertEqual(set(self.axes), set(result['axes']))
        self.assertNotIn('APPROVED', json.dumps(result))

    def test_missing_each_required_case_cannot_reduce_gate_scope(self):
        for case in self.cases:
            with self.subTest(case=case):
                ref = self.manifest['results'].pop(case)
                self.incomplete(False)
                self.manifest['results'][case] = ref

    def test_manifest_parse_and_schema_errors_precede_substantive_evaluation(self):
        path = self.root / 'bad.json'
        for raw in ('{', '[]', 'null', '{"schema_id":"x","schema_id":"y"}', '{"v":NaN}'):
            path.write_text(raw)
            result = self.gate.evaluate(path)
            self.assertFalse(result['structural_valid'])
            self.assertFalse(result['substantive_evaluated'])
        self.manifest['schema_version'] = True
        self.incomplete(False)
        self.manifest['schema_version'] = 2
        self.incomplete(False)

    def test_missing_invalid_and_tampered_artifacts(self):
        pristine = copy.deepcopy(self.manifest)
        for field in ('sha256', 'bytes', 'role', 'collection_state', 'path', 'id'):
            self.manifest = copy.deepcopy(pristine)
            del self.manifest['artifacts'][0][field]
            self.incomplete(False)
        self.manifest = pristine
        raw = (self.root / 'raw.json').read_bytes()
        (self.root / 'raw.json').write_bytes(b'X' * len(raw))
        self.incomplete(False)
        (self.root / 'raw.json').unlink()
        self.incomplete(False)

    def test_duplicate_or_escaping_artifact_paths_fail(self):
        pristine = copy.deepcopy(self.manifest)
        for path in ('../escape', '/absolute', 'C:/drive', 'a\\b', 'a:b', 'a//b', 'a/./b'):
            self.manifest = copy.deepcopy(pristine)
            self.manifest['artifacts'][0]['path'] = path
            self.incomplete(False)
        self.manifest = pristine
        self.manifest['artifacts'].append(copy.deepcopy(self.manifest['artifacts'][0]))
        self.incomplete(False)

    def test_nonpass_and_unexecuted_required_axes_remain_incomplete(self):
        original = self.record('T30.native-live')
        for axis in self.axes:
            for verdict in ('FAIL', 'INCONCLUSIVE', 'INFRA_ERROR', 'SKIP_UNSUPPORTED'):
                r = copy.deepcopy(original)
                r['axes'][axis]['verdict'] = verdict
                self.add('T30.native-live', r, 'C15-result')
                self.incomplete(True)
            for state in ('NOT_RUN', 'ATTEMPTED', 'PARTIAL', 'BLOCKED_BY_ENVIRONMENT'):
                r = copy.deepcopy(original)
                r['axes'][axis].update(execution_state=state, verdict='INCONCLUSIVE')
                self.add('T30.native-live', r, 'C15-result')
                self.incomplete(True)

    def test_case_execution_failure_and_axis_waiver_fail(self):
        original = self.record('T30.native-live')
        for field, value in [('execution_state', 'NOT_RUN'), ('profile', 'CPU')]:
            r = copy.deepcopy(original)
            r[field] = value
            self.add('T30.native-live', r, 'C15-result')
            self.incomplete()
        r = copy.deepcopy(original)
        r['command']['exit_code'] = 1
        self.add('T30.native-live', r, 'C15-result')
        self.incomplete(True)
        r = copy.deepcopy(original)
        r['axes']['gpu_resource_lifetime'].update(required_for_gate=False, applicable=False)
        self.add('T30.native-live', r, 'C15-result')
        self.incomplete(False)

    def test_empty_or_unresolved_evidence_and_missing_model_proof_fail(self):
        original = self.record('T30.native-live')
        for value in ([], ['missing'], ['']):
            r = copy.deepcopy(original)
            r['axes']['protocol_semantics']['evidence_refs'] = value
            self.add('T30.native-live', r, 'C15-result')
            self.incomplete()
        r = copy.deepcopy(original)
        r['proof_refs'].remove('T30.native-live-C07')
        self.add('T30.native-live', r, 'C15-result')
        self.incomplete(True)

    def test_missing_identity_and_invariant_fail(self):
        r = self.record('T30.native-live')
        del r['identity_refs']['runtime']
        self.add('T30.native-live', r, 'C15-result')
        self.incomplete(False)
        r = self.record('T27.normal')
        r['observed_invariants'] = []
        self.add('T27.normal', r, 'C15-result')
        self.incomplete(False)

    def test_t60_gap_cannot_be_waived_by_other_passes(self):
        self.add('coverage', {'schema_version': 1, 'missing': [{'requirement': 'model', 'required': True,
                    'execution_state': 'NOT_RUN', 'reason': 'No production path', 'evidence_refs': []}]}, 'T60-coverage')
        self.incomplete(True)

    def test_aa_requires_five_immutable_independent_runs(self):
        original = copy.deepcopy(self.comparison)
        for kind in ('count', 'identity', 'duplicate', 'capture', 'state'):
            c = copy.deepcopy(original)
            if kind == 'count': c['aa'].pop()
            if kind == 'identity': c['aa'][1]['identity']['artifact'] = 'f' * 64
            if kind == 'duplicate': c['aa'][1]['run_id'] = c['aa'][0]['run_id']
            if kind == 'capture': c['aa'][1]['capture_ref'] = 'missing'
            if kind == 'state': c['aa'][1]['execution_state'] = 'NOT_RUN'
            self.add('comparison', c, 'T49-comparison')
            self.incomplete()

    def test_ab_mismatch_arm_drift_and_uncounterbalanced_order_fail(self):
        original = copy.deepcopy(self.comparison)
        for kind in ('settings', 'identity', 'order', 'same-binary', 'alignment', 'unit'):
            c = copy.deepcopy(original)
            runs = c['ab_pinned_control']
            if kind == 'settings': runs[1]['match']['settings'] = 'different'
            if kind == 'identity': runs[2]['identity']['build'] = 'a' * 64
            if kind == 'order': runs[0]['arm'] = 'B'
            if kind == 'same-binary': c['ab_same_binary'][1]['identity']['artifact'] = '9' * 64
            if kind == 'alignment': runs[1]['alignment_ref'] = 'missing'
            if kind == 'unit': c['unit'] = 'individual-frame'
            self.add('comparison', c, 'T49-comparison')
            self.incomplete()

    def test_cli_failure_is_nonzero_and_machine_readable(self):
        path = self.root / 'malformed.json'
        path.write_text('{')
        p = subprocess.run([sys.executable, '-B', str(HERE / 'gate_evaluator.py'), str(path)], capture_output=True)
        self.assertEqual(1, p.returncode)
        self.assertEqual('G2 EVIDENCE INCOMPLETE', json.loads(p.stdout)['evidence_state'])

    def test_candidate_identity_cannot_change_between_comparison_forms(self):
        for run in self.comparison['ab_same_binary']:
            run['identity']['artifact'] = '9' * 64
        self.add('comparison', self.comparison, 'T49-comparison')
        self.incomplete()

    def test_review_failed_or_unrelated_comparison_reports_refuse(self):
        original = self.record('variance')
        for change in ({'verdict': 'FAIL'}, {'execution_state': 'NOT_RUN'}, {'valid': False}, {'run_ids': ['unrelated']}):
            self.add('variance', dict(original, **change), 'T49-report')
            self.incomplete()

    def test_review_identity_hash_payload_and_campaign_links_refuse(self):
        original = self.record('source')
        self.add('source', dict(original, sha256='8' * 64), 'identity')
        self.incomplete(False)
        self.add('source', original, 'identity')
        self.comparison['ab_same_binary'][0]['identity_refs']['runtime'] = 'host'
        self.add('comparison', self.comparison, 'T49-comparison')
        self.incomplete(False)

    def test_review_untyped_unvalidated_or_unrelated_owner_proofs_refuse(self):
        key = 'T30.native-live-C07'
        original = self.record(key)
        self.add(key, b'not a typed owner record', 'C07')
        self.incomplete(False)
        for change in ({'verdict': 'FAIL'}, {'base_real_frame_ids': ['wrong-frame']},
                       {'raw_sha256': '9' * 64}, {'closure_complete': False}, {'validation_ref': 'raw'}):
            self.add(key, dict(original, **change), 'C07')
            self.incomplete()

    def test_review_reused_run_receipt_and_capture_refuse(self):
        original = copy.deepcopy(self.comparison)
        for key in ('command_ref', 'capture_ref', 'alignment_ref'):
            c = copy.deepcopy(original)
            c['aa'][1][key] = c['aa'][0][key]
            self.add('comparison', c, 'T49-comparison')
            self.incomplete(False)

    def test_review_required_invariant_cannot_be_replaced_by_file_exists(self):
        r = self.record('T27.normal')
        r['expected_invariants'] = ['file exists']
        r['observed_invariants'][0]['id'] = 'file exists'
        self.add('T27.normal', r, 'C15-result')
        self.incomplete(False)

    def test_review_source_payload_requires_exact_source_fields(self):
        payload = b'arbitrary source bytes'
        self.add('source-payload', payload, 'identity-payload')
        identity = self.record('source')
        identity['sha256'] = hashlib.sha256(payload).hexdigest()
        self.add('source', identity, 'identity')
        for group in ('ab_same_binary', 'ab_pinned_control'):
            for run in self.comparison[group]:
                if run['identity_refs']['source'] == 'source': run['identity']['source'] = identity['sha256']
        self.add('comparison', self.comparison, 'T49-comparison')
        self.incomplete(False)

    def test_review_t49_result_cannot_describe_unrelated_runs(self):
        case = 'T49.aa'
        r = self.record(case)
        r['run_ids'] = ['unrelated']
        for ref in r['proof_refs']:
            if ref.endswith(('C06', 'C07', 'C03', 'C13')):
                proof = self.record(ref)
                proof['run_ids'] = ['unrelated']
                validator = self.record(proof['validation_ref'])
                validator['run_ids'] = ['unrelated']
                self.add(proof['validation_ref'], validator, 'owner-validator')
                self.add(ref, proof, proof['contract'])
        self.add(case, r, 'C15-result')
        self.incomplete(False)


if __name__ == '__main__':
    unittest.main(verbosity=2)
