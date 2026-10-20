import copy
import tempfile
import unittest
from pathlib import Path
from test_storage import CORPUS
from test_replay import ADAPTER
from storage import *
from mutations import apply_overlay,publish_case,reduce_case
from runner import replay_case
import mutations
from payloads import materialize

class PayloadTests(unittest.TestCase):
    def recipe(self):
        return {'kind':'procedural','generator':'constant_bits_v1','format':'R8G8B8A8_UNORM','extent':{'width':2,'height':2},'texel_a_hex':'01020304','texel_b_hex':'01020304','tile_size':1,'expected_raw_sha256':{'state':'not_observed','reason_id':'GYM.NOT_OBSERVED'}}
    def overlay(self,case,payload):
        o=parse(read(CORPUS.parent/'mutation_overlay.json'))
        o.update(layer='payload',preconditions=[],expected_sidecar_ref={'state':'not_observed','reason_id':'GYM.NO_SIDECAR'})
        o['edits']=[{'op':'set','target':'/frames/0/resources/0/payload','before_sha256':{'state':'not_observed','reason_id':'GYM.NOT_OBSERVED'},'value':payload}]
        return o

    def test_verified_synthetic_raw_overlay_publishes_and_keeps_parent(self):
        c=load_corpus(CORPUS)[0];before=digest(c['frames']);data=bytes.fromhex('0000803f');h=sha(data)
        payload={'kind':'chunked_raw','format':'R32_FLOAT','packing':'tightly_packed_subresource_le_v1','extent':{'width':1,'height':1},'bytes_per_texel':4,'raw_bytes':'4','raw_sha256':h,'chunks':[{'sha256':h,'bytes':'4'}]}
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';p.write_bytes(canon(self.overlay(c,payload)));(Path(d)/'payloads').mkdir();(Path(d)/'payloads'/f'{h}.bin').write_bytes(data)
            n,r=apply_overlay(c,p);self.assertEqual(digest(c['frames']),before)
            self.assertEqual(n['verified_blobs'][h],data);self.assertEqual(n['expectations']['approval'],'pending_review')
            publish_case(n,Path(d)/'out',r);self.assertEqual(load_corpus(Path(d)/'out/corpus_manifest.json')[0]['input_hash'],n['input_hash'])
            (Path(d)/'payloads'/f'{h}.bin').write_bytes(b'bad!')
            with self.assertRaisesRegex(GymError,'HASH_MISMATCH'):apply_overlay(c,p)

    def test_recipe_hash_verified_and_payload_layer_cannot_edit_authority(self):
        c=load_corpus(CORPUS)[0];payload=self.recipe()
        payload['expected_raw_sha256']={'state':'known','value':'0'*64}
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';o=self.overlay(c,payload);p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'HASH_MISMATCH'):apply_overlay(c,p)
            o['edits'][0]['target']='/frames/0/identity/authority';o['edits'][0]['value']='record_only';p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'UNSAFE_PATH'):apply_overlay(c,p)

    def test_payload_shape_and_record_layer_cannot_bypass_payload_policy(self):
        c=load_corpus(CORPUS)[0]
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';o=self.overlay(c,None);p.write_bytes(canon(o))
            with self.assertRaises(GymError):apply_overlay(c,p)
            o=self.overlay(c,self.recipe());o['layer']='record';p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'UNSAFE_PATH'):apply_overlay(c,p)
            resource=copy.deepcopy(c['frames'][0]['resources'][0]);resource['payload']=self.recipe()
            o['edits'][0].update(target='/frames/0/resources/0',value=resource);p.write_bytes(canon(o))
            with self.assertRaisesRegex(GymError,'UNSAFE_PATH'):apply_overlay(c,p)

    def test_payload_reduction_preserves_real_failure_and_reports_region(self):
        c=load_corpus(CORPUS)[0];c['frames'][0]['resources'][0]['payload']=self.recipe()
        c['expectations']['qualifications'][0]['expected']['fits']=False
        c['input_hash']=input_hash(c['frames'],c['environment']);c['expectations']['case_input_sha256']=c['input_hash']
        adapters={'source-contract-cpu-v2':ADAPTER};failure=replay_case(c,adapters)[0]
        n,r=reduce_case(c,failure,adapters,32)
        self.assertTrue(r.get('payload_regions'))
        self.assertNotEqual(r.get('payload_regions'),'NOT_READY')
        self.assertEqual(replay_case(n,adapters)[0]['execution_status'],'FAIL')
        with tempfile.TemporaryDirectory() as d:
            publish_case(n,Path(d)/'out',r);load_corpus(Path(d)/'out/corpus_manifest.json')

    def test_transport_campaign_rejects_malformed_bytes_without_replay(self):
        c=load_corpus(CORPUS)[0];o=self.overlay(c,None);o['layer']='transport_bytes'
        o['edits'][0].update(target='/frames/0',value={'offset':0,'delete_bytes':1,'insert_hex':'ff'})
        with tempfile.TemporaryDirectory() as d:
            p=Path(d)/'overlay.json';p.write_bytes(canon(o))
            self.assertTrue(hasattr(mutations,'transport_campaign'),'loader-negative campaign is implemented')
            r=mutations.transport_campaign(c,p,Path(d)/'out')
            self.assertEqual(r['loader_status'],'INVALID_CORPUS');self.assertEqual(r['reason_id'],'GYM.INVALID_JSON')
            self.assertEqual(r['adapter_execution'],'NOT_RUN')
            self.assertTrue((Path(d)/'out/TRANSPORT_MANIFEST.json').is_file())

    def test_declared_raw_size_bounds_owned_chunk_before_join(self):
        data=b'a'*8;h=sha(data)
        payload={'kind':'chunked_raw','format':'R32_FLOAT','packing':'tightly_packed_subresource_le_v1','extent':{'width':1,'height':1},'bytes_per_texel':4,'raw_bytes':'4','raw_sha256':h,'chunks':[{'sha256':h,'bytes':'8'}]}
        with self.assertRaisesRegex(GymError,'CAPACITY_EXCEEDED'):materialize(payload,{h:data},4)

    def test_zero_extent_recipe_does_not_generate_unused_rows(self):
        p=self.recipe();p.update(generator='checkerboard_rgba8_v1',extent={'width':4294967295,'height':0})
        # With a verified empty payload hash, no width-sized work is necessary.
        p['expected_raw_sha256']={'state':'known','value':sha(b'')}
        self.assertEqual(materialize(p,{}),b'')

if __name__=='__main__':unittest.main()
