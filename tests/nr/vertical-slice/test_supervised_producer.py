"""Controlled receipt-verifier tests. No product obligation or runtime evidence."""
import copy
import dataclasses
import hashlib
import json
import os
import pathlib
import shutil
import tempfile
import unittest

try:
    import supervised_producer as producer
except ImportError:
    producer = None


HERE = pathlib.Path(__file__).resolve().parent
RUN = '1' * 32
SUITE = 'initial-profile-preparation'
CASE = 'preparation_without_fake_commit'


def digest(data):
    return hashlib.sha256(data).hexdigest()


class SupervisedProducerTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(producer, 'the supervised producer adapter is required')
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name) / 'owned-run'
        self.root.mkdir()
        self.documents = {}
        self.write('compiler.bin', b'controlled compiler identity')
        self.write('python.bin', b'controlled python identity')
        self.write('powershell.bin', b'controlled supervisor identity')
        self.write('suite.exe', b'controlled executable identity; never executable')
        self.write('source.cpp', b'controlled source fixture')
        self.write('execute.stdout', (HERE / 'fixtures/supervised_testonly.jsonl').read_bytes())
        for name in ('compile.stdout', 'compile.stderr', 'execute.stderr', 'verify.stderr'):
            self.write(name, b'')
        self.put('verify.stdout', {'kind': 'evidence-framing', 'suite': SUITE, 'emitted_suite': SUITE,
                 'run_id': RUN, 'expected_records': 1, 'observed_records': 1, 'current_callable': 1,
                 'preconditions': 0, 'unbound': 0, 'mismatches': 0, 'raw_only': False,
                 'canonical_decode_scope': 'C++ production codec, not this Python framing check',
                 'gate_approval': 'NOT_PERFORMED'})
        self.put('inventory.json', {'suites': {SUITE: [{'id': CASE, 'binding': 'CURRENT_CALLABLE'}]},
                 'suite_metadata': {SUITE: {'emitted_suite': SUITE}}})
        self.put('environment.json', {'kind': 'TEST_ONLY', 'os': 'controlled', 'architecture': 'x64'})
        self.put('run-owner.json', {'run_id': RUN, 'physical_directory': str(self.root),
                 'repository_physical': str(self.root / 'repository'), 'process_id': 123,
                 'created_utc': '2026-09-25T00:00:00Z'})
        source_receipts = [self.hash_receipt('source.cpp'), self.hash_receipt('inventory.json')]
        self.put('sources-before.json', source_receipts)
        self.put('sources-after.json', copy.deepcopy(source_receipts))
        self.commands = {}
        for role, executable, args in (
                ('compile', 'compiler.bin', [str(self.root / 'source.cpp'), '/Fe' + str(self.root / 'suite.exe')]),
                ('execute', 'suite.exe', [RUN]),
                ('verify', 'python.bin', ['-B', 'verify_initial.py', '--log', str(self.root / 'execute.stdout'),
                                        '--suite', SUITE, '--run-id', RUN])):
            attempt = role + '-attempt'
            intent = {'run_id': RUN, 'attempt_id': attempt, 'label': SUITE + '-' + role,
                      'executable': str(self.root / executable), 'arguments': args,
                      'windows_argument_string': 'controlled argv rendering',
                      'working_directory': str(self.root / 'repository'),
                      'intent_utc': '2026-09-25T00:00:01Z',
                      'stdout': str(self.root / (role + '.stdout')),
                      'stderr': str(self.root / (role + '.stderr'))}
            process = dict(process_created=True, user_code_started=True, process_id=124,
                           group_assigned_before_resume=True, parent_exit_observed=True,
                           group_empty_observed=True, last_active_processes=0, numeric_exit_code=0,
                           timed_out=False, termination_requested=False, capture_complete=True,
                           errors=[], cleanup_errors=[], started_utc='2026-09-25T00:00:01Z',
                           ended_utc='2026-09-25T00:00:02Z')
            self.put(role + '.intent.json', intent)
            self.put(role + '.result.json', {'run_id': RUN, 'attempt_id': attempt, 'intent': intent,
                     'process': process, 'error': None, 'ended_utc': '2026-09-25T00:00:02Z'})
            self.put(role + '.pre-hash.json', self.hash_receipt(executable))
            self.put(role + '.hashes.json', {'run_id': RUN, 'attempt_id': attempt,
                     'before': self.hash_receipt(executable), 'after': self.hash_receipt(executable),
                     'stdout': self.hash_receipt(role + '.stdout'), 'stderr': self.hash_receipt(role + '.stderr')})
            self.commands[role] = {key + '_ref': role + '.' + suffix for key, suffix in
                                   [('intent', 'intent.json'), ('result', 'result.json'),
                                    ('pre_hash', 'pre-hash.json'), ('hashes', 'hashes.json')]}
        self.binding = {'schema_version': 'NeuRotic.SupervisedRunnerBinding/1', 'run_id': RUN,
                        'suite': SUITE, 'assertion_ids': [CASE], 'source_commit': 'a' * 40,
                        'source_tree': 'b' * 40, 'environment_identity': 'controlled-env',
                        'config_identity': 'controlled-config', 'run_owner_ref': 'run-owner.json',
                        'sources_before_ref': 'sources-before.json', 'sources_after_ref': 'sources-after.json',
                        'inventory_ref': 'inventory.json', 'environment_ref': 'environment.json',
                        'binary_ref': 'suite.exe', 'test_source_path': str(self.root / 'source.cpp'),
                        'inventory_source_path': str(self.root / 'inventory.json'),
                        'runner': self.hash_receipt('powershell.bin'), 'commands': self.commands}
        self.put('binding.json', self.binding)
        self.identities = dict(source_commit='a' * 40, source_tree='b' * 40,
                               environment_identity='controlled-env', config_identity='controlled-config',
                               binary_sha256=digest((self.root / 'suite.exe').read_bytes()))
        self.result = {'identities': self.identities, 'test_binding': 'controlled-binding',
                       'producer': {'kind': 'ACTUAL_RUNNER', 'identity': 'controlled-authority',
                                    'authenticated_binding_ref': 'binding.json'}, 'artifacts': []}
        self.obligation = {'id': 'N01:unit:CPU', 'axis': 'CPU', 'identities': copy.deepcopy(self.identities)}
        self.reseal()

    def write(self, name, content):
        (self.root / name).write_bytes(content)

    def put(self, name, document):
        self.documents[name] = copy.deepcopy(document)
        self.write(name, (json.dumps(document, sort_keys=True) + '\n').encode())

    def hash_receipt(self, name):
        data = (self.root / name).read_bytes()
        return dict(path=str(self.root / name), state='Recorded', sha256=digest(data), bytes=len(data), error=None)

    def reseal(self):
        artifacts = {p.name: digest(p.read_bytes()) for p in self.root.iterdir() if p.is_file()}
        self.result['artifacts'] = [dict(path=name, sha256=sha, kind='RESULT') for name, sha in artifacts.items()]
        self.trust = producer.TrustedRunnerRun(
            authority_identity='controlled-authority', origin='TEST_ONLY', run_directory=self.root,
            run_id=RUN, source_commit='a' * 40, source_tree='b' * 40,
            source_hashes=tuple((r['path'], r['sha256']) for r in self.documents['sources-before.json']),
            tool_hashes=tuple((str(self.root / p), artifacts[p]) for p in ('compiler.bin', 'python.bin', 'powershell.bin')),
            environment_identity='controlled-env', environment_sha256=artifacts['environment.json'],
            config_identity='controlled-config', artifact_hashes=tuple(artifacts.items()))

    def verify(self, allow_test_only=True):
        return producer.verify_runner_binding(self.root, self.result, self.obligation,
                                             {'controlled-authority': self.trust}, allow_test_only=allow_test_only)

    def test_controlled_positive_verifies_receipt_but_not_product_coverage(self):
        outcome = self.verify()
        self.assertTrue(outcome['verified'], outcome)
        self.assertEqual(outcome['origin'], 'TEST_ONLY')
        self.assertEqual(outcome['assertion_ids'], [CASE])
        self.assertFalse(outcome['product_obligation_proven'])

    def test_manifest_cannot_self_enroll_a_trust_anchor(self):
        outcome = producer.verify_runner_binding(self.root, self.result, self.obligation, {})
        self.assertFalse(outcome['verified'])
        outcome = producer.verify_runner_binding(self.root, self.result, self.obligation,
                    {'controlled-authority': dataclasses.asdict(self.trust)}, allow_test_only=True)
        self.assertFalse(outcome['verified'])

    def test_test_only_origin_cannot_verify_product_evidence(self):
        self.assertFalse(self.verify(allow_test_only=False)['verified'])

    def test_copied_run_directory_cannot_reuse_authority(self):
        copied = self.root.parent / 'copy'
        shutil.copytree(self.root, copied)
        outcome = producer.verify_runner_binding(copied, self.result, self.obligation,
                                             {'controlled-authority': self.trust}, allow_test_only=True)
        self.assertFalse(outcome['verified'])

    @unittest.skipUnless(os.name == 'nt', 'Windows supervisor physical-path representation')
    def test_windows_physical_owner_alias_names_same_directory(self):
        owner = self.documents['run-owner.json']
        owner['physical_directory'] = '\\\\?\\' + str(self.root.resolve())
        self.put('run-owner.json', owner)
        self.reseal()
        self.trust = dataclasses.replace(self.trust, run_directory=pathlib.Path(owner['physical_directory']))
        outcome = self.verify()
        self.assertTrue(outcome['verified'], outcome)

    def test_forged_receipt_with_rehashed_manifest_is_not_authorized(self):
        doc = self.documents['execute.result.json']
        doc['process']['process_id'] = 999
        self.put('execute.result.json', doc)
        for artifact in self.result['artifacts']:
            if artifact['path'] == 'execute.result.json':
                artifact['sha256'] = digest((self.root / artifact['path']).read_bytes())
        self.assertFalse(self.verify()['verified'])

    def test_stale_source_commit_and_tree_are_rejected(self):
        for key in ('source_commit', 'source_tree'):
            with self.subTest(key=key):
                original = self.result['identities'][key]
                self.result['identities'][key] = 'd' * 40
                self.assertFalse(self.verify()['verified'])
                self.result['identities'][key] = original

    def test_source_hash_drift_is_rejected_even_with_pinned_receipt(self):
        docs = self.documents['sources-after.json']
        docs[0]['sha256'] = 'd' * 64
        self.put('sources-after.json', docs)
        self.reseal()
        self.assertFalse(self.verify()['verified'])

    def test_cross_environment_and_config_are_rejected(self):
        for key in ('environment_identity', 'config_identity'):
            with self.subTest(key=key):
                original = self.result['identities'][key]
                self.result['identities'][key] = 'other'
                self.assertFalse(self.verify()['verified'])
                self.result['identities'][key] = original

    def test_source_snapshot_size_drift_is_rejected(self):
        changed = self.documents['sources-after.json']
        changed[0]['bytes'] += 1
        self.put('sources-after.json', changed)
        self.reseal()
        self.assertFalse(self.verify()['verified'])

    def test_failed_timeout_and_uncertain_cleanup_are_rejected(self):
        for changes in ({'numeric_exit_code': 1}, {'timed_out': True}, {'group_empty_observed': False},
                        {'capture_complete': False}, {'errors': ['launch failed']}, {'cleanup_errors': ['uncertain']}):
            with self.subTest(changes=changes):
                original = copy.deepcopy(self.documents['execute.result.json'])
                changed = copy.deepcopy(original)
                changed['process'].update(changes)
                self.put('execute.result.json', changed)
                self.reseal()
                self.assertFalse(self.verify()['verified'])
                self.put('execute.result.json', original)

    def test_missing_executable_and_changed_tool_hash_are_rejected(self):
        (self.root / 'suite.exe').unlink()
        self.assertFalse(self.verify()['verified'])
        self.write('suite.exe', b'controlled executable identity; never executable')
        self.trust = dataclasses.replace(self.trust, tool_hashes=((str(self.root / 'compiler.bin'), 'd' * 64),))
        self.assertFalse(self.verify()['verified'])

    def test_cpu_receipt_cannot_supply_warp_or_game_layer(self):
        for axis in ('WARP_MECHANICS', 'HARDWARE_MECHANICS', 'LIVE_GAME', 'SOURCE_ANALYSIS'):
            with self.subTest(axis=axis):
                self.obligation['axis'] = axis
                self.assertFalse(self.verify()['verified'])

    def test_wrong_run_and_transplanted_command_are_rejected(self):
        changed = self.documents['execute.result.json']
        changed['intent']['run_id'] = '2' * 32
        self.put('execute.result.json', changed)
        self.reseal()
        self.assertFalse(self.verify()['verified'])

    def test_inventory_unknown_case_is_rejected(self):
        self.binding['assertion_ids'] = ['unregistered-case']
        self.put('binding.json', self.binding)
        self.reseal()
        self.assertFalse(self.verify()['verified'])

    def test_well_framed_failed_assertion_is_not_a_successful_receipt(self):
        rows = [json.loads(line) for line in (self.root / 'execute.stdout').read_text().splitlines()]
        rows[0]['matched'] = False
        rows[1]['record']['record']['result']['protocolSemantic']['verdict']['value'] = 'FAIL'
        rows[2]['mismatches'] = 1
        self.write('execute.stdout', ('\n'.join(json.dumps(row) for row in rows) + '\n').encode())
        report = self.documents['verify.stdout']
        report['mismatches'] = 1
        self.put('verify.stdout', report)
        for role in ('execute', 'verify'):
            hashes = self.documents[role + '.hashes.json']
            hashes['stdout'] = self.hash_receipt(role + '.stdout')
            self.put(role + '.hashes.json', hashes)
        self.reseal()
        outcome = self.verify()
        self.assertFalse(outcome['verified'])
        self.assertIn('assertion failed', outcome['reason'])

    def test_framing_report_does_not_replace_raw_emitted_evidence(self):
        self.write('execute.stdout', b'{"kind":"suite-summary"}\n')
        hashes = self.documents['execute.hashes.json']
        hashes['stdout'] = self.hash_receipt('execute.stdout')
        self.put('execute.hashes.json', hashes)
        self.reseal()
        self.assertFalse(self.verify()['verified'])

    def test_escaping_binding_ref_is_rejected(self):
        self.result['producer']['authenticated_binding_ref'] = '../outside.json'
        self.assertFalse(self.verify()['verified'])

    def evaluate_bundle(self, fixture_mode, trust, status='PASS'):
        from gate_evaluator import SPECTRE_IDENTITIES, evaluate_spectre_candidate
        cases = json.loads((HERE / 'spectre_case_map_58.json').read_text())
        policy = json.loads((HERE / 'spectre_scope_policy.json').read_text())
        case_sha = digest((HERE / 'spectre_case_map_58.json').read_bytes())
        identities = dict.fromkeys(SPECTRE_IDENTITIES)
        identities.update(self.identities, schema_identity='spectre-closure-v1-candidate',
                          case_definition_identity=case_sha)
        obligations = [dict(id=f"{case['id']}:{layer}:{axis}", case_id=case['id'], axis=axis,
                            mandatory=True, identities=copy.deepcopy(identities))
                       for case in cases['cases'] for layer in case['original_layers']
                       for axis in policy['layer_axes'][layer]]
        result = copy.deepcopy(self.result)
        result.update(id='controlled-receipt', obligation_id=self.obligation['id'], identities=identities,
                      status=status, started=status == 'PASS', oracle_met=True if status == 'PASS' else None,
                      reason='TEST_ONLY controlled verifier input',
                      control_result_id=None, mutant=False)
        manifest = dict(schema_version='spectre-closure-v1-candidate', scope_id=policy['scope_id'],
                        case_map_sha256=case_sha,
                        scope_policy_sha256=digest((HERE / 'spectre_scope_policy.json').read_bytes()),
                        obligations=obligations, results=[result], human_g2='NOT_GRANTED_BY_EVALUATOR',
                        w04_authorized=False)
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(manifest))
        return evaluate_spectre_candidate(path, fixture_mode=fixture_mode,
                                          trusted_runner_runs={'controlled-authority': trust})

    def test_evaluator_verified_fixture_receipt_has_no_product_mapping(self):
        output = self.evaluate_bundle(True, self.trust)
        self.assertTrue(output['structural_valid'], output)
        self.assertEqual(len(output['verified_runner_receipts']), 1)
        self.assertEqual(output['unmapped_runner_obligations'], [self.obligation['id']])
        self.assertEqual(len(output['unsatisfied_obligations']), 129)
        self.assertEqual(output['evidence_state'], 'SCOPE EVIDENCE INCOMPLETE')
        self.assertFalse(output['human_g2_granted'])
        self.assertFalse(output['w04_authorized'])

    def test_evaluator_product_rejects_fixture_origin_even_with_context(self):
        output = self.evaluate_bundle(False, self.trust)
        self.assertTrue(output['structural_valid'], output)
        self.assertFalse(output['verified_runner_receipts'])
        self.assertIn(self.obligation['id'], output['unverified_producers'])
        self.assertEqual(len(output['unsatisfied_obligations']), 129)

    def test_evaluator_trusted_runner_context_does_not_invent_original_oracle(self):
        # This caller-created context exercises the host API. It is not a real run.
        authority_stub = dataclasses.replace(self.trust, origin='ACTUAL_RUNNER')
        output = self.evaluate_bundle(False, authority_stub)
        self.assertEqual(len(output['verified_runner_receipts']), 1)
        self.assertEqual(len(output['unsatisfied_obligations']), 129)
        self.assertEqual(output['unmapped_runner_obligations'], [self.obligation['id']])

    def test_evaluator_authenticates_receipt_without_claiming_original_oracle_ran(self):
        authority_stub = dataclasses.replace(self.trust, origin='ACTUAL_RUNNER')
        output = self.evaluate_bundle(False, authority_stub, status='NOT_RUN')
        self.assertEqual(len(output['verified_runner_receipts']), 1)
        self.assertEqual(output['result_statuses'][self.obligation['id']], ['NOT_RUN'])
        self.assertEqual(len(output['unsatisfied_obligations']), 129)


if __name__ == '__main__':
    unittest.main()
