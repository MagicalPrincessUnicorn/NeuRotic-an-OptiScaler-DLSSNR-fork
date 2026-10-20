import sys,time,threading,unittest,hashlib
from pathlib import Path
ROOT=Path(__file__).resolve().parents[3];sys.path.insert(0,str(ROOT/'tools/nr_semantic_worker'))
from character_tracking_tests import texture,shifted,detection
from tracking_engine import TrackingEngine

def request(n,epoch=1,pose=False):
    return dict(sequence=n,capture_ns=1_000_000_000+n*20_000_000,epoch=[epoch]*8,
                maximum_persons=16,pose_requested=pose,detect_objects=True,hold_ms=80)
class Provider:
    def __init__(self):self.block=threading.Event();self.enter=threading.Event();self.pose_enter=threading.Event();self.pose_block=threading.Event()
    def analyze(self,image,cap,with_pose,detect_objects):
        self.enter.set();self.block.wait(2)
        self.stats=dict(candidates=1,returned=1,detector_ms=1.,pose_ms=0.)
        return [detection()]
    def refine_regions(self,image,regions):
        self.pose_enter.set();self.pose_block.wait(2)
        return regions,2.
class EngineTests(unittest.TestCase):
    def setUp(self):self.p=Provider();self.e=TrackingEngine(self.p)
    def tearDown(self):self.p.block.set();self.p.pose_block.set();self.e.close()
    def tick(self,n,epoch=1,pose=False):
        return self.e.step(shifted(texture(),n-1),request(n,epoch,pose),hashlib.sha256(str(n).encode()).hexdigest())
    def wait_detection(self,start=2,epoch=1,pose=False):
        self.p.block.set()
        for n in range(start,start+8):
            time.sleep(.01);reply=self.tick(n,epoch,pose)
            if reply['persons']:return n,reply
        self.fail('detector result never appeared')
    def test_blocked_detector_does_not_block_image_replies(self):
        self.tick(1);self.assertTrue(self.p.enter.wait(1))
        started=time.monotonic()
        for n in range(2,5):self.tick(n)
        self.assertLess(time.monotonic()-started,.3)
        _,r=self.wait_detection(5);self.assertEqual(len(r['persons']),1)
        self.assertEqual(r['persons'][0]['source']['sequence'],1)
    def test_tracking_continues_during_pose_stall(self):
        self.tick(1,pose=True);n,r=self.wait_detection(pose=True)
        self.assertTrue(self.p.pose_enter.wait(1));x=r['persons'][0]['box'][0]
        r=self.tick(n+1,pose=True);self.assertGreater(r['persons'][0]['box'][0],x)
    def test_epoch_reset_rejects_old_job(self):
        self.tick(1);self.assertTrue(self.p.enter.wait(1));self.tick(2,epoch=2)
        self.p.block.set();time.sleep(.03)
        r=self.tick(3,epoch=2);self.assertEqual(r['persons'],[])
    def test_engine_rejects_nonmonotonic_source(self):
        self.tick(1)
        with self.assertRaises(ValueError):self.tick(1)
    def test_configuration_reset_does_not_report_old_detector_counts(self):
        self.tick(1);n,_=self.wait_detection()
        self.e.metrics['detector_returned']=16;self.e.metrics['detector_candidates']=16
        req=request(n+1,epoch=2);req['maximum_persons']=1
        r=self.e.step(texture(),req,'c'*64)
        self.assertLessEqual(r['tracking']['detector_returned'],1)

if __name__=='__main__':unittest.main()
