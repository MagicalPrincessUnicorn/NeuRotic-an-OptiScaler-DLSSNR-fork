"""Exercise Desktop model recognition/import without loading a model or GPU."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile

worker = Path(sys.argv[1]).resolve()
results = []

def check(ok, name, detail=None):
    results.append(dict(case=name,passed=bool(ok),detail=detail))

def run(*args):
    proc = subprocess.run([str(worker), *map(str,args)], capture_output=True, text=True, timeout=30)
    model = json.loads(proc.stdout)['model']
    return proc.returncode, model

fixture_parent=Path(__file__).resolve().parents[2]/'builds'
with tempfile.TemporaryDirectory(prefix='nr-model-name-',dir=fixture_parent) as temp:
    root = Path(temp)
    assert root.resolve().parent == fixture_parent.resolve()
    model = root/'nvngx_dlssnr.dll'
    for label, data in [('different-build',b'filename fixture'),('empty',b''),('changed-content',b'new bytes')]:
        model.write_bytes(data)
        code, result = run('--verify-model',model)
        check(code==0 and result['ready'],label,result['reason'])
        if result['ready']:
            check(result['sha256']==hashlib.sha256(data).hexdigest(),label+'-copy-identity')
    upper=root/'NVNGX_DLSSNR.DLL'
    model.rename(root/'rename.tmp');(root/'rename.tmp').rename(upper)
    code,result=run('--verify-model',upper)
    check(code==0 and result['ready'],'case-insensitive-name',result['reason'])
    code,result=run('--import-model',upper,'--models-root',root/'deposit')
    check(code==0 and result['ready'],'import-different-build',result['reason'])
    deposited=root/'deposit'/'nvngx_dlssnr.dll'
    if deposited.exists():
        check(deposited.read_bytes()==upper.read_bytes(),'import-preserves-bytes')
        deposited.write_bytes(b'user replacement')
        code,result=run('--import-model',upper,'--models-root',root/'deposit')
        check(code==0 and result['ready'] and deposited.read_bytes()==b'user replacement','existing-user-file-preserved',result['reason'])
    wrong=root/'other.dll';wrong.write_bytes(b'filename fixture')
    code,result=run('--verify-model',wrong)
    check(code!=0 and not result['ready'],'wrong-name-refused')
    code,result=run('--verify-model',root/'missing'/'nvngx_dlssnr.dll')
    check(code!=0 and not result['ready'],'missing-file-refused')
    directory=root/'directory'/'nvngx_dlssnr.dll';directory.mkdir(parents=True)
    code,result=run('--verify-model',directory)
    check(code!=0 and not result['ready'],'directory-is-not-file')

text=json.dumps(results,indent=2)
print(text)
if len(sys.argv)>2:Path(sys.argv[2]).write_text(text+'\n')
sys.exit(any(not r['passed'] for r in results))
