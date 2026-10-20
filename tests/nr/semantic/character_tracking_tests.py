import sys, unittest, hashlib
from pathlib import Path
import numpy as np
import cv2 as cv
ROOT=Path(__file__).resolve().parents[3]
sys.path.insert(0,str(ROOT/'tools/nr_semantic_worker'))
from tracking import MotionTracker

def source(n,ms): return dict(sequence=n,capture_ns=1_000_000_000+int(ms*1e6),image_sha256=hashlib.sha256(str(n).encode()).hexdigest())
def detection(box=(.2,.2,.5,.8),cls=0,score=.9):
    return dict(box=list(box),class_id=cls,score=score,box_semantics='detector_box',landmarks=[])
def texture(seed=8):
    rng=np.random.default_rng(seed)
    return cv.GaussianBlur(rng.integers(0,256,(180,320,3),dtype=np.uint8),(3,3),0)
def shifted(im,x,y=0): return cv.warpAffine(im,np.float32([[1,0,x],[0,1,y]]),(320,180))

class TrackingTests(unittest.TestCase):
    def test_new_detection_replaces_missed_track_at_capacity(self):
        for hold_ms in (0,80):
            with self.subTest(hold_ms=hold_ms):
                t=MotionTracker(1,hold_ms);im=texture();s=source(1,0)
                t.advance(im,s);t.correct([detection((.05,.1,.25,.8))],s)
                old_id=t.snapshot()[0]['track_id']
                s=source(2,20);t.advance(im,s)
                assigned=t.correct([detection((.65,.1,.85,.8))],s)
                self.assertEqual(len(assigned),1)
                self.assertEqual(len(t.snapshot()),1)
                self.assertNotEqual(t.snapshot()[0]['track_id'],old_id)
                self.assertEqual(t.snapshot()[0]['source'],s)
                self.assertAlmostEqual(t.snapshot()[0]['box'][0],.65)

    def test_crowd_turnover_keeps_confirmed_ids_and_admits_newcomers(self):
        t=MotionTracker(8,80);im=texture();s=source(1,0);t.advance(im,s)
        original=[detection((.02+i*.12,.05,.10+i*.12,.40)) for i in range(8)]
        t.correct(original,s);old_ids=[p['track_id'] for p in t.snapshot()]
        s=source(2,20);t.advance(im,s)
        newcomers=[detection((.02+i*.24,.55,.20+i*.24,.95),score=.95) for i in range(4)]
        # Newcomers rank first; low-confidence existing people must still retain
        # their identity rather than being evicted before association finishes.
        remaining=[dict(p,score=.4) for p in original[:4]]
        assigned=t.correct(newcomers+remaining,s);people=t.snapshot()
        self.assertEqual(len(assigned),8)
        self.assertEqual(len(people),8)
        self.assertEqual({p['track_id'] for p in people}&set(old_ids),set(old_ids[:4]))
        self.assertTrue(all(p['source']==s for p in people))
        self.assertEqual(sum(p['box'][1]>.5 for p in people),4)

    def test_overlapping_newcomer_cannot_steal_better_match_pose(self):
        t=MotionTracker();im=texture();s=source(1,0);t.advance(im,s)
        original=detection((.3,.2,.6,.8));t.correct([original],s)
        pose=dict(original,landmarks=[dict(x=.4,y=.4,visibility=1.,presence=1.,z_relative=0.) for _ in range(33)])
        t.refine([pose],s);ident=t.snapshot()[0]['track_id']
        s=source(2,20);t.advance(im,s)
        t.correct([detection((.22,.2,.52,.8),score=.95),original],s)
        old=next(p for p in t.snapshot() if p['track_id']==ident)
        self.assertAlmostEqual(old['box'][0],.3,delta=.005)
        self.assertEqual(len(old['landmarks']),33)
    def test_cut_retaining_small_patch_invalidates_history(self):
        for seed_track in (False,True):
            t=MotionTracker();im=texture();s=source(1,0);t.advance(im,s)
            if seed_track:t.correct([detection()],s)
            cut=texture(81);cut[55:80,95:125]=im[55:80,95:125]
            t.advance(cut,source(2,20));self.assertEqual(t.snapshot(),[])
            t.correct([detection()],s);self.assertEqual(t.snapshot(),[])
    def test_first_delayed_result_is_transported_through_history(self):
        t=MotionTracker(16,80);im=texture();s=source(1,0);t.advance(im,s)
        for n in range(2,6):t.advance(shifted(im,5*(n-1)),source(n,(n-1)*20))
        t.correct([detection()],s)
        self.assertAlmostEqual(t.snapshot()[0]['box'][0],.2625,delta=.01)
    def test_foreground_moves_independently_of_camera(self):
        t=MotionTracker(16,80);background=texture(55);patch=texture(7)[40:140,80:160]
        a=background.copy();a[40:140,80:160]=patch
        b=background.copy();b[40:140,88:168]=patch
        s=source(1,0);t.advance(a,s);t.correct([detection((.27,.26,.48,.73))],s)
        t.advance(b,source(2,20));self.assertAlmostEqual(t.snapshot()[0]['box'][0],.295,delta=.01)
    def test_miss_breaks_consecutive_class_votes(self):
        t,im,s=self.seeded()
        for n,items in [(2,[detection(cls=23)]),(3,[detection(cls=23)]),(4,[]),(5,[detection(cls=23)])]:
            s=source(n,(n-1)*20);t.advance(im,s);t.correct(items,s)
        self.assertEqual(t.snapshot()[0]['class_id'],0)
    def seeded(self):
        t=MotionTracker(16,80);im=texture();s=source(1,0)
        t.advance(im,s);t.correct([detection()],s)
        self.assertEqual(len(t.snapshot()),1)
        return t,im,s
    def test_pan_and_delayed_detection_catch_up(self):
        t,im,s=self.seeded()
        for n in range(2,6): t.advance(shifted(im,(n-1)*5),source(n,(n-1)*20))
        t.correct([detection()],s)
        b=t.snapshot()[0]['box'];self.assertAlmostEqual(b[0],.2+20/320,delta=.012)
        self.assertEqual(t.snapshot()[0]['source'],s)
        self.assertLess(abs(b[0]-(.2+20/320)),.0625*.25)
    def test_reversal_does_not_keep_old_direction(self):
        t,im,s=self.seeded();t.advance(shifted(im,8),source(2,20));t.advance(im,source(3,40))
        p=t.snapshot()[0];self.assertAlmostEqual(p['box'][0],.2,delta=.012)
        self.assertLessEqual(p['velocity'][0],0)
    def test_zoom_tracks_extent(self):
        t,im,s=self.seeded();m=cv.getRotationMatrix2D((160,90),0,1.08)
        t.advance(cv.warpAffine(im,m,(320,180)),source(2,20));b=t.snapshot()[0]['box']
        self.assertAlmostEqual(b[2]-b[0],.3*1.08,delta=.02)
    def test_blank_occlusion_and_cut_remove_boxes(self):
        for next_image in (np.zeros((180,320,3),np.uint8),texture(81)):
            t,im,s=self.seeded();t.advance(next_image,source(2,20));self.assertEqual(t.snapshot(),[])
            t.correct([detection()],s);self.assertEqual(t.snapshot(),[])
    def test_confirmation_expires_despite_good_flow(self):
        t,im,s=self.seeded()
        for n in range(2,20):t.advance(im,source(n,(n-1)*20))
        self.assertEqual(t.snapshot(),[])
    def test_misses_are_bounded_and_do_not_refresh(self):
        t,im,s=self.seeded()
        for n in range(2,9):
            s=source(n,(n-1)*20);t.advance(im,s);t.correct([],s)
        self.assertEqual(t.snapshot(),[])
    def test_class_flip_requires_three_consistent_observations(self):
        t,im,s=self.seeded();ident=t.snapshot()[0]['track_id']
        for n in (2,3):
            s=source(n,(n-1)*20);t.advance(im,s);t.correct([detection(cls=23)],s)
            self.assertEqual(t.snapshot()[0]['class_id'],0)
        s=source(4,60);t.advance(im,s);t.correct([detection(cls=23)],s)
        self.assertEqual(t.snapshot()[0]['class_id'],23);self.assertEqual(t.snapshot()[0]['track_id'],ident)
    def test_low_confidence_cannot_create_but_can_maintain(self):
        t=MotionTracker(16,80);im=texture();s=source(1,0);t.advance(im,s)
        t.correct([detection(score=.4)],s);self.assertEqual(t.snapshot(),[])
        s=source(2,20);t.advance(im,s);t.correct([detection()],s)
        s=source(3,40);t.advance(im,s);t.correct([detection(score=.4)],s)
        self.assertEqual(len(t.snapshot()),1)
    def test_sixteen_independent_tracks(self):
        t=MotionTracker(16,80);im=texture();s=source(1,0);t.advance(im,s)
        ds=[detection((.02+(i%4)*.24,.02+(i//4)*.24,.22+(i%4)*.24,.22+(i//4)*.24)) for i in range(16)]
        t.correct(ds,s);self.assertEqual(len(t.snapshot()),16)
        t.advance(shifted(im,3),source(2,20));self.assertEqual(len(t.snapshot()),16)
        self.assertEqual(len({p['track_id'] for p in t.snapshot()}),16)
    def test_out_of_order_and_missing_history(self):
        t,im,s=self.seeded()
        with self.assertRaises(ValueError):t.advance(im,s)
        t.correct([detection()],source(9,-100));self.assertEqual(t.snapshot()[0]['source'],s)
    def test_pose_is_projected_and_class_change_clears_it(self):
        t,im,s=self.seeded();pose=detection();pose['landmarks']=[dict(x=.3,y=.4,visibility=1.,presence=1.,z_relative=0.) for _ in range(33)]
        t.advance(shifted(im,6),source(2,20));t.refine([pose],s)
        self.assertAlmostEqual(t.snapshot()[0]['landmarks'][0]['x'],.3+6/320,delta=.01)
        for n in range(3,6):
            sn=source(n,(n-1)*20);t.advance(shifted(im,6),sn);t.correct([detection((.2+6/320,.2,.5+6/320,.8),23)],sn)
        self.assertEqual(t.snapshot()[0]['landmarks'],[])

if __name__=='__main__':unittest.main()
