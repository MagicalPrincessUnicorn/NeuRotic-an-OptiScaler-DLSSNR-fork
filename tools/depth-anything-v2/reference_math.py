"""CPU numeric oracle, not the real-time production path. Original code; MIT."""
from dataclasses import dataclass
import math
import numpy as np

MEAN=np.asarray([.485,.456,.406],dtype=np.float32)
STD=np.asarray([.229,.224,.225],dtype=np.float32)
@dataclass(frozen=True)
class Geometry:
    source_w:int; source_h:int; tensor_w:int; tensor_h:int
    content_w:int; content_h:int; left:int; top:int

def letterbox(sw:int,sh:int,tw:int,th:int)->Geometry:
    if any(type(v) is not int or v<=0 for v in (sw,sh,tw,th)):
        raise ValueError('positive integer dimensions required')
    if max(sw,sh)>16384 or min(tw,th)<14 or max(tw,th)>2048 or tw%14 or th%14:
        raise ValueError('source limit 16384; tensor dimensions 14..2048 and divisible by 14')
    scale=min(tw/sw,th/sh)
    cw=min(tw,max(1,math.floor(sw*scale+.5)));ch=min(th,max(1,math.floor(sh*scale+.5)))
    return Geometry(sw,sh,tw,th,cw,ch,(tw-cw)//2,(th-ch)//2)

def _cubic(t):
    t=np.abs(t);a=-.75
    return np.where(t<=1,((a+2)*t-(a+3))*t*t+1,
                    np.where(t<2,((a*t-5*a)*t+8*a)*t-4*a,0))

def preprocess_rgb(rgb:np.ndarray,tw:int,th:int):
    rgb=np.asarray(rgb,dtype=np.float32)
    if rgb.ndim!=3 or rgb.shape[2]!=3 or not np.isfinite(rgb).all() or (rgb<0).any() or (rgb>1).any():
        raise ValueError('input must be finite display-like HWC RGB in [0,1]')
    sh,sw,_=rgb.shape;g=letterbox(sw,sh,tw,th)
    xs=(np.arange(g.content_w,dtype=np.float64)+.5)*sw/g.content_w-.5
    ys=(np.arange(g.content_h,dtype=np.float64)+.5)*sh/g.content_h-.5
    xb=np.floor(xs).astype(int);yb=np.floor(ys).astype(int)
    out=np.zeros((g.content_h,g.content_w,3),np.float64)
    for j in range(-1,3):
        wy=_cubic(ys-(yb+j))
        for i in range(-1,3):
            wx=_cubic(xs-(xb+i))
            sample=rgb[np.clip(yb+j,0,sh-1)[:,None],np.clip(xb+i,0,sw-1)[None,:]]
            out+=sample*wy[:,None,None]*wx[None,:,None]
    padded=np.empty((th,tw,3),np.float32);padded[:]=MEAN
    padded[g.top:g.top+g.content_h,g.left:g.left+g.content_w]=out
    return np.ascontiguousarray(((padded-MEAN)/STD).transpose(2,0,1)[None]),g

def materialize(raw:np.ndarray,g:Geometry,ow:int,oh:int):
    raw=np.asarray(raw,dtype=np.float32)
    if raw.shape!=(g.tensor_h,g.tensor_w) or ow<=0 or oh<=0:
        raise ValueError('shape does not match the recorded transform')
    # Inverse pixel-center transform. Clamp to the CONTENT, never sample padding.
    x=np.clip((np.arange(ow)+.5)*g.content_w/ow-.5+g.left,g.left,g.left+g.content_w-1)
    y=np.clip((np.arange(oh)+.5)*g.content_h/oh-.5+g.top,g.top,g.top+g.content_h-1)
    x0=np.floor(x).astype(int);y0=np.floor(y).astype(int)
    x1=np.minimum(x0+1,g.left+g.content_w-1);y1=np.minimum(y0+1,g.top+g.content_h-1)
    fx=x-x0;fy=y-y0
    out=np.zeros((oh,ow),np.float64);valid=np.ones((oh,ow),bool)
    for yy,wy in [(y0,1-fy),(y1,fy)]:
        for xx,wx in [(x0,1-fx),(x1,fx)]:
            a=raw[yy[:,None],xx[None,:]];w=wy[:,None]*wx[None,:]
            ok=np.isfinite(a)&(a>=0)
            valid &= ok | (w<=1e-7)
            out+=np.where(ok,a,0)*w
    out[~valid]=0
    return out.astype(np.float32),valid

def normalize_relative(a,low:float,high:float):
    a=np.asarray(a,dtype=np.float32)
    if not math.isfinite(low) or not math.isfinite(high) or high-low<=1e-6:
        return np.zeros_like(a),np.zeros_like(a,dtype=bool)
    valid=np.isfinite(a)
    result=np.clip((np.where(valid,a,low)-low)/(high-low),0,1)
    return np.where(valid,result,0).astype(np.float32),valid

def smooth_range(old,new,dt:float,tau:float,reset:bool):
    if not math.isfinite(dt) or not math.isfinite(tau) or dt<0 or tau<=0:
        raise ValueError('invalid range filter time')
    if len(new)!=2 or not all(math.isfinite(v) for v in new) or new[1]-new[0]<=1e-6:
        raise ValueError('invalid current range')
    if reset or old is None:return tuple(new)
    if len(old)!=2 or not all(math.isfinite(v) for v in old) or old[1]-old[0]<=1e-6:
        return tuple(new)
    alpha=-math.expm1(-dt/tau)
    return tuple(a+alpha*(b-a) for a,b in zip(old,new))
