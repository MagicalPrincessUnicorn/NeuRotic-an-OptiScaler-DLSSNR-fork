"""Bounded image-backed tracking. No game coordinates or model calls.

All state is owned by the worker's image thread. Detector and pose jobs return
immutable observations which are transported from their recorded source image.
"""
from collections import deque
from copy import deepcopy
import numpy as np
import cv2 as cv

CONFIRM_NS=350_000_000
POSE_NS=250_000_000

def iou(a,b):
    w=max(0.,min(a[2],b[2])-max(a[0],b[0]));h=max(0.,min(a[3],b[3])-max(a[1],b[1]))
    return w*h/max(1e-12,(a[2]-a[0])*(a[3]-a[1])+(b[2]-b[0])*(b[3]-b[1])-w*h)

def transform(region,matrix):
    out=deepcopy(region);x0,y0,x1,y1=out['box']
    points=np.array([[x0,y0,1],[x1,y0,1],[x1,y1,1],[x0,y1,1]])@matrix.T
    low=points.min(axis=0);high=points.max(axis=0)
    box=np.clip(np.r_[low,high],0,1)
    if np.any(high-low<=0) or np.any(box[2:]-box[:2]<.002):return None
    # A box mostly outside the image is not a usable observation.
    if np.prod(box[2:]-box[:2])<.35*np.prod(high-low):return None
    out['box']=box.tolist()
    for p in out.get('landmarks',[]):
        p['x'],p['y']=(matrix@np.array([p['x'],p['y'],1.])).tolist()
    return out

class Flow:
    def __init__(self,previous,current):
        self.previous=previous;self.current=current;self.scale=np.array([current.shape[1],current.shape[0]],float)
        self.before=np.empty((0,2));self.after=self.before;self.global_matrix=None;self.cut=True
        points=cv.goodFeaturesToTrack(previous,maxCorners=600,qualityLevel=.015,minDistance=5,blockSize=5)
        if points is None:return
        after,valid,error=cv.calcOpticalFlowPyrLK(previous,current,points,None,winSize=(15,15),maxLevel=2)
        back,reverse,_=cv.calcOpticalFlowPyrLK(current,previous,after,points.copy(),winSize=(15,15),maxLevel=2,flags=cv.OPTFLOW_USE_INITIAL_FLOW)
        good=(valid[:,0]!=0)&(reverse[:,0]!=0)&(error[:,0]<12)&(np.linalg.norm(back-points,axis=2)[:,0]<1.2)
        good&=np.isfinite(after).all(axis=(1,2))
        self.original=points[:,0]/self.scale
        # Consensus among a tiny surviving patch is not scene continuity.
        # Treat widespread support loss as a transport barrier, also for late AI.
        self.cut=float(good.mean())<.2
        self.before=points[good,0]/self.scale;self.after=after[good,0]/self.scale
        self.global_matrix=self.fit(self.before,self.after)[0]

    def fit(self,before,after):
        if len(before)<4:return None,0.
        # Work in isotropic pixel coordinates when estimating rotation/scale.
        matrix,inliers=cv.estimateAffinePartial2D(before*self.scale,after*self.scale,method=cv.RANSAC,
            ransacReprojThreshold=1.5,maxIters=100,confidence=.97,refineIters=5)
        if matrix is None or not np.isfinite(matrix).all():return None,0.
        quality=float(inliers.mean());scale=float(np.hypot(matrix[0,0],matrix[1,0]))
        if quality<.65 or not .8<=scale<=1.25:return None,0.
        matrix[:,:2]*=self.scale[None,:]/self.scale[:,None];matrix[:,2]/=self.scale
        return matrix,quality

    def region(self,box):
        if self.cut:return None,0.
        x0,y0,x1,y1=box
        selected=(self.before[:,0]>=x0)&(self.before[:,0]<=x1)&(self.before[:,1]>=y0)&(self.before[:,1]<=y1)
        before=self.before[selected];after=self.after[selected]
        original=(self.original[:,0]>=x0)&(self.original[:,0]<=x1)&(self.original[:,1]>=y0)&(self.original[:,1]<=y1)
        if len(before)>=6 and len(before)>=.5*int(original.sum()):
            return self.fit(before,after)
        # Background motion supplies an initial guess, never permission to move
        # an unobserved/occluded subject. Require validated features inside it.
        mask=np.zeros_like(self.previous)
        lo=np.floor(np.array([x0,y0])*self.scale).astype(int);hi=np.ceil(np.array([x1,y1])*self.scale).astype(int)
        mask[max(0,lo[1]):hi[1],max(0,lo[0]):hi[0]]=255
        points=cv.goodFeaturesToTrack(self.previous,32,.01,3,mask=mask,blockSize=3)
        if points is None or len(points)<4:return None,0.
        guess=None;flags=0
        if self.global_matrix is not None:
            p=points[:,0]/self.scale
            guess=((np.c_[p,np.ones(len(p))]@self.global_matrix.T)*self.scale).astype(np.float32).reshape(-1,1,2).copy()
            flags=cv.OPTFLOW_USE_INITIAL_FLOW
        moved,valid,error=cv.calcOpticalFlowPyrLK(self.previous,self.current,points,guess,
            winSize=(15,15),maxLevel=2,flags=flags)
        back,reverse,_=cv.calcOpticalFlowPyrLK(self.current,self.previous,moved,points.copy(),winSize=(15,15),maxLevel=2,flags=cv.OPTFLOW_USE_INITIAL_FLOW)
        good=(valid[:,0]!=0)&(reverse[:,0]!=0)&(error[:,0]<12)&(np.linalg.norm(back-points,axis=2)[:,0]<1.2)
        if good.mean()<.5:return None,0.
        return self.fit(points[good,0]/self.scale,moved[good,0]/self.scale)

