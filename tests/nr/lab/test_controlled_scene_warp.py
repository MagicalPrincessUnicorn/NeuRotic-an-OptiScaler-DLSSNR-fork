"""Explicit WARP-only shader negative controls against an already built package.

Mutated shader captures bind the actual mutated shader hash, so rejection must
come from independent pixel expectations, not a mismatched source identity.
"""
import argparse
import hashlib
import importlib.util
import json
from pathlib import Path
import subprocess


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--package',type=Path,required=True)
    p.add_argument('--output',type=Path,required=True)
    a=p.parse_args();a.output.mkdir(parents=True,exist_ok=False)
    package=a.package.resolve()
    spec=importlib.util.spec_from_file_location('scene_oracle',package/'scene_oracle.py')
    oracle=importlib.util.module_from_spec(spec);spec.loader.exec_module(oracle)
    receipt=json.loads((package/'SOURCE_BUILD_RECEIPT.json').read_text(encoding='utf-8-sig'))
    compiler=next(r['compiler'] for r in receipt['builds'] if r['name']=='scene.cso')
    fxc=Path(compiler['path'])
    if hashlib.sha256(fxc.read_bytes()).hexdigest()!=compiler['sha256']:raise ValueError('FXC_IDENTITY_CHANGED')
    original=(package/'source/tools/nr/lab/rendering/scene/scene.hlsl').read_text()
    cases=[('motion-sign','mv = float2(oldOrigin-origin);','mv = float2(origin-oldOrigin);','role=motion'),
           ('wrong-depth','float z = .75;','float z = .5;','role=depth')]
    records=[]
    for name,before,after,expected in cases:
        assert original.count(before)==1
        root=a.output/name;root.mkdir();source=root/'mutant.hlsl';binary=root/'mutant.cso'
        source.write_text(original.replace(before,after),encoding='utf-8',newline='\n')
        args=[str(fxc),'/nologo','/T','cs_5_0','/E','main','/Ges','/WX','/Fo',str(binary),str(source)]
        proc=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=30)
        (root/'compile.log').write_bytes(proc.stdout)
        assert proc.returncode==0,'mutant must compile'
        run=root/'capture';run.mkdir()
        args=[str(package/'W03-P1-Supervisor.exe'),'--exe',str(package/'ControlledScene.exe'),
              '--receipt',str(run),'--timeout-ms','60000','--','--output',str(run),
              '--scene',str(package/'SCENE.json'),'--shader',str(binary),'--depth','conventional']
        proc=subprocess.run(args,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=65)
        (root/'supervise.log').write_bytes(proc.stdout)
        supervisor=json.loads((run/'supervisor.json').read_text())
        capture=json.loads((run/'scene.json').read_text())
        assert proc.returncode==0 and supervisor['child_exit_code']==0 and supervisor['child_drained'] and not supervisor['timed_out']
        assert capture['status']=='COMPLETE','mutant must finish cleanly before oracle rejection'
        reason=None
        try:
            oracle.validate_run(run,scene_bytes=(package/'SCENE.json').read_bytes(),
                                expected_shader_sha256=oracle.digest(binary.read_bytes()))
        except ValueError as e:reason=str(e)
        assert reason and 'ORACLE_MISMATCH' in reason and expected in reason,reason
        record=dict(name=name,status='REJECTED_AS_EXPECTED',reason=reason,worker_exit_code=0,
            mutated_shader_sha256=oracle.digest(binary.read_bytes()),provider_executed=False)
        records.append(record);print(name+': '+reason,flush=True)
    (a.output/'MUTATION_RESULTS.json').write_text(json.dumps(records,indent=2)+'\n',encoding='utf-8',newline='\n')


if __name__=='__main__':main()
