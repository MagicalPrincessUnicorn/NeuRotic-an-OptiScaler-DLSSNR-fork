"""Evidence-reader regressions; fixtures are synthetic, never GPU evidence."""
import copy
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('input_audit', ROOT / 'tools/nr/lab/streamline/control/input_audit.py')
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class InputAuditTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.planes = {
            'output.rgba16f': struct.pack('<4e', .25, .5, .75, 1.) * 4096,
            'depth.r32f': struct.pack('<f', 1.) * 4096,
            'motion.rg16f': struct.pack('<2e', 0., 0.) * 4096,
        }
        self.runner = {
            'schema_version': 3, 'receipt_kind': 'P1_UPLOAD_READBACK_CONTROL',
            'status': 'P1_UPLOAD_READBACK_CONTROL_PASS', 'process_id': 42,
            'requested_feature_id': 0, 'create_count': 0, 'evaluate_count': 0,
            'module_load': dict(provider=False, core=False, forwarder=False),
            'device_removed_reason': 0,
            'adapter': {'name': 'Microsoft Basic Render Driver', 'vendor_id': 5140},
            'retained_baseline': dict(list_closed=True, submitted=True, fence_signaled=True,
                fence_completed=True, readback_observed=True, submitted_fence_value=1,
                completed_fence_value=1, input_extent=[64,64], output_extent=[64,64],
                format='R16G16B16A16_FLOAT'),
            'readback': {'sha256': hashlib.sha256(self.planes['output.rgba16f']).hexdigest()},
        }
        self.supervisor = dict(schema_version=1, process_status='exited', child_exit_code=0,
            child_pid=42, child_drained=True, timed_out=False, unhandled_exception=None)
        self.control = dict(schema_version=1, classification=self.runner['status'],
            provider_loaded=False, requested_feature_id=0, warp=True,
            pixel_mismatches=0, qualification=False)
        self.write()

    def write(self):
        for name, value in [('runner.json', self.runner), ('supervisor.json', self.supervisor), ('control.json', self.control)]:
            (self.root / name).write_text(json.dumps(value), encoding='utf-8')
        for name, raw in self.planes.items():
            (self.root / name).write_bytes(raw)

    def test_control_pass_is_not_scene_or_provider_pass(self):
        result = audit.audit_run(self.root)
        self.assertEqual(result['status'], 'INPUT_AUDIT_COMPLETE')
        self.assertEqual(result['plumbing'], 'EXACT_P1_CONTROL_BYTES_AND_COMPLETION')
        self.assertEqual(result['scene_readiness'], 'DEGENERATE_CONTROL_ONLY')
        self.assertEqual(result['planes']['depth']['min'], [1.0])
        self.assertEqual(result['planes']['motion']['nonzero_pixels'], 0)
        self.assertEqual(result['planes']['color_copy']['unique_pixels'], 1)
        self.assertFalse(result['provider_evaluated'])
        self.assertFalse(result['source_authenticity_verified'])
        self.assertEqual(result['hang_cause'], 'UNDETERMINED')

    def test_rejects_uncertain_completion_and_process(self):
        for field, value in [('completed_fence_value', 2**64-1), ('fence_completed', False), ('submitted', False)]:
            with self.subTest(field=field):
                old = copy.deepcopy(self.runner)
                self.runner['retained_baseline'][field] = value
                self.write()
                with self.assertRaises(ValueError): audit.audit_run(self.root)
                self.runner = old
        self.supervisor['child_pid'] = 43
        self.write()
        with self.assertRaises(ValueError): audit.audit_run(self.root)

    def test_rejects_provider_or_physical_receipt(self):
        self.runner['module_load']['provider'] = True
        self.write()
        with self.assertRaises(ValueError): audit.audit_run(self.root)
        self.runner['module_load']['provider'] = False
        self.control['warp'] = False
        self.write()
        with self.assertRaises(ValueError): audit.audit_run(self.root)

    def test_rejects_boolean_as_fence_number(self):
        self.runner['retained_baseline']['completed_fence_value'] = True
        self.write()
        with self.assertRaises(ValueError): audit.audit_run(self.root)

    def test_detects_guide_corruption_even_when_output_hash_matches(self):
        self.planes['depth.r32f'] = struct.pack('<f', .5) + self.planes['depth.r32f'][4:]
        self.write()
        with self.assertRaisesRegex(ValueError, 'CONTROL_BYTES_MISMATCH:depth'): audit.audit_run(self.root)

    def test_rejects_nonfinite_and_truncated_planes(self):
        for raw in [struct.pack('<2e', float('nan'), 0.) * 4096, bytes(3)]:
            self.planes['motion.rg16f'] = raw
            self.write()
            with self.assertRaises(ValueError): audit.audit_run(self.root)

    def test_rejects_changed_output_hash(self):
        self.runner['readback']['sha256'] = '0' * 64
        self.write()
        with self.assertRaisesRegex(ValueError, 'OUTPUT_HASH'): audit.audit_run(self.root)

    def test_missing_file_is_not_pass(self):
        (self.root / 'motion.rg16f').unlink()
        with self.assertRaises(OSError): audit.audit_run(self.root)

    def test_rejects_duplicate_json_keys(self):
        p = self.root / 'control.json'
        p.write_text(p.read_text().replace('{', '{"warp":false,', 1))
        with self.assertRaisesRegex(ValueError, 'DUPLICATE'): audit.audit_run(self.root)

    def test_statistics_identify_varied_motion_without_claiming_correctness(self):
        result = audit.plane_stats(struct.pack('<2e', 0.,0.) + struct.pack('<2e', 1.,-2.), '<2e', 2)
        self.assertEqual(result['unique_pixels'], 2)
        self.assertEqual(result['nonzero_pixels'], 1)
        self.assertEqual(result['min'], [0.,-2.])
        self.assertEqual(result['max'], [1.,0.])


if __name__ == '__main__': unittest.main()
