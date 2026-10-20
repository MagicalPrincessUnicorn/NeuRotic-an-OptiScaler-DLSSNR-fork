"""Provider issuer boundary controls; these cannot enroll live authority."""
import unittest
from provider_producer import CAPACITY, PROVIDER_MUTATIONS, PROVIDER_BINDINGS, capacity_rows, adapter, verify_provider_binding


class ProviderProducerTests(unittest.TestCase):
    def test_original_cpu_labels_keep_explicit_physical_scope(self):
        self.assertEqual(set(PROVIDER_BINDINGS), {'L12:unit:CPU', 'L12:mutation:CPU'})
        self.assertEqual(len(PROVIDER_MUTATIONS), 11)
        self.assertTrue(all(row['physical_substrate'] == 'WARP_CONTROLLED_PROVIDER_CONTRACT' for row in PROVIDER_BINDINGS.values()))

    def test_capacity_needs_each_original_comparison_once(self):
        raw = '\n'.join('CAPACITY PASS ' + name for name in CAPACITY)
        self.assertTrue(all(capacity_rows(raw).values()))
        for changed in (raw.split('\n', 1)[1], raw + '\nCAPACITY PASS ' + CAPACITY[0], raw.replace(CAPACITY[1], 'unrelated')):
            with self.assertRaises(ValueError):
                capacity_rows(changed)
        self.assertFalse(capacity_rows(raw.replace('CAPACITY PASS ', 'CAPACITY FAIL ', 1))[CAPACITY[0]])

    def test_actual_software_adapter_is_not_hardware_evidence(self):
        raw = 'WARP_ADAPTER name=Microsoft Basic Render Driver vendor=5140 device=140 luid_low=20792 luid_high=0'
        self.assertEqual(adapter(raw), raw)
        for changed in (raw.replace('5140', '4318'), raw.replace('device=140', 'device=1'), raw + '\n' + raw, ''):
            with self.assertRaises(ValueError):
                adapter(changed)

    def test_saved_receipt_cannot_enroll_capability(self):
        result = dict(producer=dict(kind='ACTUAL_RUNNER', identity='external'))
        for runs in ({}, {'external': dict(origin='ACTUAL_RUNNER')}):
            self.assertFalse(verify_provider_binding('.', result, {}, runs)['verified'])


if __name__ == '__main__':
    unittest.main()
