import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[3]/'tools/nr/lab/streamline/control'))
from discovery_stages import summarize

def event(seq, event_type, **data):
    return {'seq': seq, 'event': event_type, 'data': data}

class Stages(unittest.TestCase):
    def test_init_success_and_identity_refusal_are_separate(self):
        r = summarize([event(0, 'api_return', name='slInit', result=0),
                       event(1, 'module_identity', phase='after_init', errors=['FOREIGN_MODULE']),
                       event(2, 'terminal', reason='MODULE_IDENTITY_REFUSED')])
        self.assertIsNone(r['first_nonzero_public_api'])
        self.assertEqual(r['terminal']['data']['reason'], 'MODULE_IDENTITY_REFUSED')
        self.assertEqual(r['module_identity_boundaries'][0]['phase'], 'after_init')
        self.assertEqual(r['module_identity_boundaries'][0]['errors'], ['FOREIGN_MODULE'])
        self.assertEqual(r['stages']['L3'], 'NOT_OBSERVED')

    def test_internal_text_cannot_supply_public_error(self):
        r = summarize([], 'prefix\ngetNGXFeatureRequirements ngxResult not implemented\n')
        self.assertIsNone(r['first_nonzero_public_api'])
        self.assertIsNone(r['internal_log_observations'][0]['public_api_result'])
        self.assertEqual(r['internal_log_observations'][0]['line'], 2)

    def test_actual_support_failure_is_first_nonzero(self):
        r = summarize([event(0, 'api_return', name='slInit', result=0),
                       event(1, 'api_return', name='slIsFeatureSupported', result=32),
                       event(2, 'api_return', name='slGetFeatureFunction', result=7)])
        self.assertEqual(r['first_nonzero_public_api']['stage'], 'L3')
        self.assertEqual(r['first_nonzero_public_api']['result'], 32)
        self.assertFalse(r['provider_admission'])

    def test_enter_without_return_is_not_success(self):
        r = summarize([event(0, 'api_enter', name='slInit')])
        self.assertEqual(r['stages']['L2'], 'NOT_OBSERVED')
        self.assertEqual(r['public_api_returns'], [])

if __name__ == '__main__':
    unittest.main()
