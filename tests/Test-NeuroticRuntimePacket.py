"""Run real Setup against controlled runtime evidence in inert game folders."""
import argparse
import hashlib
import json
import os
import shutil
import stat
import subprocess
import zipfile
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]
OUT=ROOT/'builds/ui-release'

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--package',type=Path,required=True)
    parser.add_argument('--receipt-name',default='runtime-packet-test')
    args=parser.parse_args()
    assert args.receipt_name.replace('-','').isalnum()
    fixture=OUT/'runtime-packet-fixture'
    assert not fixture.exists() and fixture.resolve().is_relative_to(OUT)
    fixture.mkdir();(fixture/'temp').mkdir()
    logs=[];checks=[]
    env={}
    for key,value in os.environ.items():
        if key.casefold() not in {k.casefold() for k in env}:env[key]=value
    windows=Path(next(v for k,v in env.items() if k.casefold()=='systemroot'))
    for key in list(env):
        if key.casefold() in {'path','psmodulepath','temp','tmp','include','lib','libpath'}:del env[key]
    env.update(PATH=str(windows/'System32'),PSModulePath='',TEMP=str(fixture/'temp'),TMP=str(fixture/'temp'))
    ps=windows/'System32/WindowsPowerShell/v1.0/powershell.exe'
    def check(label,ok):
        assert ok,label
        checks.append(label);print('PASS:',label,flush=True)
    def inventory(folder):
        return {str(p.relative_to(folder)):sha(p) for p in folder.rglob('*') if p.is_file()}
    try:
        with zipfile.ZipFile(args.package) as archive:
            check('ZIP CRCs',archive.testzip() is None)
            archive.extractall(fixture/'received packet')
        package=fixture/'received packet'/args.package.stem
        engine=package/'support/NeuRotic-Setup-Engine.ps1'
        helper=package/'support/NeuRotic-Prerequisites.ps1'
        manifest_path=package/'support/PACKAGE-MANIFEST.json'
        original_helper=helper.read_bytes();original_manifest=manifest_path.read_bytes()
        def set_evidence(kind):
            helper.write_bytes(original_helper);manifest_path.write_bytes(original_manifest)
            if kind=='live':return
            if kind=='missing':evidence='@()'
            else:
                version="[version]'14.29.30139.0'" if kind=='old' else '$null'
                evidence="@('msvcp140.dll','msvcp140_atomic_wait.dll','vcruntime140.dll','vcruntime140_1.dll') | ForEach-Object { [pscustomobject]@{Name=$_;Version="+version+"} }"
            # The controlled test packet is rehashed; no bypass exists in delivery.
            helper.write_bytes(original_helper + ('\nfunction Get-NeuroticVcRuntimeStatus([version]$MinimumVersion) {\n$testEvidence='+evidence+'\nTest-NeuroticVcRuntimeEvidence @($testEvidence) $MinimumVersion $true\n}\n').encode())
            manifest=json.loads(original_manifest)
            row=next(row for row in manifest['files'] if row['path']=='support\\NeuRotic-Prerequisites.ps1')
            row.update(bytes=helper.stat().st_size,sha256=sha(helper))
            manifest_path.write_text(json.dumps(manifest),encoding='utf-8')
        def game(name):
            folder=fixture/'Games'/name;folder.mkdir(parents=True)
            exe=folder/'FixtureGame.exe';shutil.copyfile(windows/'System32/cmd.exe',exe)
            (folder/'nvngx_dlssnr.dll').write_bytes(b'INERT PRIVATE MODEL - NEVER EXECUTE')
            (folder/'keep-save.dat').write_bytes(b'USER DATA')
            return folder,exe
        def run(label,script,arguments,expected,input_text=None):
            result=subprocess.run([str(ps),'-NoProfile','-ExecutionPolicy','Bypass','-File',str(script),*arguments],cwd=fixture,env=env,capture_output=True,text=True,encoding='utf-8',input=input_text,timeout=90)
            logs.append(label+f' exit={result.returncode}\n'+result.stdout+result.stderr)
            check(label,result.returncode==expected)
            return result.stdout+result.stderr
        for kind in ('missing','old','unknown'):
            set_evidence(kind)
            for mode in ('preflight','cancel','continue'):
                folder,exe=game(kind+'-'+mode)
                before=inventory(folder)
                arguments=['-GameExecutable',str(exe),'-ProxyName','dxgi.dll']+(['-CheckOnly'] if mode=='preflight' else ['-ConfirmInstall'])
                if mode=='continue':arguments+=['-ContinueWithoutRuntime']
                label=kind+' '+mode
                output=run(label,engine,arguments,0,'0\n' if mode=='cancel' else None)
                check(label+' official download/retry message','https://aka.ms/vc14/vc_redist.x64.exe' in output and 'run NeuRotic Setup again' in output)
                check(label+' explains runtime risk','may fail to load or display errors' in output)
                if mode=='continue':
                    check(label+' installs exact product',sha(folder/'dxgi.dll')==sha(package/'payload/OptiScaler.dll'))
                    check(label+' preserves original files',all(sha(folder/name)==digest for name,digest in before.items()))
                else:
                    check(label+' changes no game files',inventory(folder)==before)
        set_evidence('missing')
        for answer in ('1',''):
            folder,exe=game('interactive-'+('continue' if answer else 'cancel'))
            before=inventory(folder)
            label='interactive '+('continue' if answer else 'empty cancel')
            output=run(label,engine,['-GameExecutable',str(exe),'-ProxyName','dxgi.dll','-ConfirmInstall'],0,answer+'\n')
            check(label+' offers both choices','Continue installation anyway' in output and 'Cancel' in output)
            if answer:
                check(label+' installs exact product',sha(folder/'dxgi.dll')==sha(package/'payload/OptiScaler.dll'))
            else:
                check(label+' makes no changes',inventory(folder)==before)
        for operation in ('Update','Repair'):
            set_evidence('live');folder,exe=game('warning-'+operation)
            run(operation+' baseline install',engine,['-GameExecutable',str(exe),'-ProxyName','dxgi.dll','-ConfirmInstall'],0)
            (folder/'OptiScaler.ini').write_bytes(b'; USER SETTINGS\r\n[Log]\r\nLogToFile = true\r\n')
            before=inventory(folder);set_evidence('missing')
            arguments=['-GameExecutable',str(exe),'-ExistingInstallAction',operation]
            run(operation+' advisory preflight',engine,arguments+['-CheckOnly'],0)
            check(operation+' preflight makes no changes',inventory(folder)==before)
            run(operation+' cancel',engine,arguments+['-ConfirmInstall'],0,'0\n')
            check(operation+' cancel makes no changes',inventory(folder)==before)
            run(operation+' continue without runtime',engine,arguments+['-ConfirmInstall','-ContinueWithoutRuntime'],0)
            check(operation+' preserves settings and game data',all(sha(folder/name)==before[name] for name in ('OptiScaler.ini','nvngx_dlssnr.dll','FixtureGame.exe','keep-save.dat'))
                  and sha(folder/'dxgi.dll')==sha(package/'payload/OptiScaler.dll'))
        for method in ('downloaded','setup-action','restore','installed'):
            set_evidence('live');folder,exe=game('recovery-'+method);before=inventory(folder)
            run(method+' install with actual x64 runtime',engine,['-GameExecutable',str(exe),'-ProxyName','dxgi.dll','-ConfirmInstall'],0)
            check(method+' installed exact product',sha(folder/'dxgi.dll')==sha(package/'payload/OptiScaler.dll'))
            set_evidence('missing')
            if method=='restore':
                state=json.loads((folder/'NeuRotic/Installer/Current-Install.json').read_text(encoding='utf-8-sig'))
                backup=Path(state['restore_chain'][-1]).parent/'NeuRotic-Setup-Engine.ps1'
                run('restore works without runtime/helper',backup,['-Restore'],0)
            elif method=='installed':
                installed=folder/'NeuRotic/Installer/NeuRotic-Setup-Engine.ps1'
                run('installed uninstall works without helper',installed,['-Uninstall','-Installed','-GameDirectory',str(folder),'-UninstallMode','Full','-ConfirmUninstall'],10)
            elif method=='setup-action':
                run('Setup uninstall action works without runtime',engine,['-GameExecutable',str(exe),'-ExistingInstallAction','Uninstall','-UninstallMode','Full','-ConfirmUninstall'],0)
            else:
                run('downloaded uninstall works without runtime',engine,['-Uninstall','-GameExecutable',str(exe),'-UninstallMode','Full','-ConfirmUninstall'],0)
            check(method+' restores original game data',all(sha(folder/name)==digest for name,digest in before.items()) and not(folder/'dxgi.dll').exists())
        (OUT/(args.receipt_name+'.json')).write_text(json.dumps({'status':'PASS','packet_sha256':sha(args.package),'checks':checks,
            'controlled_evidence_packet_only':True,'game_or_provider_executed':False,'system_runtime_changed':False,'temporary_fixture_removed':True},indent=2))
    finally:
        (OUT/(args.receipt_name+'.log')).write_text('\n'.join(logs),encoding='utf-8')
        assert fixture.resolve().is_relative_to(OUT)
        for current,dirs,files in os.walk(fixture,followlinks=False):
            for name in dirs+files:assert not(os.stat(Path(current)/name,follow_symlinks=False).st_file_attributes & stat.FILE_ATTRIBUTE_REPARSE_POINT)
        shutil.rmtree(fixture)

if __name__=='__main__':main()
