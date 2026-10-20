"""Synthetic controls for the 58-case TEST-003 versioned adapter; no product run."""
import copy
import hashlib
import json
import pathlib
import tempfile
import unittest

from gate_evaluator import evaluate, evaluate_spectre_candidate


HERE = pathlib.Path(__file__).resolve().parent
CASE_PATH = HERE / 'spectre_case_map_58.json'
POLICY_PATH = HERE / 'spectre_scope_policy.json'
IDENTITIES = {
    'source_commit': 'a' * 40, 'source_tree': 'b' * 40,
    'schema_identity': 'spectre-closure-v1-candidate',
    'case_definition_identity': hashlib.sha256(CASE_PATH.read_bytes()).hexdigest(),
    'profile_identity': 'fixture-profile', 'placement': 'NativeAfter',
    'delivery_contract_identity': 'fixture-delivery', 'provider_identity': 'fixture-provider',
    'environment_identity': 'fixture-environment', 'model_identity': 'fixture-model',
    'game_identity': 'fixture-game', 'config_identity': 'fixture-config',
    'driver_identity': 'fixture-driver', 'binary_sha256': 'c' * 64,
}


class SpectreScopeTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.root = pathlib.Path(self.temp.name)
        artifact = self.root / 'synthetic-evaluator-evidence.txt'
        artifact.write_text('TEST_ONLY evaluator fixture\n', encoding='utf-8')
        self.artifact = {
            'path': artifact.name, 'sha256': hashlib.sha256(artifact.read_bytes()).hexdigest(), 'kind': 'LOG'
        }
        cases = json.loads(CASE_PATH.read_text(encoding='utf-8'))['cases']
        layers = json.loads(POLICY_PATH.read_text(encoding='utf-8'))['layer_axes']
        obligations = []
        for case in cases:
            for layer in case['original_layers']:
                for axis in layers[layer]:
                    obligations.append({'id': f"{case['id']}:{layer}:{axis}", 'case_id': case['id'],
                                        'axis': axis, 'mandatory': True, 'identities': copy.deepcopy(IDENTITIES)})
        self.manifest = {
            'schema_version': 'spectre-closure-v1-candidate',
            'scope_id': 'W03-ORIGINAL-LAYER-CONSTRUCTION-V1',
            'case_map_sha256': hashlib.sha256(CASE_PATH.read_bytes()).hexdigest(),
            'scope_policy_sha256': hashlib.sha256(POLICY_PATH.read_bytes()).hexdigest(),
            'obligations': obligations, 'results': [],
            'human_g2': 'NOT_GRANTED_BY_EVALUATOR', 'w04_authorized': False,
        }

    def run_bundle(self, fixture_mode=False):
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.manifest), encoding='utf-8')
        return evaluate_spectre_candidate(path, fixture_mode=fixture_mode)

    def fill_synthetic_passes(self):
        self.manifest['results'] = [
            {'id': 'result:' + o['id'], 'obligation_id': o['id'], 'status': 'PASS',
             'test_binding': 'synthetic evaluator oracle',
             'producer': {'identity': 'fixture', 'kind': 'TEST_ONLY', 'authenticated_binding_ref': None},
             'identities': copy.deepcopy(o['identities']), 'started': True, 'oracle_met': True,
             'reason': 'synthetic evaluator fixture only', 'artifacts': [copy.deepcopy(self.artifact)],
             'control_result_id': None, 'mutant': False}
            for o in self.manifest['obligations']]

    def test_missing_results_preserve_every_original_layer(self):
        result = self.run_bundle()
        self.assertTrue(result['structural_valid'])
        self.assertEqual(len(result['unsatisfied_obligations']), len(self.manifest['obligations']))
        self.assertFalse(result['human_g2_granted'])
        self.assertFalse(result['w04_authorized'])

    def test_synthetic_completeness_is_fixture_only(self):
        self.fill_synthetic_passes()
        fixture = self.run_bundle(fixture_mode=True)
        product = self.run_bundle()
        self.assertEqual(fixture['evidence_state'], 'FIXTURE SCOPE COMPLETE')
        self.assertEqual(product['evidence_state'], 'SCOPE EVIDENCE INCOMPLETE')
        self.assertTrue(product['unsatisfied_obligations'])
        self.assertFalse(fixture['human_g2_granted'])

    def test_stale_identity_and_missing_layer_cannot_complete(self):
        self.fill_synthetic_passes()
        self.manifest['results'][0]['identities']['source_commit'] = 'd' * 40
        self.assertIn(self.manifest['obligations'][0]['id'], self.run_bundle(True)['unsatisfied_obligations'])
        self.manifest['results'][0]['identities']['source_commit'] = 'a' * 40
        self.manifest['obligations'].pop()
        self.assertFalse(self.run_bundle(True)['structural_valid'])

    def test_unknown_required_identity_cannot_be_supplied_by_result(self):
        self.fill_synthetic_passes()
        first = self.manifest['obligations'][0]
        first['identities']['source_commit'] = None
        result = self.run_bundle(True)
        self.assertTrue(result['structural_valid'])
        self.assertIn(first['id'], result['unsatisfied_obligations'])

    def test_conflicting_failure_is_retained_and_cli_never_grants_g2(self):
        self.fill_synthetic_passes()
        first = self.manifest['results'][0]
        failure = copy.deepcopy(first)
        failure.update(id='older-failure', status='FAIL', oracle_met=False,
                       reason='original oracle failed')
        self.manifest['results'].insert(0, failure)
        fixture = self.run_bundle(True)
        self.assertIn(first['obligation_id'], fixture['unsatisfied_obligations'])
        self.assertEqual(fixture['result_statuses'][first['obligation_id']], ['FAIL', 'PASS'])
        self.manifest['results'].pop(0)
        path = self.root / 'manifest.json'
        path.write_text(json.dumps(self.manifest), encoding='utf-8')
        cli = evaluate(path)
        self.assertTrue(cli['structural_valid'])
        self.assertEqual(cli['evidence_state'], 'SCOPE EVIDENCE INCOMPLETE')
        self.assertFalse(cli['human_g2_granted'])

    def test_artifact_and_duplicate_result_refuse(self):
        self.fill_synthetic_passes()
        self.manifest['results'][0]['artifacts'][0]['sha256'] = '0' * 64
        self.assertFalse(self.run_bundle(True)['structural_valid'])
        self.fill_synthetic_passes()
        self.manifest['results'].append(copy.deepcopy(self.manifest['results'][0]))
        self.assertFalse(self.run_bundle(True)['structural_valid'])

    def test_infra_error_cannot_become_pass(self):
        self.fill_synthetic_passes()
        result = self.manifest['results'][0]
        result.update(status='INFRA_ERROR', oracle_met=None, reason='supervised runner launch failed')
        output = self.run_bundle(True)
        self.assertTrue(output['structural_valid'])
        self.assertIn(result['obligation_id'], output['unsatisfied_obligations'])

    def test_self_declared_actual_runner_binding_is_not_authentication(self):
        self.fill_synthetic_passes()
        for result in self.manifest['results']:
            result['producer'] = {
                'identity': 'claimed-runner', 'kind': 'ACTUAL_RUNNER',
                'authenticated_binding_ref': self.artifact['path']}
        output = self.run_bundle()
        self.assertTrue(output['structural_valid'])
        self.assertEqual(output['evidence_state'], 'SCOPE EVIDENCE INCOMPLETE')
        self.assertIn(self.manifest['obligations'][0]['id'], output['unverified_producers'])


if __name__ == '__main__':
    unittest.main()
