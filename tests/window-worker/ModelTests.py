"""Explicit local user model; verifies storage without loading the provider."""
import hashlib,json,subprocess,sys
from pathlib import Path
root=Path(__file__).resolve().parents[2]
if len(sys.argv)!=2:
 print('BLOCKED: supply explicit NR file for storage fixture');sys.exit(3)
original=Path(sys.argv[1]).resolve()
fixture=root/'builds/window-worker/model-fixture'
exe=root/'builds/window-worker/bin/NeuRotic.WindowWorker.exe'
created=None;checks=0
def run(*args):
 result=subprocess.run([str(exe),*map(str,args)],capture_output=True,text=True,encoding='utf-8',timeout=30)
 return result.returncode,json.loads(result.stdout)['model']
def check(ok,name):
 global checks
 checks+=1
 if not ok:raise AssertionError(name)
try:
 before=original.stat()
 code,model=run('--verify-model',original)
 check(code==0 and model['ready'] and model['bytes']==before.st_size,'named file recognized')
 check(model['sha256']==hashlib.sha256(original.read_bytes()).hexdigest(),'actual file identity retained')
 code,imported=run('--import-model',original,'--models-root',fixture)
 created=Path(imported['path']) if imported.get('ready') else None
 check(code==0 and imported['ready'],'valid model import')
 check(created==fixture.resolve()/'nvngx_dlssnr.dll','single flat deposit without hash subdirectory')
 check(created!=original and created.stat().st_size==before.st_size,'independent copied bytes')
 check(hashlib.sha256(created.read_bytes()).hexdigest()==model['sha256'],'actual copied SHA256')
 copied_id=created.stat().st_ino
 code,repeated=run('--import-model',original,'--models-root',fixture)
 check(code==0 and repeated['path']==str(created) and created.stat().st_ino==copied_id,'idempotent verified import preserves file')
 with created.open('r+b') as stream:stream.write(b'XX')
 code,changed=run('--verify-model',created)
 check(code==0 and changed['ready'],'changed content still recognized by filename')
 code,refused=run('--import-model',original,'--models-root',fixture)
 check(code==0 and refused['ready'],'existing named destination reused without overwrite')
 check(created.read_bytes()[:2]==b'XX','failed import leaves changed destination intact')
 after=original.stat()
 check((before.st_size,before.st_mtime_ns,before.st_ino)==(after.st_size,after.st_mtime_ns,after.st_ino),'original identity and last-write remain unchanged')
 print(f'PASS model storage {checks} actual file assertions')
except Exception as e:
 print('FAIL model storage:',str(e));sys.exit(1)
finally:
 if created and created.is_relative_to(fixture.resolve()):
  created.unlink(missing_ok=True)
  (fixture/'README.txt').unlink(missing_ok=True)
  try:created.parent.rmdir()
  except OSError:pass
