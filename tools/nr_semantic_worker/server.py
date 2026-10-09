"""Explicitly launched CPU worker; inherited local pipes only. No network or downloads."""
import hashlib, json, os, sys
from pathlib import Path
ROOT=Path(__file__).resolve().parent
sys.path.insert(0,str(ROOT))
sys.path.insert(0,str(ROOT/'packages'))
from protocol import read_message,read_exact,write_message,validate_request

def main():
    source=sys.stdin.buffer; sink=sys.stdout.buffer
    hello=read_message(source)
    if set(hello)!={'schema','nonce'} or type(hello['schema']) is not int or hello['schema'] not in (2,3) or not isinstance(hello['nonce'],str) or len(hello['nonce'])!=32:
        raise ValueError('bad handshake')
    nonce=hello['nonce'];schema=hello['schema']
    import numpy as np
    import cv2 as cv
    from providers.zoo_character import ZooCpuProvider
    specs=json.loads((ROOT/'models.lock.json').read_text())['models']
    provider=ZooCpuProvider(*(ROOT/s['destination'] for s in specs))
    hashes=[s['sha256'] for s in specs]
    from tracking_engine import TrackingEngine
    engine=TrackingEngine(provider) if schema==3 else None
    write_message(sink,{'schema':schema,'nonce':nonce,'pid':os.getpid(),'provider':'opencv-zoo-cpu','models':hashes})
    while True:
        try: request=validate_request(read_message(source),nonce)
        except EOFError:
            if engine:engine.close()
            return 0
        if request['schema']!=schema:raise ValueError('schema changed during session')
        raw=read_exact(source,request['bytes'])
        pixels=np.frombuffer(raw,np.uint8).reshape(request['height'],request['stride'])[:,:request['width']*4]
        image=cv.cvtColor(pixels.reshape(request['height'],request['width'],4),cv.COLOR_RGBA2BGR)
        # Binary request key remains the host's authority, never a worker-created live key.
        reply={'schema':schema,'nonce':nonce,'pid':os.getpid(),'models':hashes,
               'sequence':request['sequence'],'epoch':request['epoch'],'capture_ns':request['capture_ns'],
               'width':request['width'],'height':request['height'],'image_sha256':hashlib.sha256(raw).hexdigest()}
        try:
            if engine:
                current=engine.step(image,request,reply['image_sha256'])
                reply.update(status='ok',persons=[{'box':p['box'],'score':p['score'],'geometry':p['box_semantics'],
                    'class_id':p['class_id'],'track_id':p['track_id'],'source':p['source'],'pose_source':p['pose_source'],
                    'quality':p['quality'],'velocity':p['velocity'],
                    'pose':[[k['x'],k['y'],k['visibility'],k['presence'],k['z_relative']] for k in p['landmarks']]
                    } for p in current['persons']],tracking=current['tracking'])
                write_message(sink,reply)
                continue
            people=provider.analyze(image,request['maximum_persons'],request['pose_requested'],request['detect_objects'])
            reply.update(status='ok',persons=[{'box':p['box'],'score':p['score'],
                'geometry':p['box_semantics'],'class_id':p['class_id'],
                'pose':[[k['x'],k['y'],k['visibility'],k['presence'],k['z_relative']] for k in p['landmarks']]} for p in people],stats=provider.stats)
        except Exception as exc:
            reply.update(status='error',reason=type(exc).__name__+': '+str(exc)[:240],persons=[])
        write_message(sink,reply)

if __name__=='__main__':
    try: raise SystemExit(main())
    except Exception as exc:
        print(type(exc).__name__+': '+str(exc)[:240],file=sys.stderr);raise SystemExit(1)
