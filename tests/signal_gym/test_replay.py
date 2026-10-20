import copy
import os
import tempfile
import unittest
from pathlib import Path
from test_storage import CORPUS
from storage import load_corpus,digest
from runner import replay_case
from lab_case import run_lab_case
ADAPTER=os.environ.get('SIGNAL_GYM_ADAPTER','C:/NeuRotic/builds/signal-gym-cpu/nr_signal_gym_source_adapter.exe')
@unittest.skipUnless(Path(ADAPTER).is_file(),'native source adapter required')
class ReplayTests(unittest.TestCase):
    def test_all_seeds_fresh_process_determinism(self):
        for case in load_corpus(CORPUS):
            with self.subTest(case=case['case_id']):
                a=replay_case(case,{'source-contract-cpu-v2':ADAPTER})[0]
                b=replay_case(case,{'source-contract-cpu-v2':ADAPTER})[0]
                self.assertEqual(a['execution_status'],'PASS');self.assertEqual(a['behavior_sha256'],b['behavior_sha256'])
    def test_golden_independence(self):
        with tempfile.TemporaryDirectory() as d:self.assertTrue(run_lab_case(Path(d),Path(ADAPTER),CORPUS)['success'])
    def test_missing_adapter_is_not_refusal(self):
        r=replay_case(load_corpus(CORPUS)[0],{})[0]
        self.assertEqual(r['execution_status'],'NOT_READY');self.assertFalse(r['deterministic']['operations'])
    def test_pending_review_does_not_pass(self):
        c=load_corpus(CORPUS)[0];c['expectations']['approval']='pending_review'
        self.assertEqual(replay_case(c,{'source-contract-cpu-v2':ADAPTER})[0]['execution_status'],'NOT_READY')
if __name__=='__main__':unittest.main()
