"""Adversarial adapter controls; these tests do not enroll product authority."""
import pathlib
import json
import tempfile
import unittest
import copy


class ConstructionBatchTests(unittest.TestCase):
    def manifest(self):
        proof = [{'path': 'complete-proof', 'sha256': 'a' * 64, 'kind': 'LOG', 'payload': 'x' * 1024}]
        results = [dict(id='control', obligation_id='C0', mutant=False, control_result_id=None,
                        artifacts=proof, status='PASS', started=True, oracle_met=True)]
        results += [dict(id='mutant' + str(index), obligation_id='C' + str(index), mutant=True,
                         control_result_id='control', artifacts=proof, status='PASS', started=True,
                         oracle_met=True) for index in (1, 2)]
        return dict(obligations=[{'id': 'C' + str(index)} for index in range(129)], results=results,
                    human_g2='NOT_GRANTED_BY_EVALUATOR', w04_authorized=False)

    def test_complete_proofs_and_controls_survive_bounded_batches(self):
        from run_construction_producer import batch_manifests, manifest_bytes
        manifest = self.manifest()
        original = copy.deepcopy(manifest)
        pair = dict(manifest, results=manifest['results'][:2])
        limit = len(manifest_bytes(pair))
        batches = batch_manifests(manifest, max_bytes=limit)
        self.assertEqual(len(batches), 2)
        combined = {}
        for batch in batches:
            self.assertLessEqual(len(manifest_bytes(batch)), limit)
            self.assertEqual(batch['obligations'], original['obligations'])
            ids = {row['id'] for row in batch['results']}
            for row in batch['results']:
                self.assertEqual(row, next(value for value in original['results'] if value['id'] == row['id']))
                if row['mutant']:
                    self.assertIn(row['control_result_id'], ids)
                combined[row['id']] = row
        self.assertEqual(combined, {row['id']: row for row in original['results']})
        self.assertEqual(manifest, original)
        self.assertTrue(all(any(row['id'] == 'control' for row in batch['results']) for batch in batches))

    def test_indivisible_pair_must_fit_exact_serialized_limit(self):
        from run_construction_producer import batch_manifests, manifest_bytes
        manifest = self.manifest()
        manifest['results'] = manifest['results'][:2]
        limit = len(manifest_bytes(manifest))
        self.assertEqual(batch_manifests(manifest, max_bytes=limit), [manifest])
        with self.assertRaisesRegex(ValueError, 'indivisible'):
            batch_manifests(manifest, max_bytes=limit - 1)

    def test_missing_conflicting_or_nonpositive_control_is_refused(self):
        from run_construction_producer import batch_manifests
        for change in ('missing', 'duplicate-id', 'duplicate-obligation', 'mutant-control', 'nonpositive'):
            manifest = self.manifest()
            if change == 'missing':
                manifest['results'][1]['control_result_id'] = 'absent'
            elif change == 'duplicate-id':
                manifest['results'].append(dict(manifest['results'][0], status='FAIL'))
            elif change == 'duplicate-obligation':
                manifest['results'][1]['obligation_id'] = 'C0'
            elif change == 'mutant-control':
                manifest['results'][1]['control_result_id'] = 'mutant2'
            else:
                manifest['results'][0]['status'] = 'FAIL'
            with self.subTest(change=change), self.assertRaises(ValueError):
                batch_manifests(manifest)

    def test_authenticated_union_requires_every_result_and_zero_replay(self):
        from run_construction_producer import evaluate_manifest_batches, manifest_bytes
        manifest = self.manifest()
        limit = len(manifest_bytes(dict(manifest, results=manifest['results'][:2])))
        expected = {row['obligation_id'] for row in manifest['results']}
        capability = {'test-only-sentinel': object()}
        calls = []

        def evaluator(path, *, trusted_runner_runs=None):
            batch = json.loads(path.read_bytes())
            calls.append((batch, trusted_runner_runs))
            accepted = bool(trusted_runner_runs)
            return dict(structural_valid=True, structural_errors=[],
                verified_runner_receipts=[{'result_id': row['id']} for row in batch['results']] if accepted else [],
                unsatisfied_obligations=[row['id'] for row in batch['obligations']
                    if not accepted or row['id'] not in {value['obligation_id'] for value in batch['results']}],
                human_g2_granted=False, w04_authorized=False)

        with tempfile.TemporaryDirectory() as directory:
            result = evaluate_manifest_batches(pathlib.Path(directory), manifest, expected, capability,
                                               evaluator, max_bytes=limit)
        self.assertEqual(set(result['mapped_obligations']), expected)
        self.assertEqual({row['result_id'] for row in result['verified_runner_receipts']},
                         {row['id'] for row in manifest['results']})
        self.assertEqual(len(result['unsatisfied_obligations']), 126)
        self.assertFalse(result['human_g2_granted'])
        self.assertEqual(len(calls), 4)
        self.assertTrue(all(trust is capability for _, trust in calls[::2]))
        self.assertTrue(all(trust is None for _, trust in calls[1::2]))

        for failure in ('missing', 'extra', 'replay', 'g2', 'conflicting-repeat'):
            trusted_count = 0
            def invalid(path, *, trusted_runner_runs=None):
                nonlocal trusted_count
                trusted_count += bool(trusted_runner_runs)
                result = evaluator(path, trusted_runner_runs=trusted_runner_runs)
                if failure == 'missing' and trusted_runner_runs:
                    result['verified_runner_receipts'].pop()
                elif failure == 'extra' and trusted_runner_runs:
                    result['verified_runner_receipts'].append({'result_id': 'unrequested'})
                elif failure == 'replay' and not trusted_runner_runs:
                    result['verified_runner_receipts'].append({'result_id': 'forged'})
                elif failure == 'g2':
                    result['human_g2_granted'] = True
                elif failure == 'conflicting-repeat' and trusted_runner_runs and trusted_count > 1:
                    result['verified_runner_receipts'][0]['unexpected_changed_fact'] = True
                return result
            with self.subTest(failure=failure), tempfile.TemporaryDirectory() as directory:
                with self.assertRaises(ValueError):
                    evaluate_manifest_batches(pathlib.Path(directory), manifest, expected, capability,
                                              invalid, max_bytes=limit)


