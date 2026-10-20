"""Run the discovery helper from the delivered ZIP without a development runtime.

Only extracted helpers and inert temporary game files are used; no game launches
or installations occur. A startup failure preserves a receipt and cleans fixtures.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import zipfile

ROOT=Path(__file__).resolve().parents[2]
FIXTURE=ROOT/'builds/character-inspector/discovery-package-fixture'
sha=lambda raw:hashlib.sha256(raw).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--archive',type=Path,required=True)
    parser.add_argument('--receipt',type=Path,required=True)
    parser.add_argument('--log',type=Path,required=True)
    args=parser.parse_args()
    assert not FIXTURE.exists(),'Inspect an existing fixture before reuse'
    for node in (FIXTURE,*FIXTURE.parents):
        if node.exists():assert not node.stat(follow_symlinks=False).st_file_attributes&0x400
    FIXTURE.mkdir();logs=[];checks=[]
    receipt={'status':'FAIL','archive_sha256':sha(args.archive.read_bytes()),
        'game_launched':False,'game_installed':False,'temporary_fixture_removed':False}
    try:
        with zipfile.ZipFile(args.archive) as z:
            assert z.testzip() is None
            for name in z.namelist():
                if not name.startswith('NeuRotic/discovery/'):continue
                assert ':' not in name and '\\' not in name and '..' not in name.split('/'), 'Unsafe discovery archive path'
                path=FIXTURE/name.removeprefix('NeuRotic/')
                assert path.resolve().is_relative_to(FIXTURE.resolve()), 'Discovery archive escapes fixture'
                path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(z.read(name))
        env={k:v for k,v in os.environ.items() if k.upper() not in ('PATH','DOTNET_ROOT','DOTNET_ROOT_X64','DOTNET_MULTILEVEL_LOOKUP')}
        env.update(PATH='C:/Windows/System32',DOTNET_ROOT=str(FIXTURE/'absent-runtime'),
            DOTNET_ROOT_X64=str(FIXTURE/'absent-runtime'),DOTNET_MULTILEVEL_LOOKUP='0',
            DOTNET_CLI_TELEMETRY_OPTOUT='1',NEUROTIC_HUB_FIXTURE_ROOT=str(FIXTURE/'user-data'))
        helper=FIXTURE/'discovery/NeuRotic.Discovery.exe'
        def run(label,arguments=(),request=None):
            result=subprocess.run([str(helper),*arguments],cwd=FIXTURE,env=env,
                input=None if request is None else json.dumps(request).encode(),
                capture_output=True,timeout=120,creationflags=subprocess.CREATE_NO_WINDOW)
            logs.append(label+'\n'+result.stdout.decode(errors='replace')+result.stderr.decode(errors='replace'))
            print(label,'exit',result.returncode,flush=True)
            assert result.returncode==0,logs[-1]
            checks.append(label)
            return result
        boot=run('extracted discovery self-test without development runtime',
            ['--self-test',str(FIXTURE/'self-test')])
        receipt['self_test_pass_lines']=boot.stdout.count(b'PASS ')
        games=FIXTURE/'Games';local=games/'Local';local.mkdir(parents=True)
        exe=local/'Local.exe';shutil.copyfile('C:/Windows/System32/cmd.exe',exe)
        (local/'save.dat').write_bytes(b'PRESERVE LOCAL SAVE')
        baseline={str(p.relative_to(games)):(sha(p.read_bytes()),p.stat().st_mtime_ns)
            for p in games.rglob('*') if p.is_file()}
        result=run('Hub-compatible folder discovery JSON',request={
            'protocolVersion':2,'kind':'DiscoverFolder','roots':[str(games)]})
        found=json.loads(result.stdout)
        assert found['protocolVersion']==2 and found['kind']=='DiscoveryResult'
        assert len(found['games'])==1 and found['games'][0]['title']=='Local'
        assert Path(found['games'][0]['executable'])==exe
        checks.append('folder scan finds the inert executable with exact source path')
        assert baseline=={str(p.relative_to(games)):(sha(p.read_bytes()),p.stat().st_mtime_ns)
            for p in games.rglob('*') if p.is_file()}
        checks.append('scan preserves game file hashes and timestamps')
        required={'NeuRotic.Discovery.exe','NeuRotic.Discovery.dll','NeuRotic.Discovery.deps.json',
            'NeuRotic.Discovery.runtimeconfig.json','hostfxr.dll','hostpolicy.dll','coreclr.dll'}
        assert required.issubset(p.name for p in (FIXTURE/'discovery').iterdir())
        checks.append('complete self-contained apphost deployment')
        receipt.update(status='PASS',checks=checks,
            helper_sha256=sha(helper.read_bytes()),
            helper_dll_sha256=sha((helper.parent/'NeuRotic.Discovery.dll').read_bytes()))
    except Exception as error:
        receipt.update(error=str(error),checks=checks)
        raise
    finally:
        assert FIXTURE==ROOT/'builds/character-inspector/discovery-package-fixture'
        for base,dirs,files in os.walk(FIXTURE,followlinks=False):
            for name in dirs+files:assert not Path(base,name).stat(follow_symlinks=False).st_file_attributes&0x400
        shutil.rmtree(FIXTURE)
        receipt['temporary_fixture_removed']=True
        args.receipt.write_text(json.dumps(receipt,indent=2)+'\n')
        args.log.write_text('\n\n'.join(logs)+'\n',encoding='utf-8')
    print(json.dumps(receipt,indent=2))

if __name__=='__main__':main()
