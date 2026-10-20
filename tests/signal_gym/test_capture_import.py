import json
import tempfile
import unittest
from pathlib import Path
from test_storage import CORPUS
from storage import *
from capture_import import capture_import,record_values
from runner import replay_case
class CaptureTests(unittest.TestCase):
    def trace(self,p,gap=False):
        (p/'raw-neurotic.log').write_bytes(b'private C:/Users/Secret 0x123456')
        (p/'harness-manifest.json').write_bytes(b'{}')
        manifest={'schema_version':1,'session_id':'a'*32,'log_sha256':sha((p/'raw-neurotic.log').read_bytes()),'harness_manifest_sha256':sha(b'{}'),'event_count':2,'issues':[]}
        (p/'TRACE-MANIFEST.json').write_bytes(canon(manifest))
        (p/'events.csv').write_text('sequence,pid,native_observation,present_observation,kind,details\n1,777,100,200,begin,private-path\n'+('3' if gap else '2')+',777,101,201,end,0xdeadbeef\n')
    def test_trace_redaction_and_unknowns(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);self.trace(p);capture_import(p,p/'out');c=load_corpus(p/'out/corpus_manifest.json')[0]
            self.assertEqual(c['frames'][0]['identity']['frame_id']['state'],'not_observed')
            self.assertEqual(c['frames'][0]['source_contract_inputs']['trace_observation']['native_observation'],'100')
            data=b''.join(x.read_bytes() for x in (p/'out').rglob('*.json'));self.assertNotIn(b'private-path',data);self.assertNotIn(b'0xdeadbeef',data)
            self.assertEqual(replay_case(c,{})[0]['execution_status'],'NOT_READY')
    def test_gap_partial_and_no_overwrite(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);self.trace(p,True);receipt=capture_import(p,p/'out');self.assertTrue(receipt['partial'])
            with self.assertRaises(FileExistsError):capture_import(p,p/'out')
    def test_raw_hash_damage(self):
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);self.trace(p);(p/'raw-neurotic.log').write_bytes(b'changed')
            with self.assertRaisesRegex(GymError,'HASH_MISMATCH'):capture_import(p,p/'out')
if __name__=='__main__':unittest.main()