class ConstructionProducerTests(unittest.TestCase):
    def test_n10_input_checks_do_not_cover_hostreturn_and_c03_oracle(self):
        import gate_evaluator as evaluator
        from construction_profiles import CONSTRUCTION_BINDINGS, PROFILES
        from construction_producer import ROOT
        rows = ('native_local_semantic_use_history', 'local_qualification_not_final_association')
        inventory = json.loads((ROOT / PROFILES['native-adversarial-authority']['inventory']).read_text())
        self.assertTrue(set(rows).issubset(row['id'] for row in inventory['suites']['native-adversarial-authority']))
        receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite='native-adversarial-authority',
            assertion_ids=rows, source_files=evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N03:unit:CPU']['source_sha256'],
            physical_substrate='CPU_CANONICAL_WITH_SYNTHETIC_EXTERIOR')
        result = dict(test_binding='N10.unit.canonical-authority/v1', mutant=False)
        self.assertFalse(evaluator.spectre_runner_covers('N10:unit:CPU', receipt, result),
            'input preparation without HostReturn qualification or C03 use must not satisfy original N10')
        self.assertNotIn('N10:unit:CPU', CONSTRUCTION_BINDINGS)
        self.assertEqual(len(evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS), 46)
        self.assertEqual(sum(row['suite'] == 'native-adversarial-authority'
                             for row in evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS.values()), 20)

    def test_o08_pre_entry_refusal_does_not_prove_untouched_fallback_permission(self):
        import gate_evaluator as evaluator
        from construction_profiles import CONSTRUCTION_BINDINGS, PROFILES
        from construction_producer import ROOT
        rows = ('untouched_refusal_original_once', 'unrestored_parameter_blocks_original')
        inventory = json.loads((ROOT / PROFILES['native-adversarial-authority']['inventory']).read_text())
        self.assertTrue(set(rows).issubset(row['id'] for row in inventory['suites']['native-adversarial-authority']))
        receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite='native-adversarial-authority',
            assertion_ids=rows, source_files=evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N03:unit:CPU']['source_sha256'],
            physical_substrate='CPU_CANONICAL_WITH_SYNTHETIC_EXTERIOR')
        result = dict(test_binding='O08.unit.canonical-authority/v1', mutant=False)
        self.assertFalse(evaluator.spectre_runner_covers('O08:unit:CPU', receipt, result),
            'pre-entry refusal and restoration failure do not prove positive untouched fallback permission')
        self.assertNotIn('O08:unit:CPU', CONSTRUCTION_BINDINGS)

    def test_live_snapshot_includes_reviewed_compiler_library(self):
        from run_construction_producer import snapshot
        from construction_producer import ROOT, sha
        name = 'OptiScaler/library/detours/detours.lib'
        self.assertEqual(snapshot().get(name), sha(ROOT / name))

    def test_semantic_failure_and_survival_are_not_infrastructure(self):
        from construction_producer import classify_comparisons
        self.assertEqual(classify_comparisons({'oracle': True}, 0), 'PASS')
        self.assertEqual(classify_comparisons({'oracle': False}, 1), 'FAIL')
        self.assertEqual(classify_comparisons({'oracle': True}, 0, ('oracle',)), 'SURVIVED')
        self.assertEqual(classify_comparisons({'oracle': False}, 1, ('oracle',)), 'KILLED')
        self.assertEqual(classify_comparisons({'oracle': True, 'unrelated': False}, 1, ('oracle',)), 'SURVIVED')
        for code in (2, 124, 125):
            self.assertEqual(classify_comparisons({'oracle': False}, code, ('oracle',)), 'INFRA_ERROR')

    def test_reviewed_compiler_and_oracle_cells_have_distinct_dispatch(self):
        import gate_evaluator as evaluator
        from oracle_producer import ORACLE_BINDINGS
        for ident, expected in ORACLE_BINDINGS.items():
            self.assertIn(ident, evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS)
            actual = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS[ident]
            self.assertEqual(actual['suite'], expected['suite'])
            self.assertEqual(actual['assertion_ids'], expected['assertion_ids'])
        self.assertEqual(evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS['N12:compile-time:SOURCE_ANALYSIS']['suite'],
                         'native-compile-authority')

    def test_original_supervision_must_match_command_directory_and_deadline(self):
        from construction_producer import verify_stage_intent
        intent = dict(command=['cl.exe', '/c', 'actual.cpp'], cwd='repo', run_id='a' * 32, timeout_seconds=60)
        observation = dict(command=intent['command'], cwd='repo', timeout_seconds=60)
        verify_stage_intent(intent, observation)
        for field, value in [('command', ['cl.exe', '/c', 'other.cpp']), ('cwd', 'other'), ('timeout_seconds', 61)]:
            changed = dict(observation, **{field: value})
            with self.assertRaises(ValueError):
                verify_stage_intent(intent, changed)

    def test_overlay_requires_entire_enrolled_dependency_set(self):
        from construction_producer import verify_overlay_membership
        expected = {'a.h': '1', 'b.cpp': '2'}
        verify_overlay_membership(['a.h', 'b.cpp'], expected, ['a.h', 'b.cpp'])
        for declared, actual in [(['b.cpp'], ['b.cpp']), (['a.h', 'b.cpp'], ['a.h', 'b.cpp', 'extra.h']),
                                 (['a.h', 'a.h', 'b.cpp'], ['a.h', 'b.cpp'])]:
            with self.assertRaises(ValueError):
                verify_overlay_membership(declared, expected, actual)

    def test_compiler_negative_requires_exact_statement_code_and_symbol(self):
        from construction_producer import compiler_oracle
        with tempfile.TemporaryDirectory() as directory:
            source = pathlib.Path(directory) / 'initial_native_authority.cpp'
            source.write_text('header\nO::CurrentRouteView forged(plan);\n')
            valid = str(source) + "(2): error C2665: 'CurrentRouteView': no overloaded function\n"
            self.assertTrue(compiler_oracle(1, source, valid))
            for bad in (valid.replace('(2)', '(1)'), valid.replace('C2665', 'C1083'),
                        valid.replace('CurrentRouteView', 'Unrelated'),
                        valid.replace('error', 'warning'), valid.replace(str(source), 'other.cpp')):
                self.assertFalse(compiler_oracle(1, source, bad))

    def test_exact_source_overlay_rejects_ambiguous_or_nonsemantic_patch(self):
        from construction_producer import substitute
        self.assertEqual(substitute(b'one\r\ntwo\r\n', 'two', 'three'), b'one\r\nthree\r\n')
        for before, old, new in ((b'xx', 'x', 'y'), (b'x', 'x', 'x'), (b'x', 'z', 'y')):
            with self.assertRaises(ValueError):
                substitute(before, old, new)

    def test_no_serialized_or_test_only_enrollment(self):
        from construction_producer import verify_construction_binding
        result = dict(producer=dict(kind='ACTUAL_RUNNER', identity='fake'))
        for authority in ({}, {'fake': {'origin': 'ACTUAL_RUNNER'}}):
            self.assertFalse(verify_construction_binding('.', result, {}, authority)['verified'])

    def test_registration_does_not_imply_original_mapping(self):
        from construction_profiles import COMPILE_PROFILE, members
        self.assertEqual(members(COMPILE_PROFILE), ())
        self.assertEqual(members('finalizer-duplicate-composite'),
                         ('finalizer-exactly-once', 'native-lifecycle'))
        with self.assertRaises(ValueError):
            members('arbitrary-unregistered-source')


if __name__ == '__main__':
    unittest.main()
