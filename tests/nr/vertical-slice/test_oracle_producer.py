"""Boundary tests for the Python evaluator-oracle adapter; no product claims."""
import json
import unittest

try:
    import oracle_producer as producer
except ImportError:
    producer = None


class OracleProducerTests(unittest.TestCase):
    def test_adapter_exists(self):
        self.assertIsNotNone(producer, 'R03/R07 need authenticated executed Python oracle evidence')

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_exact_rows_and_summary_are_required(self):
        rows = [dict(kind='oracle-assertion', run_id='a'*32, id=name, matched=True)
                for name in producer.PROFILES['r07']['assertions']]
        summary = dict(kind='oracle-summary', run_id='a'*32, profile='r07',
                       checks=len(rows), failures=0, scope='EVALUATOR_TEST_ONLY')
        raw = '\n'.join(json.dumps(row) for row in [*rows, summary])
        self.assertEqual(set(producer.parse_assertions(raw, 'a'*32, 'r07')), {r['id'] for r in rows})
        for altered in ('', raw+'\n'+json.dumps(rows[0]), raw.replace('"matched": true', '"matched": "true"', 1),
                        raw.replace('"failures": 0', '"failures": 1'), raw.replace('"checks": '+str(len(rows)), '"checks": 0')):
            with self.subTest(altered=altered[:60]), self.assertRaises(ValueError):
                producer.parse_assertions(altered, 'a'*32, 'r07')

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_unrelated_failure_and_infrastructure_do_not_kill(self):
        rows = dict.fromkeys(producer.PROFILES['r07']['assertions'], True)
        mutation = 'drop_mandatory_cell'
        self.assertEqual(producer.classify('r07', mutation, 0, rows), 'SURVIVED')
        rows['r07_fixture_complete_only'] = False
        self.assertEqual(producer.classify('r07', mutation, 1, rows), 'SURVIVED')
        rows[producer.MUTATIONS[mutation]['detector']] = False
        self.assertEqual(producer.classify('r07', mutation, 1, rows), 'KILLED')
        self.assertEqual(producer.classify('r07', mutation, 124, rows), 'INFRA_ERROR')
        self.assertEqual(producer.classify('r07', mutation, 125, rows), 'INFRA_ERROR')

    @unittest.skipUnless(producer is not None, 'adapter absence is the intended RED')
    def test_exported_binding_cannot_self_enroll(self):
        result = dict(producer=dict(kind='ACTUAL_RUNNER', identity='copied', authenticated_binding_ref='binding.json'))
        self.assertFalse(producer.verify_oracle_binding('.', result, {}, {})['verified'])
        self.assertFalse(producer.verify_oracle_binding('.', result, {}, {'copied': {}})['verified'])


if __name__ == '__main__':
    unittest.main()
