"""Exercise the delivered schema-3 worker, actual models and moving images."""
import argparse,hashlib,json,queue,subprocess,sys,threading,time
from pathlib import Path

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--worker',type=Path,required=True)
    parser.add_argument('--image',type=Path,required=True);parser.add_argument('--receipt',type=Path,required=True)
    args=parser.parse_args();root=args.worker.resolve();sys.path[:0]=[str(root/'packages'),str(root)]
    import cv2 as cv
    import numpy as np
    from protocol import read_message,write_message
    image=cv.imread(str(args.image));assert image is not None
    image=cv.resize(image,(360,480));records=[]
    p=subprocess.Popen([str(root/'runtime/python.exe'),'-I','-B',str(root/'server.py')],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    replies=queue.Queue()
    def receive():
        try:
            while True:replies.put(read_message(p.stdout,1024*1024))
        except BaseException as e:replies.put(e)
    threading.Thread(target=receive,daemon=True).start()
    def reply():
        value=replies.get(timeout=10)
        if isinstance(value,BaseException):raise value
        return value
    nonce='7'*32
    try:
        write_message(p.stdin,dict(schema=3,nonce=nonce));hello=reply();assert hello['schema']==3
        history={};sequence=0
        for epoch,objects,pose in [(1,True,False),(2,True,True),(3,False,False),(4,False,True)]:
            for step in range(45):
                sequence+=1;shift=min(step,20)*.5
                moved=cv.warpAffine(image,np.float32([[1,0,shift],[0,1,0]]),(360,480))
                rgba=cv.cvtColor(moved,cv.COLOR_BGR2RGBA);raw=rgba.tobytes();capture=time.monotonic_ns();sha=hashlib.sha256(raw).hexdigest()
                req=dict(schema=3,nonce=nonce,sequence=sequence,epoch=[epoch]*8,capture_ns=capture,
                    width=360,height=480,stride=1440,bytes=len(raw),maximum_persons=16,pose_requested=pose,detect_objects=objects,hold_ms=80)
                history[sequence]=(capture,sha,epoch)
                started=time.perf_counter();write_message(p.stdin,req);p.stdin.write(raw);p.stdin.flush();r=reply()
                elapsed=(time.perf_counter()-started)*1000
                assert r['status']=='ok',r
                assert r['sequence']==sequence and r['image_sha256']==sha and r['epoch']==req['epoch']
                assert len(r['persons'])<=16 and len({t['track_id'] for t in r['persons']})==len(r['persons'])
                for t in r['persons']:
                    source=t['source'];original=history[source['sequence']]
                    assert original==(source['capture_ns'],source['image_sha256'],epoch)
                    assert 0<=capture-source['capture_ns']<=350_000_000
                    assert t['quality']>=.65 and (pose or not t['pose'])
                    if t['pose']:
                        ps=t['pose_source'];assert history[ps['sequence']]==(ps['capture_ns'],ps['image_sha256'],epoch)
                        assert capture-ps['capture_ns']<=250_000_000
                records.append(dict(epoch=epoch,sequence=sequence,reply_ms=elapsed,count=len(r['persons']),
                    poses=sum(bool(t['pose']) for t in r['persons']),classes=[t['class_id'] for t in r['persons']],**r['tracking']))
                time.sleep(max(0,.025-elapsed/1000))
            mode=[row for row in records if row['epoch']==epoch]
            assert max(row['count'] for row in mode)>0,'No tracked subjects in actual mode '+str(epoch)
            print('PASS actual mode',epoch,'max boxes',max(row['count'] for row in mode),'max poses',max(row['poses'] for row in mode),flush=True)
        p.stdin.close();p.wait(timeout=10);assert p.returncode==0,p.stderr.read().decode(errors='replace')
    finally:
        if p.poll() is None:p.kill();p.wait()
    import statistics
    summary=dict(frames=len(records),median_reply_ms=statistics.median(r['reply_ms'] for r in records),
        median_tracking_ms=statistics.median(r['tracking_ms'] for r in records),max_reply_ms=max(r['reply_ms'] for r in records))
    args.receipt.parent.mkdir(parents=True,exist_ok=True);args.receipt.write_text(json.dumps(dict(summary=summary,records=records),indent=2)+'\n')
    print(json.dumps(summary))

if __name__=='__main__':main()