class MotionTracker:
    def __init__(self,maximum=16,hold_ms=80):
        self.maximum=max(1,min(16,maximum));self.hold_ns=max(0,min(100,hold_ms))*1_000_000
        self.history=deque(maxlen=24);self.tracks=[];self.next_id=1;self.last_correction=0

    def advance(self,image,source):
        if self.history and (source['sequence']<=self.history[-1][0]['sequence'] or source['capture_ns']<=self.history[-1][0]['capture_ns']):
            raise ValueError('nonmonotonic tracking image')
        h,w=image.shape[:2];size=(320,max(32,round(h*320/w))) if w>=h else (max(32,round(w*320/h)),320)
        gray=cv.resize(cv.cvtColor(image,cv.COLOR_BGR2GRAY),size,interpolation=cv.INTER_AREA)
        flow=None;dt=0
        if self.history:
            previous=self.history[-1];dt=(source['capture_ns']-previous[0]['capture_ns'])/1e9
            if gray.shape==previous[1].shape and dt<=.1:flow=Flow(previous[1],gray)
            if flow is None or flow.cut:
                self.history.clear();flow=None
        advanced=[]
        for track in self.tracks:
            if flow is None:continue
            matrix,quality=flow.region(track['box'])
            if matrix is None:continue
            moved=transform(track,matrix)
            if moved is None:continue
            velocity=(np.array(moved['box'][:2])+moved['box'][2:]-np.array(track['box'][:2])-track['box'][2:])/(2*dt)
            velocity=np.clip(velocity,-2,2)
            # A reversal invalidates prior prediction immediately.
            old=np.array(track['velocity']);velocity=np.where(old*velocity<0,0,velocity)
            moved['velocity']=velocity.tolist();moved['quality']=quality;advanced.append(moved)
        self.tracks=advanced;self.history.append((dict(source),gray,flow));self._expire()

    def _expire(self):
        now=self.history[-1][0]['capture_ns']
        self.tracks=[t for t in self.tracks if now-t['source']['capture_ns']<=CONFIRM_NS and
            (t['_miss_ns'] is None or now-t['_miss_ns']<self.hold_ns)]
        for t in self.tracks:
            if t['pose_source'] and now-t['pose_source']['capture_ns']>POSE_NS:
                t['landmarks']=[];t['pose_source']=None

    def _project(self,region,source):
        entries=list(self.history);index=next((i for i,e in enumerate(entries) if e[0]==source),None)
        if index is None or entries[-1][0]['capture_ns']-source['capture_ns']>CONFIRM_NS:return None
        out=deepcopy(region);quality=1.
        for _,_,flow in entries[index+1:]:
            if flow is None:return None
            matrix,q=flow.region(out['box'])
            if matrix is None:return None
            out=transform(out,matrix)
            if out is None:return None
            quality=min(quality,q)
        return out,quality

    def correct(self,detections,source):
        if not self.history or source['sequence']<=self.last_correction:return
        if not any(e[0]==source for e in self.history):return
        if self.history[-1][0]['capture_ns']-source['capture_ns']>CONFIRM_NS:return
        self.last_correction=source['sequence'];matched=set();assignments=[];newcomers=[]
        projected=[(self._project(d,source),d) for d in detections]
        projected=[(p[0],p[1],d) for p,d in projected if p is not None]
        # Freeze both sides before assigning: confidence ordering cannot steal
        # an old identity from another detection with a better geometric match.
        overlaps=[[iou(t['box'],p[0]['box']) for t in self.tracks] for p in projected]
        for detection_index in sorted(range(len(projected)),key=lambda i:-projected[i][0]['score']):
            region,quality,original=projected[detection_index]
            candidates=sorted(((score,i) for i,score in enumerate(overlaps[detection_index])),reverse=True)
            match=candidates[0] if candidates and candidates[0][0]>.35 else None
            if match and len(candidates)>1 and candidates[1][0]>.35 and match[0]-candidates[1][0]<.1:
                continue # Ambiguity does not authorize label/pose transfer.
            if match:
                competitors=sorted(((row[match[1]],i) for i,row in enumerate(overlaps)),reverse=True)
                if len(competitors)>1 and competitors[1][0]>.35 and competitors[0][0]-competitors[1][0]<.1:
                    continue
                if competitors[0][1]!=detection_index or match[1] in matched:match=None
            if match:
                index=match[1];t=self.tracks[index];matched.add(index)
                changed=region['class_id']!=t['class_id']
                if changed and region['score']>=.55:
                    t['_votes']=t['_votes']+1 if t['_candidate']==region['class_id'] else 1
                    t['_candidate']=region['class_id']
                    if t['_votes']>=3:
                        t['class_id']=region['class_id'];t['landmarks']=[];t['pose_source']=None;t['_votes']=0
                else:t['_votes']=0;t['_candidate']=None
                # Geometric correction is immediate at speed. Only subpixel-ish
                # detector wobble gets smoothing after motion transport.
                a=np.array(t['box']);b=np.array(region['box'])
                if np.max(np.abs(a-b))<.015:b=.7*b+.3*a
                t['box']=b.tolist();t['box_semantics']=region['box_semantics'];t['quality']=quality
                if t['class_id']==region['class_id']:t['score']=region['score']
                t['source']=dict(source);t['_miss_ns']=None
                assignments.append(dict(deepcopy(original),track_id=t['track_id']))
            elif region['score']>=.5:
                newcomers.append((region,quality,original))
        for i,t in enumerate(self.tracks):
            if i not in matched:
                t['_votes']=0;t['_candidate']=None
                if t['_miss_ns'] is None:t['_miss_ns']=self.history[-1][0]['capture_ns']
        self._expire()
        # Finish association before admitting new detections: missed tracks may
        # be held briefly, but cannot reserve capacity against current evidence.
        # Confirmed tracks retain identity even below the new-track threshold.
        newcomers=newcomers[:self.maximum-len(matched)]
        if newcomers:
            confirmed=[t for t in self.tracks if t['_miss_ns'] is None]
            held=sorted((t for t in self.tracks if t['_miss_ns'] is not None),
                key=lambda t:(t['source']['capture_ns'],t['score']),reverse=True)
            self.tracks=confirmed+held[:self.maximum-len(confirmed)-len(newcomers)]
            for region,quality,original in newcomers:
                track=deepcopy(region);track.update(track_id=self.next_id,source=dict(source),pose_source=None,
                    quality=quality,velocity=[0.,0.],_miss_ns=None,_votes=0,_candidate=None)
                self.next_id+=1;self.tracks.append(track)
                assignments.append(dict(deepcopy(original),track_id=track['track_id']))
        return assignments

    def refine(self,poses,source):
        if not self.history or self.history[-1][0]['capture_ns']-source['capture_ns']>POSE_NS:return
        for pose in poses:
            if pose['class_id']!=0 or len(pose['landmarks'])!=33:continue
            projected=self._project(pose,source)
            if projected is None:continue
            region,_=projected
            candidates=sorted(((iou(t['box'],region['box']),i) for i,t in enumerate(self.tracks) if t['class_id']==0 and
                ('track_id' not in pose or t['track_id']==pose['track_id'])),reverse=True)
            if not candidates or candidates[0][0]<.6:continue
            if len(candidates)>1 and candidates[0][0]-candidates[1][0]<.15:continue
            t=self.tracks[candidates[0][1]]
            if t['pose_source'] and t['pose_source']['sequence']>=source['sequence']:continue
            t['landmarks']=region['landmarks'];t['pose_source']=dict(source)

    def snapshot(self):
        return [{k:deepcopy(v) for k,v in t.items() if not k.startswith('_')} for t in self.tracks]
