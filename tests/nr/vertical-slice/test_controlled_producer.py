"""Controlled verifier negatives; these fixtures are never live WARP evidence."""
import copy
import unittest
import pathlib

try:
    import controlled_producer as producer
except ImportError:
    producer = None


class ControlledProducerTests(unittest.TestCase):
    def test_original_controlled_cells_require_complete_independent_row_groups(self):
        import gate_evaluator as evaluator
        required = {
            'O05': ('actual upload records source content revision',
                    'genuine identity preserves existing content version without commands',
                    'Identity refuses a distinct current destination without content or edge changes'),
            'L04': ('recorded commands are not completion or release',
                    'reset negative has actual pending commands',
                    'observed reset cancels unsubmitted recording without readback success',
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
        }
        for case, rows in required.items():
            key = case + ':controlled D3D12 host:WARP_MECHANICS'
            with self.subTest(case=case):
                self.assertIn(key, evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS)
                mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS[key]
                self.assertTrue(set(rows).issubset(mapping['assertion_ids']))
                receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite='owned-continuation',
                    assertion_ids=list(mapping['assertion_ids']), source_files=mapping['source_sha256'],
                    physical_substrate='WARP_CONTROLLED_COPY')
                result = dict(test_binding=mapping['test_binding'], mutant=False)
                self.assertTrue(evaluator.spectre_runner_covers(key, receipt, result))
                for row in rows:
                    missing = [value for value in receipt['assertion_ids'] if value != row]
                    self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, assertion_ids=missing), result))
                self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, origin='TEST_ONLY'), result))
                self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, physical_substrate='HARDWARE'), result))
                self.assertFalse(evaluator.spectre_runner_covers(case + ':unit:CPU', receipt, result))

    def test_l07_mapping_requires_reviewed_sources_exact_oracle_and_substrate(self):
        import gate_evaluator as evaluator
        for key in ('L07:controlled D3D12 host:WARP_MECHANICS', 'L07:mutation:CPU'):
            mapping = evaluator.SPECTRE_REVIEWED_RUNNER_BINDINGS[key]
            self.assertTrue(mapping['source_sha256'])
            root = pathlib.Path(__file__).resolve().parents[3]
            for name, value in mapping['source_sha256'].items():
                self.assertEqual(producer.sha(root / name), value, name)
            receipt = dict(verified=True, origin='ACTUAL_RUNNER', suite=mapping['suite'],
                assertion_ids=list(producer.L07_ASSERTIONS), source_files=dict(mapping['source_sha256']),
                physical_substrate='WARP_CONTROLLED_COPY', mutation_names=list(producer.L07_MUTANTS))
            result = dict(test_binding=mapping['test_binding'], mutant=mapping.get('mutant', False))
            self.assertTrue(evaluator.spectre_runner_covers(key, receipt, result))
            for field, changed in (('physical_substrate', 'HARDWARE_CONTROLLED_COPY'), ('origin', 'TEST_ONLY'),
                                   ('assertion_ids', list(producer.L07_ASSERTIONS)[1:])):
                with self.subTest(key=key, field=field):
                    self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, **{field: changed}), result))
            for name in ('tests/nr/spectre/owned_continuation.cpp', 'tests/nr/spectre/owned_process.py',
                         'OptiScaler/dlssnr/NrGpuSafety.cpp', 'tests/nr/spectre/run_owned_continuation.py'):
                altered = dict(receipt['source_files'], **{name: '0' * 64})
                self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, source_files=altered), result))
            if result['mutant']:
                self.assertFalse(evaluator.spectre_runner_covers(key, dict(receipt, mutation_names=[]), result))

    def test_adapter_exists(self):
        self.assertIsNotNone(producer, 'L07 requires an authentic controlled-run adapter')

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_raw_assertion_rows_require_exact_summary_and_no_duplicates(self):
        import json
        text = '\n'.join('ASSERT ' + json.dumps(dict(name=name, matched=True))
                         for name in producer.L07_ASSERTIONS)
        text += f'\nowned-continuation checks={len(producer.L07_ASSERTIONS)} failed=0; controlled mechanics/readback only; no NR/provider/game/BeforeFg/seal\n'
        self.assertEqual(set(producer.assertions(text)), set(producer.L07_ASSERTIONS))
        for changed in (text.replace('failed=0', 'failed=1'), text + text,
                        text.replace('"matched": true', '"matched": "true"', 1),
                        text.replace('ASSERT ', '', 1)):
            with self.subTest(changed=changed[:80]):
                with self.assertRaises(ValueError):
                    producer.assertions(changed)

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_supervised_zero_and_intended_nonzero_are_distinct_from_infrastructure(self):
        value = dict(process_created=True, user_code_started=True, group_assigned_before_resume=True,
                     parent_exit_observed=True, group_empty_observed=True, capture_complete=True,
                     numeric_exit_code=0, last_active_processes=0, process_id=100,
                     timed_out=False, termination_requested=False, errors=[], cleanup_errors=[])
        producer.supervised(value, 0)
        nonzero = dict(value, numeric_exit_code=1)
        producer.supervised(nonzero, 1)
        for changes in ({'numeric_exit_code': 1}, {'timed_out': True}, {'group_empty_observed': False},
                        {'capture_complete': False}, {'errors': ['launch']}, {'cleanup_errors': ['held']}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                producer.supervised(dict(value, **changes), 0)

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_untrusted_export_cannot_self_enroll(self):
        result = dict(producer=dict(identity='copied', kind='ACTUAL_RUNNER',
                                    authenticated_binding_ref='binding.json'))
        self.assertFalse(producer.verify_controlled_binding('.', result, {}, {})['verified'])
        self.assertFalse(producer.verify_controlled_binding('.', result, {}, {'copied': {}})['verified'])


if __name__ == '__main__':
    unittest.main()
