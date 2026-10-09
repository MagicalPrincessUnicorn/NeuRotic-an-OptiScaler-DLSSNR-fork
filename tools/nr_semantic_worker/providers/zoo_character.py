# SPDX-License-Identifier: Apache-2.0
# Adapted from OpenCV Zoo (Apache-2.0), commit 47534e27c9851bb1128ccc0102f1145e27f23f98.
# Changes: NMS corner conversion, generated anchor grid, bounded ROI, immutable caller
# detection, constructor-only backend selection, explicit hash-checked local model loading.
# Upstream source and licensing are retained under third_party/opencv_zoo.
"""Offline CPU reference. No model loads/downloads occur on import."""
from pathlib import Path
import hashlib
import importlib.util
import json
import time
import numpy as np
import cv2 as cv
from inspector_contract import canonical_pose

ROOT = Path(__file__).resolve().parents[1]

def anchor_grid() -> np.ndarray:
    return np.asarray([[(x+.5)/n,(y+.5)/n] for n,repeats in ((28,2),(14,2),(7,6))
                       for y in range(n) for x in range(n) for _ in range(repeats)],dtype=np.float32)

def nms_corners(boxes: np.ndarray, scores: np.ndarray, score_threshold: float, nms_threshold: float) -> np.ndarray:
    boxes=np.asarray(boxes,dtype=np.float64);scores=np.asarray(scores,dtype=np.float64).reshape(-1)
    if boxes.ndim!=2 or boxes.shape[1]!=4 or len(boxes)!=len(scores) or len(boxes)>5000:
        raise ValueError('invalid detection shape')
    if not np.isfinite(boxes).all() or not np.isfinite(scores).all() or np.any(boxes[:,2:]<=boxes[:,:2]):
        raise ValueError('non-finite or reversed detection')
    if np.any((scores<0)|(scores>1)) or not 0<=score_threshold<=1 or not 0<=nms_threshold<=1:
        raise ValueError('invalid scores or threshold')
    if not len(boxes): return np.empty(0,dtype=np.int64)
    xywh=boxes.copy();xywh[:,2:]-=xywh[:,:2]
    return np.asarray(cv.dnn.NMSBoxes(xywh.tolist(),scores.tolist(),score_threshold,nms_threshold,top_k=5000),dtype=np.int64).reshape(-1)

def prepare_pose(pose, image: np.ndarray, person: np.ndarray):
    p=np.asarray(person,dtype=np.float64).reshape(-1).copy()
    if p.shape!=(13,) or not np.isfinite(p).all() or image.ndim!=3 or image.shape[2]!=3:
        raise ValueError('invalid pose request')
    h,w=image.shape[:2]
    if not 0<h<=2048 or not 0<w<=2048:
        raise ValueError('reference input must be at most 2048 per side')
    radius=float(np.linalg.norm(p[4:6]-p[6:8]))
    hip=p[4:6]
    if radius<2 or radius>min(max(w,h),1024) or np.any(hip+radius<=0) or hip[0]-radius>=w or hip[1]-radius>=h:
        raise ValueError('degenerate, excessive, or off-frame pose ROI')
    return pose._preprocess(image,p)

def detector_region(person, width, height):
    """Virtual full-body circle from the detector, explicitly estimated."""
    p=np.asarray(person,dtype=np.float64).reshape(-1)
    if p.shape!=(13,) or not np.isfinite(p).all() or not 0<=p[-1]<=1:
        raise ValueError('invalid person detection')
    radius=float(np.linalg.norm(p[4:6]-p[6:8]))
    if not 2<=radius<=min(max(width,height),1024): return None
    hip=p[4:6]
    box=[max(0.,(hip[0]-radius)/width),max(0.,(hip[1]-radius)/height),
         min(1.,(hip[0]+radius)/width),min(1.,(hip[1]+radius)/height)]
    if box[0]>=box[2] or box[1]>=box[3]: return None
    return dict(box=box,box_semantics='estimated_person_region',score=float(p[-1]),
                landmarks=[],facing='unknown',label='Person',class_id=0)

