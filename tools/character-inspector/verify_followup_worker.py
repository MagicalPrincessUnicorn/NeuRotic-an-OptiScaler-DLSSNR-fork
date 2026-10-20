"""Verify explicitly supplied local worker/model code; no downloads or game execution."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import struct
import subprocess
import sys
import time

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--worker',type=Path,required=True)
    parser.add_argument('--image',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    args=parser.parse_args();worker=args.worker.resolve()
    sys.path[:0]=[str(worker/'packages'),str(worker)]
    import cv2 as cv
    from providers.zoo_character import ZooCpuProvider
    specs=json.loads((worker/'models.lock.json').read_text())['models']
    provider=ZooCpuProvider(*(worker/s['destination'] for s in specs))
    image=cv.imread(str(args.image));assert image is not None
    receipt={'image_sha256':hashlib.sha256(args.image.read_bytes()).hexdigest(),
             'model_hashes':[s['sha256'] for s in specs],'tests':[]}
    for objects,pose in ((False,False),(False,True),(True,False),(True,True)):
        for repeat in range(2):
            started=time.perf_counter();people=provider.analyze(image,16,with_pose=pose,detect_objects=objects)
            elapsed=(time.perf_counter()-started)*1000
            row={'objects':objects,'pose':pose,'repeat':repeat,'elapsed_ms':elapsed,
                 'stats':provider.stats.copy(),'persons':people}
            receipt['tests'].append(row)
            assert len(people)<=16 and provider.stats['returned']==len(people)
            assert pose or provider.stats['pose_attempts']==0
            assert provider.stats['usable_poses']==sum(len(p['landmarks'])==33 for p in people)
            assert all(not p['landmarks'] or p['class_id']==0 for p in people)
            print(objects,pose,repeat,round(elapsed,2),provider.stats,
                  [(p['class_id'],p['box_semantics']) for p in people],flush=True)
    fast=receipt['tests'][4]['persons'];detailed=receipt['tests'][6]['persons']
    assert [(p['class_id'],p['box']) for p in fast]==[(p['class_id'],p['box']) for p in detailed], 'pose detail changed detector geometry'
    nonce='1'*32;pixels=b'\0\0\0\xff'*(320*240)
    def frame(value):
        raw=json.dumps(value).encode();return struct.pack('<I',len(raw))+raw
    requests=[]
    for i,objects in enumerate((False,True)):
        requests.append(frame({'schema':2,'nonce':nonce,'sequence':i+1,'epoch':[1,2,3,4,5,6,7,8],
            'capture_ns':100+i,'width':320,'height':240,'stride':1280,'bytes':len(pixels),
            'maximum_persons':16,'pose_requested':False,'detect_objects':objects})+pixels)
    result=subprocess.run([str(worker/'runtime/python.exe'),'-I',str(worker/'server.py')],
        input=frame({'schema':2,'nonce':nonce})+b''.join(requests),capture_output=True,timeout=30)
    assert result.returncode==0,result.stderr
    stream=io.BytesIO(result.stdout);messages=[]
    while prefix:=stream.read(4):
        assert len(prefix)==4
        size=struct.unpack('<I',prefix)[0];assert size<=1048576
        raw=stream.read(size);assert len(raw)==size;messages.append(json.loads(raw))
    assert len(messages)==3 and messages[0]['models']==receipt['model_hashes']
    for i,r in enumerate(messages[1:]):
        assert r['schema']==2 and r['sequence']==i+1 and r['status']=='ok' and r['persons']==[]
        assert r['image_sha256']==hashlib.sha256(pixels).hexdigest()
    receipt['pipe_messages']=messages
    args.receipt.write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    print('PASS real pinned CPU models and isolated schema2 child: both modes, exact model/image identity')

if __name__=='__main__': main()
