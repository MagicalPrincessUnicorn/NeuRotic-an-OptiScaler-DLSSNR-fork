"""CPU oracle and evidence mutations. Synthetic receipts are not GPU results."""
import hashlib
import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[3]
PATH = ROOT / 'tools/nr/lab/rendering/scene/scene_oracle.py'
oracle = None
if PATH.exists():
    spec = importlib.util.spec_from_file_location('scene_oracle', PATH)
    oracle = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(oracle)


class SceneTests(unittest.TestCase):
    def setUp(self):
        self.assertIsNotNone(oracle, 'Controlled scene oracle is not implemented')

    def test_hand_calculated_geometry_and_motion(self):
        self.assertEqual(oracle.pixel(0, 24, 24, 'conventional'), ((.75,.25,.125,1.), .25, (0.,0.), 0))
        self.assertEqual(oracle.pixel(1, 28, 26, 'conventional'), ((.75,.25,.125,1.), .25, (-4.,-2.), 0))
        self.assertEqual(oracle.pixel(1, 24, 24, 'conventional')[1:], (.75, (0.,0.), 1))
        self.assertEqual(oracle.pixel(1, 60, 30, 'conventional')[1:], (.75, (0.,0.), 0))

    def test_inverted_depth_changes_only_depth(self):
        a = oracle.expected_frame(2, 'conventional')
        b = oracle.expected_frame(2, 'inverted')
        for role in ('color','motion','disocclusion'): self.assertEqual(a[role], b[role])
        for x, y in zip(struct.iter_unpack('<f', a['depth']), struct.iter_unpack('<f', b['depth'])):
            self.assertEqual(x[0] + y[0], 1.)

    def test_exact_nontrivial_population(self):
        frame = oracle.expected_frame(1, 'conventional')
        motion = list(struct.iter_unpack('<2e', frame['motion']))
        self.assertEqual(motion.count((-4.,-2.)), 32*24)
        self.assertEqual(sum(v[0] for v in struct.iter_unpack('<I', frame['disocclusion'])), 152)
        self.assertEqual(len(frame['color']), 127*95*8)
        self.assertGreater(len(set(struct.iter_unpack('<4e', frame['color']))), 2)

    def test_rejects_unknown_profile_and_frame(self):
        for f,p in [(3,'conventional'),(-1,'conventional'),(True,'conventional'),(0,'unknown')]:
            with self.subTest(frame=f,profile=p), self.assertRaises(ValueError): oracle.expected_frame(f,p)

    def make_run(self):
        tmp = tempfile.TemporaryDirectory(); self.addCleanup(tmp.cleanup)
        root = Path(tmp.name)
        frames = []
        for f in range(3):
            planes = []
            for role, data in oracle.expected_frame(f, 'conventional').items():
                name = f'frame-{f}-{role}.bin'; (root/name).write_bytes(data)
                fmt,bpp = oracle.FORMATS[role]
                row = 127*bpp
                planes.append(dict(role=role,path=name,sha256=hashlib.sha256(data).hexdigest(),
                    format=fmt,width=127,height=95,bytes=len(data),row_bytes=row,row_pitch=(row+255)//256*256,
                    resource_id=str(100+list(oracle.FORMATS).index(role)),state_after='NON_PIXEL_SHADER_RESOURCE'))
            frames.append(dict(frame_id=str(f),previous_frame_id=str(max(0,f-1)),reset=f==0,
                submitted_fence=str(f+1),completed_fence=str(f+1),wait_result=0,
                device_removed_reason=0,submitted=True,list_closed=True,readback_observed=True,
                command_list_id=str(200+f),allocator_id=str(300+f),planes=planes))
        receipt=dict(schema_version=1,kind='CONTROLLED_SCENE_CAPTURE',status='COMPLETE',
            process_id=42,profile='orthographic-moving-rectangle-v1',depth_convention='conventional',
            backend='warp',width=127,height=95,frame_count=3,queue_type='DIRECT',queue_id='99',
            provider_evaluate_count=0,provider_loaded=False,scene_sha256=hashlib.sha256((PATH.parent/'SCENE.json').read_bytes()).hexdigest(),shader_sha256='2'*64,
            conventions=oracle.CONVENTIONS,frames=frames,
            diagnostics=dict(available=True,complete=True,error_count=0,warning_count=0,read_failures=0,uncaptured=0,storage_denied=0,discarded=0,stored=0,visible=0,captured=0,messages=[]),
            debug_layer_enabled=True,state_provenance='SOURCE_RECORDED',retention='UNTIL_PROCESS_EXIT')
        receipt['diagnostics_startup']=dict(receipt['diagnostics'])
        supervisor=dict(schema_version=1,process_status='exited',child_exit_code=0,child_pid=42,
            child_drained=True,timed_out=False,unhandled_exception=None)
        (root/'scene.json').write_text(json.dumps(receipt))
        (root/'supervisor.json').write_text(json.dumps(supervisor))
        return root, receipt

    def verify(self,root):
        return oracle.validate_run(root, scene_bytes=(PATH.parent/'SCENE.json').read_bytes(), expected_shader_sha256='2'*64)

    def test_valid_synthetic_capture_checks_exact_data(self):
        root,r = self.make_run()
        result = self.verify(root)
        self.assertEqual(result['status'], 'SCENE_BYTES_VALIDATED')
        self.assertEqual(result['bytes_checked'], 127*95*20*3)
        self.assertFalse(result['provider_qualified'])

    def test_detects_wrong_motion_sign_even_with_updated_hash(self):
        root,r = self.make_run(); plane = r['frames'][1]['planes'][2]
        p=root/plane['path']; raw=bytearray(p.read_bytes())
        struct.pack_into('<2e',raw,(26*127+28)*4,4.,2.)
        p.write_bytes(raw);plane['sha256']=hashlib.sha256(raw).hexdigest()
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'ORACLE_MISMATCH'):self.verify(root)

    def test_rejects_replayed_frame(self):
        root,r = self.make_run();r['frames'][1]=r['frames'][0]
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaises(ValueError):self.verify(root)

    def test_rejects_unknown_completion_and_missing_capture(self):
        for change in ('fence','capture','boolean','resource'):
            root,r=self.make_run()
            if change=='fence':r['frames'][1]['completed_fence']=str(2**64-1)
            elif change=='capture':r['diagnostics']['complete']=False
            elif change=='boolean':r['frames'][0]['wait_result']=False
            else:r['frames'][1]['planes'][1]['resource_id']=r['frames'][1]['planes'][0]['resource_id']
            (root/'scene.json').write_text(json.dumps(r))
            with self.subTest(change=change),self.assertRaises(ValueError):self.verify(root)

    def test_rejects_wrong_source_binding(self):
        root,r=self.make_run();r['scene_sha256']='3'*64
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaises(ValueError):self.verify(root)

    def test_rejects_path_escape(self):
        root,r=self.make_run();r['frames'][0]['planes'][0]['path']='../outside.bin'
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaises(ValueError):self.verify(root)

    def test_rejects_failure_receipt(self):
        root,r=self.make_run();r['status']='FAILED'
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaises(ValueError):self.verify(root)

    def test_rejects_duplicate_json_keys(self):
        root,r=self.make_run();p=root/'scene.json'
        p.write_text(p.read_text().replace('{','{"status":"FAILED",',1))
        with self.assertRaises(ValueError):self.verify(root)

    def test_startup_loss_is_retained_and_new_workload_loss_rejected(self):
        root,r=self.make_run()
        for key in ('diagnostics_startup','diagnostics'):
            r[key]['storage_denied']=67;r[key]['complete']=False
        (root/'scene.json').write_text(json.dumps(r))
        result=self.verify(root)
        self.assertEqual(result['startup_storage_denied'],67)
        self.assertFalse(result['whole_lifetime_debug_coverage'])
        r['diagnostics']['storage_denied']=68
        (root/'scene.json').write_text(json.dumps(r))
        with self.assertRaisesRegex(ValueError,'WORKLOAD_CAPTURE_LOSS'):self.verify(root)

    def test_changed_definition_cannot_be_rebound_to_hardcoded_outputs(self):
        for field,value in [('step',[0,0]),('depth',.5)]:
            with self.subTest(field=field):
                root,r=self.make_run()
                scene=json.loads((PATH.parent/'SCENE.json').read_text())
                scene['foreground'][field]=value
                raw=json.dumps(scene).encode();(root/'definition.json').write_bytes(raw)
                (root/'shader.cso').write_bytes(b'synthetic shader identity')
                r['scene_sha256']=hashlib.sha256(raw).hexdigest()
                r['shader_sha256']=hashlib.sha256(b'synthetic shader identity').hexdigest()
                (root/'scene.json').write_text(json.dumps(r))
                argv=['scene_oracle.py','--run',str(root),'--scene',str(root/'definition.json'),
                      '--shader',str(root/'shader.cso'),'--output',str(root/'validation')]
                with self.assertRaisesRegex(ValueError,'SCENE_DEFINITION'),patch('sys.argv',argv):oracle.main()


if __name__=='__main__':unittest.main()