def decode_landmarks(pose, outputs, roi, angle, rotation, pad):
    """Pinned Zoo landmark transform, without unused world/mask postprocessing."""
    if len(outputs)!=5: raise ValueError('unexpected pose output count')
    points=np.asarray(outputs[0],dtype=np.float64)
    conf=np.asarray(outputs[1],dtype=np.float64)
    if points.shape!=(1,195) or conf.shape!=(1,1) or not np.isfinite(points).all() or not np.isfinite(conf).all():
        raise ValueError('unexpected landmark tensors')
    score=float(conf[0,0])
    if not 0<=score<=1: raise ValueError('invalid pose score')
    if score<pose.conf_threshold: return None
    points=points[0].reshape(39,5).copy()
    points[:,3:]=1/(1+np.exp(-np.clip(points[:,3:],-100,100)))
    scale=(roi[1]-roi[0])/np.asarray(pose.input_size)
    points[:,:2]=(points[:,:2]-np.asarray(pose.input_size)/2)*scale
    points[:,2]*=max(scale)
    rotated=points[:,:2]@cv.getRotationMatrix2D((0,0),angle,1.)[:,:2]
    inverse=cv.invertAffineTransform(rotation)
    center=np.append((roi[0]+roi[1])/2,1.)@inverse.T
    points[:,:2]=rotated+center+pad
    return points,score

def _load_class(relative: str, name: str):
    path=ROOT/'third_party/opencv_zoo'/relative
    specs=json.loads((ROOT/'upstream.lock.json').read_text(encoding='utf-8'))['sources']
    spec=next(s for s in specs if s['destination']==path.relative_to(ROOT).as_posix())
    data=path.read_bytes()
    blob=hashlib.sha1(b'blob '+str(len(data)).encode()+b'\0'+data).hexdigest()
    if blob!=spec['git_blob_sha1']: raise ValueError('upstream source differs from pinned blob')
    module_spec=importlib.util.spec_from_file_location('_neurotic_'+name,path)
    module=importlib.util.module_from_spec(module_spec);module_spec.loader.exec_module(module)
    return getattr(module,name)

