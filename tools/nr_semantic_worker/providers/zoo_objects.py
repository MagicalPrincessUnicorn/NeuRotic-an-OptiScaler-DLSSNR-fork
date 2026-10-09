# SPDX-License-Identifier: Apache-2.0
"""Pinned NanoDet adapter. CPU only; exact letterbox mapping and class-aware NMS."""
import numpy as np
import cv2 as cv
from providers.zoo_character import _load_class

class ObjectDetector:
    def __init__(self, path):
        Base=_load_class('models/object_detection_nanodet/nanodet.py','NanoDet')
        class ClassAware(Base):
            def post_process(self, outputs):
                if len(outputs)!=6: raise ValueError('unexpected object detector outputs')
                boxes=[];scores=[]
                for stride,cls,reg,anchors in zip(self.strides,outputs[::2],outputs[1::2],self.anchors_mlvl):
                    cls=np.asarray(cls).reshape(-1,80);reg=np.asarray(reg).reshape(-1,4,8)
                    if len(cls)!=len(anchors) or len(reg)!=len(anchors) or not np.isfinite(cls).all() or not np.isfinite(reg).all():
                        raise ValueError('invalid object tensors')
                    exp=np.exp(reg-reg.max(axis=2,keepdims=True));distance=(exp/exp.sum(axis=2,keepdims=True))@self.project*stride
                    if len(cls)>1000:
                        indices=np.argsort(-cls.max(axis=1),kind='stable')[:1000]
                        cls=cls[indices];distance=distance[indices];anchors=anchors[indices]
                    boxes.append(np.clip(np.c_[anchors-distance[:,:2],anchors+distance[:,2:]],0,416));scores.append(cls)
                boxes=np.concatenate(boxes);scores=np.concatenate(scores)
                classes=scores.argmax(axis=1);confidence=scores.max(axis=1)
                if np.any((confidence<0)|(confidence>1)): raise ValueError('invalid object scores')
                xywh=boxes.copy();xywh[:,2:]-=xywh[:,:2]
                valid=np.flatnonzero((xywh[:,2]>0)&(xywh[:,3]>0))
                keep=np.asarray(cv.dnn.NMSBoxesBatched(xywh[valid].tolist(),confidence[valid].tolist(),
                    classes[valid].tolist(),self.prob_threshold,self.iou_threshold),dtype=np.int64).reshape(-1)
                keep=valid[keep]
                return np.c_[boxes[keep],confidence[keep],classes[keep]]
        self.detector=ClassAware(str(path),backend_id=cv.dnn.DNN_BACKEND_OPENCV,target_id=cv.dnn.DNN_TARGET_CPU)

    def analyze(self, image, cap):
        height,width=image.shape[:2];scale=min(416/width,416/height)
        rw=max(1,min(416,round(width*scale)));rh=max(1,min(416,round(height*scale)))
        left=(416-rw)//2;top=(416-rh)//2
        resized=cv.resize(image,(rw,rh),interpolation=cv.INTER_AREA)
        canvas=cv.copyMakeBorder(resized,top,416-rh-top,left,416-rw-left,cv.BORDER_CONSTANT,value=0)
        predictions=self.detector.infer(canvas);result=[]
        if len(predictions): predictions=predictions[np.argsort(-predictions[:,4],kind='stable')]
        for row in predictions:
            if len(result)==cap: break
            if row.shape!=(6,) or not np.isfinite(row).all() or not 0<=row[4]<=1 or row[5]!=int(row[5]) or not 0<=row[5]<80:
                raise ValueError('invalid object prediction')
            x0,y0,x1,y1=(row[:4]-[left,top,left,top])/[rw,rh,rw,rh]
            box=[max(0.,x0),max(0.,y0),min(1.,x1),min(1.,y1)]
            if box[0]>=box[2] or box[1]>=box[3]: continue
            result.append(dict(box=box,score=float(row[4]),class_id=int(row[5]),
                box_semantics='detector_box',landmarks=[],facing='unknown',label='Object'))
        return result,len(predictions)
