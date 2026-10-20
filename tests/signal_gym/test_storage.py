import copy
import sys
import unittest
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parents[2] / 'tools/signal_gym'))
from storage import *
import storage
CORPUS = Path(__file__).resolve().parents[1] / 'data/nr_signal_gym/v2/corpus_manifest.json'
class StorageTests(unittest.TestCase):
    def test_all_seeds(self): self.assertEqual(len(load_corpus(CORPUS)), 16)
    def test_strict_json(self):
        for b in [b'{"a":1,"a":2}', b'\xef\xbb\xbf{}', b'1.0', b'1e1', b'-0', b'NaN', b'"\\ud800"', b'9007199254740992', b'{"\xc3\xa9":0}']:
            with self.subTest(b=b), self.assertRaises(GymError): parse(b)
    def test_canonical(self): self.assertEqual(digest(parse(b'{ "b":0, "a":false}')), digest(parse(b'{"a":false,"b":0}')))
    def test_paths(self):
        for s in ['../x', 'C:/x', '/x', 'a\\b', 'a//b', 'CON.txt', 'a./b']:
            with self.subTest(s=s), self.assertRaises(GymError): safe_path(CORPUS.parent, s)
    def test_damaged_references(self):
        c = load_corpus(CORPUS)[0]; c['frames'][0]['resources'] = []
        with self.assertRaisesRegex(GymError, 'REFERENCE_MISSING'): validate_frames(c['frames'], c['environment'])
    def test_expectation_binding(self):
        c=load_corpus(CORPUS)[0]; c['expectations']['case_input_sha256']='0'*64
        with self.assertRaisesRegex(GymError, 'HASH_MISMATCH'): check_expectations(c)
    def test_shared_provenance_dag_is_linear_and_valid(self):
        visits=[]
        class CountedParents(list):
            def __iter__(self):
                for p in super().__iter__():
                    visits.append(p)
                    yield p
        graph={f'claim-{i}':CountedParents([f'claim-{j}' for j in range(max(0,i-16),i)]) for i in range(64)}
        storage.validate_lineage(graph)
        self.assertLessEqual(len(visits),2*sum(len(v) for v in graph.values()))
        c=load_corpus(CORPUS)[0];frame=c['frames'][0];template=copy.deepcopy(frame['signals'][0]);claim=copy.deepcopy(template['provenance'][0]);frame['signals']=[]
        for start in range(0,64,16):
            signal=copy.deepcopy(template);signal['signal_id']=f'signal-{start}';signal['provenance']=[]
            for i in range(start,start+16):
                node=copy.deepcopy(claim);node['claim_id']=f'claim-{i}';node['lineage_parents']=list(graph[node['claim_id']]);signal['provenance'].append(node)
            frame['signals'].append(signal)
        validate_frames(c['frames'],c['environment'])
    def test_lineage_cycle_and_missing_reference_still_rejected(self):
        with self.assertRaisesRegex(GymError,'PROVENANCE_CYCLE'):storage.validate_lineage({'a':['b'],'b':['a']})
        with self.assertRaisesRegex(GymError,'REFERENCE_MISSING'):storage.validate_lineage({'a':['missing']})
if __name__ == '__main__': unittest.main()
