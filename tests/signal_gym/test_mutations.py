import copy
import tempfile
import unittest
from pathlib import Path
from test_storage import CORPUS
from test_replay import ADAPTER
from storage import *
from mutations import apply_overlay,reduce_case,publish_case
from runner import replay_case
class MutationTests(unittest.TestCase):
    def test_packet_overlay_preserves_parent_and_matches_sidecar(self):
        c=load_corpus(CORPUS)[0];before=digest(c['frames']);n,r=apply_overlay(c,CORPUS.parent/'mutation_overlay.json')
        self.assertEqual(digest(c['frames']),before);self.assertEqual(r['changed_pointers'],['/frames/0/signals/0/producer_frame_id'])
        self.assertEqual(n['expectations']['approval'],'synthetic_reviewed')
        self.assertEqual(replay_case(n,{'source-contract-cpu-v2':ADAPTER})[0]['execution_status'],'PASS')
        with tempfile.TemporaryDirectory() as d:
            publish_case(n,Path(d)/'derived',r);self.assertEqual(load_corpus(Path(d)/'derived/corpus_manifest.json')[0]['input_hash'],n['input_hash'])
    def test_false_precondition(self):
        c=load_corpus(CORPUS)[0];o=parse(read(CORPUS.parent/'mutation_overlay.json'));o['preconditions'][0]['equals']=None
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'MUTATION_NOT_APPLICABLE'):apply_overlay(c,p)
    def test_protected_target(self):
        c=load_corpus(CORPUS)[0];o=parse(read(CORPUS.parent/'mutation_overlay.json'));o['edits'][0]['target']='/frames/0/provenance/distribution'
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'UNSAFE_PATH'):apply_overlay(c,p)
    def test_missing_sidecar_pending(self):
        c=load_corpus(CORPUS)[0];o=parse(read(CORPUS.parent/'mutation_overlay.json'))
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';p.write_bytes(canon(o));n,_=apply_overlay(c,p)
            self.assertEqual(replay_case(n,{'source-contract-cpu-v2':ADAPTER})[0]['execution_status'],'NOT_READY')
    def test_reducer_keeps_same_failure(self):
        c=load_corpus(CORPUS)[0];c['expectations']['qualifications'][0]['expected']['fits']=False
        unused=copy.deepcopy(c['frames'][0]['signals'][0]);unused['signal_id']='unused';unused['provenance']=[];c['frames'][0]['signals'].append(unused)
        c['input_hash']=input_hash(c['frames'],c['environment']);c['expectations']['case_input_sha256']=c['input_hash']
        adapters={'source-contract-cpu-v2':ADAPTER};failure=replay_case(c,adapters)[0];reduced,receipt=reduce_case(c,failure,adapters,16)
        self.assertEqual(len(reduced['frames'][0]['signals']),1);self.assertIn('/frames/0/signals/1',receipt['removed_pointers'])
        self.assertEqual(replay_case(reduced,adapters)[0]['execution_status'],'FAIL')
    def test_reducer_tracks_required_failure_after_optional_mismatch(self):
        c=load_corpus(CORPUS)[0]
        optional=c['expectations']['transport_checks'][0]
        optional.update(required=False,expected='absent')
        required=c['expectations']['qualifications'][0]
        required['expected']['fits']=False
        unused=copy.deepcopy(c['frames'][0]['signals'][0]);unused['signal_id']='unused';unused['provenance']=[]
        c['frames'][0]['signals'].append(unused)
        c['input_hash']=input_hash(c['frames'],c['environment']);c['expectations']['case_input_sha256']=c['input_hash']
        adapters={'source-contract-cpu-v2':ADAPTER};failure=replay_case(c,adapters)[0]
        self.assertEqual(failure['deterministic']['mismatches'][0]['check_id'],optional['check_id'])
        reduced,receipt=reduce_case(c,failure,adapters,16)
        self.assertEqual(receipt['failure_signature'][0],required['check_id'])
        mismatches=replay_case(reduced,adapters)[0]['deterministic']['mismatches']
        self.assertIn(required['check_id'],[m['check_id'] for m in mismatches])
if __name__=='__main__':unittest.main()