class ZooCpuProvider:
    """Explicit construction only. Reference uses one CPU OpenCV session per model."""
    def __init__(self, detector_path: Path, pose_path: Path, object_path: Path=None):
        specs=json.loads((ROOT/'models.lock.json').read_text(encoding='utf-8'))['models']
        paths=(Path(detector_path),Path(pose_path))+( (Path(object_path),) if object_path is not None else () )
        for path,spec in zip(paths,specs):
            if path.stat().st_size!=spec['bytes'] or hashlib.sha256(path.read_bytes()).hexdigest()!=spec['sha256']:
                raise ValueError('model is not the pinned, licensed artifact: '+str(path))
        Person=_load_class('models/person_detection_mediapipe/mp_persondet.py','MPPersonDet')
        PoseClass=_load_class('models/pose_estimation_mediapipe/mp_pose.py','MPPose')
        class CorrectedPerson(Person):
            def _load_anchors(self): return anchor_grid()
            def _postprocess(self, output_blob, original_shape, pad_bias):
                if len(output_blob)!=2: raise ValueError('wrong detector outputs')
                scores=output_blob[1][0,:,0].astype(np.float64)
                delta=output_blob[0][0]
                if scores.shape!=(2254,) or delta.shape!=(2254,12) or not np.isfinite(delta).all() or not np.isfinite(scores).all():
                    raise ValueError('unexpected detector tensor contract')
                scores=1/(1+np.exp(-np.clip(scores,-100,100)))
                scale=max(original_shape)
                center=delta[:,:2]/self.input_size+self.anchors
                size=delta[:,2:4]/self.input_size
                boxes=np.concatenate(((center-size/2)*scale,(center+size/2)*scale),axis=1)
                boxes-=np.tile(pad_bias,2)
                valid=(scores>=self.score_threshold)&np.all(boxes[:,2:]>boxes[:,:2],axis=1)
                candidates=np.flatnonzero(valid)
                if not len(candidates): return np.empty((0,13))
                keep=candidates[nms_corners(boxes[candidates],scores[candidates],self.score_threshold,self.nms_threshold)]
                keypoints=delta[keep,4:].reshape(-1,4,2)/self.input_size+self.anchors[keep,None,:]
                keypoints=keypoints*scale-pad_bias
                return np.c_[boxes[keep],keypoints.reshape(-1,8),scores[keep]]
        self.detector=CorrectedPerson(str(detector_path),backendId=cv.dnn.DNN_BACKEND_OPENCV,targetId=cv.dnn.DNN_TARGET_CPU)
        self.pose=PoseClass(str(pose_path),backendId=cv.dnn.DNN_BACKEND_OPENCV,targetId=cv.dnn.DNN_TARGET_CPU)
        self.objects=None
        if object_path is not None:
            from providers.zoo_objects import ObjectDetector
            self.objects=ObjectDetector(object_path)
        # Do not use upstream's broken setter; constructor sets the right fields.
        cv.setNumThreads(1)

    def _refine(self,image,region,person):
        started=time.perf_counter()
        try:
            blob,roi,angle,rotation,pad=prepare_pose(self.pose,image,person)
        except ValueError: return region
        self.stats['pose_attempts']+=1
        self.pose.model.setInput(blob)
        outputs=self.pose.model.forward(self.pose.model.getUnconnectedOutLayersNames())
        raw=decode_landmarks(self.pose,outputs,roi,angle,rotation,pad)
        self.stats['pose_ms']+=(time.perf_counter()-started)*1000
        if raw is None: return region
        h,w=image.shape[:2];landmarks=canonical_pose(raw[0],w,h)
        visible=[k for k in landmarks if k['visibility']>=.5 and k['presence']>=.5 and 0<=k['x']<=1 and 0<=k['y']<=1]
        if len(visible)<4: return region
        region['landmarks']=landmarks;self.stats['usable_poses']+=1
        return region

    def refine_regions(self,image,regions):
        """Pose-thread-only model; separate counters from the detector thread."""
        worker=ZooCpuProvider.__new__(ZooCpuProvider);worker.pose=self.pose
        worker.stats=dict(pose_attempts=0,usable_poses=0,pose_ms=0.)
        h,w=image.shape[:2]
        for region in regions:
            if region['class_id']!=0:continue
            x0,y0,x1,y1=region['box'];cx=(x0+x1)*w/2;cy=(y0+y1)*h/2
            radius=max((x1-x0)*w,(y1-y0)*h)/2
            person=np.array([x0*w,y0*h,x1*w,y1*h,cx,cy,cx,cy-radius,0,0,0,0,region['score']])
            worker._refine(image,region,person)
        return regions,worker.stats['pose_ms']

    def analyze(self, image: np.ndarray, max_persons: int=4, with_pose: bool=False, detect_objects: bool=False) -> list[dict]:
        if type(max_persons) is not int or not 1<=max_persons<=16:
            raise ValueError('max_persons must be 1..16')
        if image.dtype!=np.uint8 or image.ndim!=3 or image.shape[2]!=3 or not 0<max(image.shape[:2])<=2048:
            raise ValueError('expected bounded SDR uint8 BGR image')
        if type(with_pose) is not bool or type(detect_objects) is not bool: raise ValueError('invalid detection mode')
        started=time.perf_counter();h,w=image.shape[:2]
        self.stats=dict(candidates=0,pose_attempts=0,usable_poses=0,returned=0,detector_ms=0.,pose_ms=0.)
        if detect_objects:
            if self.objects is None: raise ValueError('object model not installed')
            result,candidates=self.objects.analyze(image,max_persons)
            self.stats.update(candidates=candidates,detector_ms=(time.perf_counter()-started)*1000)
            if with_pose:
                for region in result:
                    if region['class_id']!=0: continue
                    x0,y0,x1,y1=region['box'];cx=(x0+x1)*w/2;cy=(y0+y1)*h/2;radius=max((x1-x0)*w,(y1-y0)*h)/2
                    # Estimated upright crop for the pose model, not invented joints.
                    person=np.array([x0*w,y0*h,x1*w,y1*h,cx,cy,cx,cy-radius,0,0,0,0,region['score']])
                    self._refine(image,region,person)
        else:
            persons=self.detector.infer(image)
            self.stats.update(candidates=len(persons),detector_ms=(time.perf_counter()-started)*1000)
            if len(persons): persons=persons[np.argsort(-persons[:,-1],kind='stable')]
            result=[]
            for person in persons:
                if len(result)==max_persons: break
                region=detector_region(person,w,h)
                if region is None: continue
                if with_pose: self._refine(image,region,person)
                result.append(region)
        # Pose detail never changes a detector box or removes a detected region.
        self.stats['returned']=len(result)
        return result
