import importlib.util
from pathlib import Path
import unittest
import copy
import json
import os

ROOT = Path(__file__).resolve().parents[3]
SPEC = importlib.util.spec_from_file_location('bridge_stage', ROOT / 'tools/nr/lab/w03_bridge/stage.py')
bridge = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(bridge)
HARNESS = Path(os.environ.get('W03_BRIDGE_HARNESS',
    'C:/NeuRotic/_exports/NeuRotic-DiagnosticBatch-20260926-v4/packages/nr-host/source/tools/nr/lab/rendering'))

class StageTests(unittest.TestCase):
    def test_scene_hooks_preserve_real_submission(self):
        source = (HARNESS / 'scene/ControlledScene.cpp').read_text()
        staged = bridge.patch_scene(source)
        self.assertEqual(staged.count('w03_bridge::PrepareRecording('), 1)
        self.assertEqual(staged.count('w03_bridge::FinishRecording('), 1)
        self.assertLess(staged.index('w03_bridge::PrepareRecording('), staged.index('list->Dispatch('))
        self.assertLess(staged.index('host.Complete('), staged.index('w03_bridge::FinishRecording('))
        self.assertEqual(staged.count('queue->ExecuteCommandLists(1,lists)'), 1)

    def test_changed_source_anchor_refuses(self):
        with self.assertRaisesRegex(ValueError, 'ANCHOR'):
            bridge.patch_scene('unreviewed scene')

    def test_contract_oracle_is_reused(self):
        source = (HARNESS / 'nr_host/ForwarderContract.cpp').read_text()
        shared = bridge.shared_contract(source)
        self.assertIn('Binding ValidateWrites(', shared)
        self.assertIn('void ValidateFrame(', shared)
        self.assertNotIn('dlssnr_forwarder.cpp', shared)
        self.assertNotIn('Json CaptureWrites(', shared)

    def test_duplicate_contract_marker_refuses(self):
        source=(HARNESS/'nr_host/ForwarderContract.cpp').read_text()
        with self.assertRaisesRegex(ValueError,'ANCHOR'):
            bridge.shared_contract(source+'\n// namespace nr_host::detail {')

    def test_missing_bridge_evidence_refuses(self):
        spec=importlib.util.spec_from_file_location('bridge_verify',ROOT/'tools/nr/lab/w03_bridge/verify.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        with self.assertRaises(ValueError): module.validate_bridge({'nr_host':{'frames':[]}})

@unittest.skipUnless(os.environ.get('W03_BRIDGE_EVIDENCE'),'needs a real controlled receipt')
class EvidenceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        spec=importlib.util.spec_from_file_location('bridge_verify',ROOT/'tools/nr/lab/w03_bridge/verify.py')
        cls.verify=importlib.util.module_from_spec(spec);spec.loader.exec_module(cls.verify)
        cls.receipt=json.loads(Path(os.environ['W03_BRIDGE_EVIDENCE']).read_text())
    def test_observed_run(self): self.verify.validate_bridge(self.receipt)
    def test_reject_mutations(self):
        mutations=[('native_result',0),('entry_calls',0),('provider_loaded',True),('normal_source_enrollment',True),
                   ('host_return','PASS'),('history_reset_pending',True),('release_calls',1),('writes',[])]
        for key,value in mutations:
            with self.subTest(field=key):
                raw=copy.deepcopy(self.receipt);raw['nr_host']['frames'][0]['contract']['w03_bridge'][key]=value
                with self.assertRaises(ValueError):self.verify.validate_bridge(raw)
    def test_terminal_measurements_required(self):
        raw=copy.deepcopy(self.receipt);terminal=raw['nr_host']['frames'][0]['contract']['w03_bridge']['terminal']
        terminal['completion']=[dict(timeline=1,required=10,observed=9)]
        with self.assertRaises(ValueError):self.verify.validate_bridge(raw)
    def test_release_readiness_must_be_observed(self):
        raw=copy.deepcopy(self.receipt)
        raw['nr_host']['frames'][0]['contract']['w03_bridge']['release_admission_before_reset']={
            'provider_pending':0,'recording_held':False,'history_closed':True,'gpu_reusable':True,'ready':True}
        with self.assertRaises(ValueError):self.verify.validate_bridge(raw)

if __name__ == '__main__': unittest.main()
