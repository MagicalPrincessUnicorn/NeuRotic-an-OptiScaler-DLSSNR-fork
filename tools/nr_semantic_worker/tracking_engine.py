"""Image-thread owner and bounded model jobs."""
from concurrent.futures import ThreadPoolExecutor
from copy import deepcopy
import time
from tracking import MotionTracker

class TrackingEngine:
    def __init__(self,provider):
        self.provider=provider;self.detector=ThreadPoolExecutor(1);self.pose=ThreadPoolExecutor(1)
        self.detect_job=None;self.pose_job=None;self.context=None;self.tracker=None;self.last_pose_ns=0
        self.metrics=dict(detector_candidates=0,detector_returned=0,detector_ms=0.,pose_ms=0.,tracking_ms=0.,
            detection_updates=0,pose_updates=0,pose_failures=0)

    def _detect(self,image,request):
        regions=self.provider.analyze(image,request['maximum_persons'],False,request['detect_objects'])
        return regions,dict(self.provider.stats)

    def step(self,image,request,image_hash):
        started=time.perf_counter()
        context=(tuple(request['epoch']),request['maximum_persons'],request['hold_ms'],request['detect_objects'],request['pose_requested'])
        if context!=self.context:
            self.context=context;self.tracker=MotionTracker(request['maximum_persons'],request['hold_ms']);self.last_pose_ns=0
            self.metrics.update(detector_candidates=0,detector_returned=0,detector_ms=0.,pose_ms=0.,tracking_ms=0.)
        source=dict(sequence=request['sequence'],capture_ns=request['capture_ns'],image_sha256=image_hash)
        self.tracker.advance(image,source)
        if self.detect_job and self.detect_job[0].done():
            future,origin,old_image,old_context=self.detect_job;self.detect_job=None
            regions,stats=future.result() # A detector fault is reported to the host.
            if old_context==context:
                assignments=self.tracker.correct(regions,origin) or []
                self.metrics.update(detector_candidates=stats['candidates'],detector_returned=stats['returned'],detector_ms=stats['detector_ms'])
                self.metrics['detection_updates']+=1
                if request['pose_requested'] and assignments and self.pose_job is None and source['capture_ns']-self.last_pose_ns>=150_000_000:
                    self.pose_job=(self.pose.submit(self.provider.refine_regions,old_image,deepcopy(assignments)),origin,context)
                    self.last_pose_ns=source['capture_ns']
        if self.pose_job and self.pose_job[0].done():
            future,origin,old_context=self.pose_job;self.pose_job=None
            try:regions,elapsed=future.result()
            except Exception:
                self.metrics['pose_failures']+=1
            else:
                if old_context==context:
                    self.tracker.refine(regions,origin);self.metrics['pose_ms']=elapsed;self.metrics['pose_updates']+=1
        # Never enqueue behind an active model call. The next job starts on the
        # newest image, while this image's tracked geometry is returned at once.
        if self.detect_job is None:
            self.detect_job=(self.detector.submit(self._detect,image,dict(request)),source,image,context)
        self.metrics['tracking_ms']=(time.perf_counter()-started)*1000
        return dict(persons=self.tracker.snapshot(),tracking=dict(self.metrics))

    def close(self):
        self.detector.shutdown(wait=False,cancel_futures=True);self.pose.shutdown(wait=False,cancel_futures=True)
