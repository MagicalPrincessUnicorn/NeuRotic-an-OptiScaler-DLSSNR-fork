import copy
import unittest
from unittest.mock import patch

from test_storage import CORPUS
from storage import GymError, check_expectations, input_hash, load_corpus
from compare import compare
from runner import replay_case


class ExpectationBoundaryTests(unittest.TestCase):
    def case(self, index=0):
        return copy.deepcopy(load_corpus(CORPUS)[index])

    def test_transport_input_cannot_count_as_qualification_or_decision(self):
        for index, group in ((0, 'qualifications'), (9, 'decisions')):
            with self.subTest(group=group):
                case = self.case(index)
                for frame in case['frames']:
                    frame['events'] = []
                check = case['expectations'][group][0]
                check['actual_pointer'] = '/transport/' + case['frames'][0]['record_id'] + '/kind'
                check['expected'] = case['frames'][0]['kind']
                case['input_hash'] = input_hash(case['frames'], case['environment'])
                case['expectations']['case_input_sha256'] = case['input_hash']
                with patch('runner.AdapterProcess') as process:
                    with self.assertRaisesRegex(GymError, 'SCHEMA_MISMATCH'):
                        replay_case(case, {})
                    process.assert_not_called()

    def test_bookkeeping_and_transport_oracle_cannot_qualify(self):
        case = self.case()
        check = case['expectations']['qualifications'][0]
        for field, value in (('actual_pointer', check['actual_pointer'].replace('/output', '/status')),
                             ('oracle_basis', 'transport_preservation')):
            with self.subTest(field=field):
                altered = copy.deepcopy(case)
                altered['expectations']['qualifications'][0][field] = value
                with self.assertRaisesRegex(GymError, 'SCHEMA_MISMATCH'):
                    check_expectations(altered)

    def test_unbound_contract_hash_cannot_qualify(self):
        case = self.case()
        contract = case['expectations']['qualifications'][0]['contract']
        contract['sha256'] = {'state': 'not_observed', 'reason_id': 'GYM.TEST_UNBOUND'}
        case['environment']['required_adapters'][0]['contract_refs'][0] = copy.deepcopy(contract)
        case['input_hash'] = input_hash(case['frames'], case['environment'])
        case['expectations']['case_input_sha256'] = case['input_hash']
        with self.assertRaisesRegex(GymError, 'CONTRACT_MISMATCH'):
            check_expectations(case)

    def operation_comparison(self, index, group):
        case = self.case(index)
        expected = case['expectations']
        expected['transport_checks'] = []
        check = expected[group][0]
        key = check['actual_pointer'].split('/')[2]
        operation = {'status': 'ok', 'domain': check['domain'],
                     'scope': check.get('scope', 'source_policy_only'),
                     'contract': copy.deepcopy(check['contract']),
                     'output': copy.deepcopy(check['expected'])}
        return {'operations': {key: operation}}, expected, operation

    def test_result_must_match_expected_contract_domain_and_decision_scope(self):
        for index, group, field, value in ((0, 'qualifications', 'domain', 'transport'),
                                          (0, 'qualifications', 'contract', None),
                                          (9, 'decisions', 'scope', 'runtime_observation')):
            with self.subTest(group=group, field=field):
                actual, expected, operation = self.operation_comparison(index, group)
                self.assertEqual(compare(copy.deepcopy(actual), expected)['required_failed'], 0)
                operation[field] = value
                with self.assertRaisesRegex(GymError, 'CONTRACT_MISMATCH'):
                    compare(actual, expected)

    def test_refusal_cannot_satisfy_a_null_qualification_output(self):
        actual, expected, operation = self.operation_comparison(0, 'qualifications')
        operation.update(status='refused', output=None)
        expected['qualifications'][0]['expected'] = None
        with self.assertRaisesRegex(GymError, 'QUALIFICATION_UNAVAILABLE'):
            compare(actual, expected)

    def test_transport_only_case_still_passes_without_qualification(self):
        result, _, _ = replay_case(self.case(13), {})
        self.assertEqual('PASS', result['execution_status'])
        self.assertEqual(0, result['deterministic']['qualified_check_count'])
        self.assertEqual(0, result['deterministic']['decision_check_count'])


if __name__ == '__main__':
    unittest.main()
