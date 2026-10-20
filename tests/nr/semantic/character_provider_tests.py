import sys, unittest
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/nr_semantic_worker'))
from providers.zoo_character import anchor_grid, nms_corners, ZooCpuProvider, prepare_pose, _load_class
import numpy as np

class ProviderTests(unittest.TestCase):
    def test_invalid_person_region_does_not_consume_crowd_capacity(self):
        image=np.zeros((100,100,3),dtype=np.uint8)
        # The first candidate has a degenerate virtual body circle. Lower
        # confidence, valid people should still fill the requested two slots.
        candidates=np.array([
            [5,5,15,35,10,20,10,20,0,0,0,0,.95],
            [25,5,35,35,30,20,30,5,0,0,0,0,.90],
            [65,5,75,35,70,20,70,5,0,0,0,0,.85]],float)
        provider=ZooCpuProvider.__new__(ZooCpuProvider)
        provider.detector=SimpleNamespace(infer=lambda image:candidates)
        people=provider.analyze(image,2,with_pose=False)
        self.assertEqual(len(people),2)
        self.assertEqual(provider.stats['candidates'],3)
        self.assertEqual(provider.stats['returned'],2)
        np.testing.assert_allclose([p['box'] for p in people],
                                   [[.15,.05,.45,.35],[.55,.05,.85,.35]])

    def test_fast_crowd_keeps_regions_without_pose(self):
        image=np.zeros((400,400,3),dtype=np.uint8)
        candidates=np.array([[x,10,x+10,40,x+5,40,x+5,10,0,0,0,0,.9] for x in range(10,330,20)],float)
        provider=ZooCpuProvider.__new__(ZooCpuProvider)
        provider.detector=SimpleNamespace(infer=lambda image:candidates)
        provider.pose=SimpleNamespace(model=SimpleNamespace(forward=lambda *args:self.fail('pose should not run in fast mode')))
        for cap in (8,16):
            people=provider.analyze(image,cap,with_pose=False)
            self.assertEqual(len(people),cap)
            self.assertTrue(all(p['landmarks']==[] and p['box_semantics']=='estimated_person_region' for p in people))
    def test_multiple_person_caps_and_failed_candidate(self):
        class Model:
            calls=0
            def setInput(self,blob): pass
            def getUnconnectedOutLayersNames(self): return []
            def forward(self,names): self.calls+=1;return [self.calls,None,None,None,None]
        candidates=np.array([[10+i*20,10,20+i*20,40,15+i*20,40,15+i*20,10,0,0,0,0,score] for i,score in enumerate((.9,.8,.7))],float)
        image=np.zeros((100,100,3),dtype=np.uint8)
        prepare=(np.zeros((1,256,256,3),np.float32),None,0,None,None)
        def decoded(pose,outputs,*args):
            if getattr(pose,'fail_first',False) and outputs[0]==1:return None
            points=np.zeros((33,5));points[:,0]=10+outputs[0]*20+np.arange(33)%3
            points[:,1]=20+np.arange(33);points[:,3:]=1
            return points,.9
        with patch('providers.zoo_character.prepare_pose',return_value=prepare),patch('providers.zoo_character.decode_landmarks',side_effect=decoded):
            for cap,expected in ((1,1),(2,2),(4,3)):
                provider=ZooCpuProvider.__new__(ZooCpuProvider)
                provider.detector=SimpleNamespace(infer=lambda image:candidates);provider.pose=SimpleNamespace(model=Model())
                persons=provider.analyze(image,cap,with_pose=True)
                self.assertEqual(len(persons),expected)
                self.assertEqual(provider.pose.model.calls,expected)
                self.assertEqual(len({tuple(p['box']) for p in persons}),expected)
                self.assertTrue(all(p['box_semantics']=='estimated_person_region' for p in persons))
            provider.pose=SimpleNamespace(model=Model(),fail_first=True)
            persons=provider.analyze(image,3,with_pose=True)
            self.assertEqual(len(persons),3)
            self.assertEqual(persons[0]['box_semantics'],'estimated_person_region')
            self.assertEqual(provider.stats['pose_attempts'],3)
            self.assertEqual(provider.stats['usable_poses'],2)
    def test_fast_pose_matches_pinned_transform(self):
        from providers.zoo_character import decode_landmarks
        Base=_load_class('models/pose_estimation_mediapipe/mp_pose.py','MPPose')
        pose=Base.__new__(Base);pose.input_size=np.array([256,256]);pose.conf_threshold=.5;pose.PERSON_BOX_ENLARGE_FACTOR=1.25
        rng=np.random.default_rng(123)
        points=rng.uniform(0,256,(1,195)).astype(np.float32);points.reshape(39,5)[:,3:]=rng.uniform(-5,5,(39,2))
        outputs=[points,np.array([[.9]],np.float32),np.zeros((1,256,256,1),np.float32),np.zeros((1,64,64,39),np.float32),np.zeros((1,117),np.float32)]
        import cv2 as cv
        for angle in (0,27,-42):
            roi=np.array([[0,0],[128,160]]);rotation=cv.getRotationMatrix2D((64,80),angle,1);pad=np.array([8,12])
            raw=pose._postprocess([v.copy() for v in outputs],roi,angle,rotation,pad,np.array([256,256]))
            fast=decode_landmarks(pose,[v.copy() for v in outputs],roi,angle,rotation,pad)
            np.testing.assert_allclose(raw[1],fast[0],atol=2e-5)
    def test_object_letterbox_mapping_and_caps(self):
        from providers.zoo_objects import ObjectDetector
        provider=ObjectDetector.__new__(ObjectDetector)
        # A 400x200 image has 104px top padding and is resized to 416x208.
        rows=np.array([[x,104,x+20,208,.9,i%80] for i,x in enumerate(range(0,320,20))],float)
        provider.detector=SimpleNamespace(infer=lambda canvas:rows)
        image=np.zeros((200,400,3),np.uint8)
        result,count=provider.analyze(image,16)
        self.assertEqual(count,16);self.assertEqual(len(result),16)
        np.testing.assert_allclose(result[0]['box'],[0,0,20/416,.5])
        self.assertEqual(result[1]['class_id'],1)
        self.assertTrue(all(p['landmarks']==[] for p in result))
    def test_offline_detailed_detector_region_contract(self):
        from inspector_contract import validate_result
        person=dict(box=[.1,.1,.5,.8],score=.9,class_id=0,box_semantics='detector_box',facing='unknown',landmarks=[dict(x=.2,y=.3,z_relative=0,visibility=.9,presence=.9) for _ in range(33)])
        packet=dict(schema=1,origin='offline_image',image_sha256='a'*64,width=1920,height=1080,persons=[person])
        self.assertIs(validate_result(packet),packet)
        person['class_id']=5
        with self.assertRaises(ValueError):validate_result(packet)
    def test_anchor_shape(self):
        self.assertEqual(anchor_grid().shape,(2254,2))
    def test_nms_uses_corner_extent(self):
        boxes=np.array([[100,100,110,110],[120,100,130,110]],dtype=float)
        self.assertEqual(len(nms_corners(boxes,np.array([.9,.8]),.5,.3)),2)
    def test_bad_tensor_is_error(self):
        with self.assertRaises(ValueError):
            nms_corners(np.array([[0,0,float('nan'),1]]),np.array([.9]),.5,.3)
    def test_model_mismatch_is_error(self):
        with self.assertRaises((OSError,ValueError)):
            ZooCpuProvider(ROOT/'missing-detector',ROOT/'missing-pose')
    def test_actual_upstream_preprocessing_keeps_caller(self):
        klass=_load_class('models/pose_estimation_mediapipe/mp_pose.py','MPPose')
        pose=klass.__new__(klass);pose.input_size=(256,256);pose.PERSON_BOX_PRE_ENLARGE_FACTOR=1
        image=np.zeros((128,128,3),dtype=np.uint8)
        person=np.array([20,10,100,120,64,64,64,14,30,30,90,30,.9],dtype=float)
        original=person.copy();pixels=image.copy()
        blob,*_=prepare_pose(pose,image,person)
        self.assertEqual(blob.shape,(1,256,256,3))
        self.assertEqual(blob.dtype,np.float32)
        np.testing.assert_array_equal(person,original)
        np.testing.assert_array_equal(image,pixels)
    def test_degenerate_roi_is_rejected_before_upstream_allocation(self):
        person=np.zeros(13);person[4:8]=[10,10,10,10]
        with self.assertRaises(ValueError):prepare_pose(None,np.zeros((64,64,3),np.uint8),person)

if __name__=='__main__': unittest.main()
