from pathlib import Path
import argparse, hashlib, json, os, shutil, subprocess, zipfile, struct, io, sys, time
sys.path.insert(0,str(Path(__file__).resolve().parent))
from package_checks import run_owned_child

root=Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--work',type=Path,default=root/'builds/character-inspector')
parser.add_argument('--motion-image',type=Path)
args=parser.parse_args()
work=args.work.resolve()
assert work.is_relative_to(root.resolve()/'builds')
# Keep the fake game shallow enough for the engine's existing Windows backup
# path bound. This directory is exclusive to this verifier and must not exist.
fixture=root.parent/'_inspector-motion-fixture'
assert not fixture.exists()
fixture.mkdir()
receipt_data=json.loads((work/'live-package-receipt.json').read_text())
(work/'live-installer-tests.json').unlink(missing_ok=True)
archive_path=Path(receipt_data['path'])
product_hash=receipt_data['product_sha256']
checks, logs, timings = [], [], {}
sha = lambda raw: hashlib.sha256(raw).hexdigest()
def check(label, condition):
    assert condition, label
    checks.append(label)
    print('PASS', label, flush=True)
def inventory(path):
    return {str(p.relative_to(path)): sha(p.read_bytes()) for p in path.rglob('*') if p.is_file()}
try:
    with zipfile.ZipFile(archive_path) as archive:
        check('ZIP CRC', archive.testzip() is None)
        for name in archive.namelist():
            assert not Path(name).is_absolute() and '..' not in Path(name).parts and ':' not in name
        archive.extractall(fixture / 'package')
        folder=archive.namelist()[0].split('/')[0]
    package = fixture / 'package' / folder
    check('original pre-App Setup launcher bytes',sha((package/'NeuRotic-Setup.cmd').read_bytes())==
        '9c6158036ef49f8c487b192c59d5e9b5587c8ec8196f5da10b9262d501b41a79')
    check('replacement console menu absent',not (package/'Install-NeuRotic.cmd').exists() and
        not (package/'support/Inspector-Console.ps1').exists())
    worker=package/'payload/OptiScaler/CharacterInspector'
    if args.motion_image:
        manifest=json.loads((package/'support/PACKAGE-MANIFEST.json').read_text())
        provenance=manifest['component_provenance']['inspector']
        check('current Inspector provenance matches packaged schema and DLL',
            provenance['private_protocol_schema']==3 and provenance['product_sha256']==product_hash and
            provenance['product_source_commit']==receipt_data['product_source_commit'])
        motion=subprocess.run([sys.executable,'-I','-B',str(root/'tools/character-inspector/verify_motion_worker.py'),
            '--worker',str(worker),'--image',str(args.motion_image),'--receipt',str(work/'packaged-motion-worker.json')],
            capture_output=True,text=True,timeout=90)
        logs.append('packaged motion worker\n'+motion.stdout+motion.stderr)
        check('actual packaged schema3 models and tracking',motion.returncode==0)
    nonce='1'*32
    message=lambda v:struct.pack('<I',len(json.dumps(v).encode()))+json.dumps(v).encode()
    width,height=320,240
    pixels=b'\0\0\0\xff'*(width*height)
    request={'schema':2,'nonce':nonce,'sequence':1,'epoch':[1,2,3,4,5,6,7,8],
             'capture_ns':100,'width':width,'height':height,'stride':width*4,'bytes':len(pixels),'maximum_persons':4,'pose_requested':False,'detect_objects':False}
    pid,code,output,errors=run_owned_child([str(worker/'runtime/python.exe'),'-I','-B',str(worker/'server.py')],
        message({'schema':2,'nonce':nonce})+message(request)+pixels,worker,30,logs)
    check('portable isolated worker exit',code==0)
    channel=io.BytesIO(output)
    def reply():
        prefix=channel.read(4);assert len(prefix)==4
        size=struct.unpack('<I',prefix)[0];assert size<=1048576
        return json.loads(channel.read(size))
    hello,response=reply(),reply()
    check('portable worker exact PID/models',hello['pid']==pid and hello['nonce']==nonce and hello['models']==[m['sha256'] for m in json.loads((worker/'models.lock.json').read_text())['models']])
    check('portable real CPU no-match',response['status']=='ok' and response['persons']==[])
    check('portable source image/key bound',response['image_sha256']==sha(pixels) and response['epoch']==request['epoch'] and response['sequence']==1 and not channel.read())

    game = fixture / 'Other D3D12 Game'
    game.mkdir()
    exe = game / 'FixtureGame.exe'
    shutil.copyfile('C:/Windows/System32/cmd.exe', exe)
    (game / 'dxgi.dll').write_bytes(b'ORIGINAL NON-NEUROTIC PROXY')
    (game / 'save.dat').write_bytes(b'KEEP MY SAVE')
    (game / 'nvngx_dlssnr.dll').write_bytes(b'PRIVATE MODEL SENTINEL - NEVER EXECUTED')
    doom = fixture / 'Separate Vulkan Game'
    doom.mkdir()
    (doom / 'winmm.dll').write_bytes(b'SEPARATE PATCHING LINE')
    (doom / 'OptiScaler.ini').write_bytes(b'[Menu]\nOverlayMenu=true\n')
    baseline, untouched = inventory(game), inventory(doom)
    engine = package / 'support/NeuRotic-Setup-Engine.ps1'
    env = {}
    for key, value in os.environ.items():
        if key.lower() not in {k.lower() for k in env}: env[key] = value
    env['PSModulePath'] = ''
    def run(label, *arguments):
        command = [str(Path(os.environ['SystemRoot'])/'System32/cmd.exe'), '/d', '/c', 'NeuRotic-Setup.cmd',
                   '-GameExecutable', str(exe), *arguments]
        # Keep progress readable during a long restore chain, including on timeout.
        progress = work / 'live-installer-active-stage.txt'
        started = time.monotonic()
        print('START', label, flush=True)
        try:
            with progress.open('wb') as stream:
                result = subprocess.run(command, cwd=package, env=env, input='', text=True,
                                        stdout=stream, stderr=stream,
                                        timeout=900 if label == 'full uninstall' else 300)
        finally:
            timings[label] = round(time.monotonic() - started, 2)
            output = progress.read_text(encoding='utf-8', errors='replace')
            logs.append(label + '\n' + output)
            (work / 'live-installer-tests.txt').write_text('\n\n'.join(logs)+'\n', encoding='utf-8')
            print('TIME', label, timings[label], 'seconds', flush=True)
        check(label + ' exit 0', result.returncode == 0)
        return output
    run('preflight', '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace', '-CheckOnly')
    check('preflight changes no files', inventory(game) == baseline)
    locked_model=worker/'models/person_detection_mediapipe_2023mar.onnx'
    original_model=locked_model.read_bytes()
    try:
        locked_model.write_bytes(b'TAMPERED MODEL FIXTURE')
        rejected=subprocess.run(['C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe',
            '-NoProfile','-ExecutionPolicy','Bypass','-File',str(engine),'-GameExecutable',str(exe),
            '-ProxyName','dxgi.dll','-ExistingProxyAction','Replace','-CheckOnly'],
            cwd=fixture,env=env,input='',text=True,capture_output=True,encoding='utf-8',timeout=120)
        logs.append('tampered model preflight\n'+rejected.stdout+rejected.stderr)
        check('tampered model package refused',rejected.returncode!=0)
        check('tampered model refusal changes no game files',inventory(game)==baseline)
    finally:
        locked_model.write_bytes(original_model)
    output = run('fresh installation', '-ProxyName', 'dxgi.dll', '-ExistingProxyAction', 'Replace', '-ConfirmInstall')
    check('completion message', 'full installation verified' in output.lower())
    check('installed exact inspector DLL', sha((game / 'dxgi.dll').read_bytes()) ==
          product_hash)
    check('installed full forwarder and SDK', all((game / name).is_file() for name in
          ['nvngx.dll_dlssnr.dll', 'OptiScaler/amd_fidelityfx_dx12.dll']))
    check('fresh INI matches packaged defaults', (game / 'OptiScaler.ini').read_bytes() ==
          (package / 'payload/OptiScaler.ini').read_bytes())
    check('offline worker/runtime/models installed', all((game/name).is_file() for name in ['OptiScaler/CharacterInspector/server.py','OptiScaler/CharacterInspector/runtime/python.exe','OptiScaler/CharacterInspector/packages/cv2/cv2.pyd','OptiScaler/CharacterInspector/models/person_detection_mediapipe_2023mar.onnx','OptiScaler/CharacterInspector/models/pose_estimation_mediapipe_2023mar.onnx']))
    check('extra MIT notice installed', (game / 'Licenses/CharacterInspector-MIT.txt').is_file())
    for name in ['FixtureGame.exe', 'save.dat', 'nvngx_dlssnr.dll']:
        check('preserved ' + name, sha((game / name).read_bytes()) == baseline[name])
    edited = b'; USER SETTINGS\r\n[Menu]\r\nOverlayMenu=true\r\n[CharacterInspector]\r\nEnabled=true\r\nMaximumPersons=2\r\n'
    (game / 'OptiScaler.ini').write_bytes(edited)
    run('update', '-ExistingInstallAction', 'Update', '-ConfirmInstall')
    check('update preserves user settings', (game / 'OptiScaler.ini').read_bytes() == edited)
    (game / 'dxgi.dll').unlink()
    run('repair missing DLL', '-ExistingInstallAction', 'Repair', '-ConfirmInstall')
    check('repair recreates exact DLL', sha((game / 'dxgi.dll').read_bytes()) ==
          product_hash)
    check('repair preserves settings', (game / 'OptiScaler.ini').read_bytes() == edited)
    run('full uninstall', '-Uninstall', '-UninstallMode', 'Full', '-ConfirmUninstall')
    check('uninstall restores exact original proxy and baseline files',
          all(sha((game / name).read_bytes()) == digest for name, digest in baseline.items()))
    check('uninstall removes owned forwarder and SDK', not (game / 'nvngx.dll_dlssnr.dll').exists()
          and not (game / 'OptiScaler/amd_fidelityfx_dx12.dll').exists()
          and not (game / 'OptiScaler/CharacterInspector/server.py').exists())
    check('separate Vulkan fixture untouched', inventory(doom) == untouched)
    receipt = {'status': 'PASS', 'package_sha256': sha(archive_path.read_bytes()),
               'checks': checks, 'stage_seconds': timings, 'real_game_files_changed': False, 'game_executed': False,
               'gpu_executed': False, 'temporary_fixture_removed': True}
finally:
    (work / 'live-installer-tests.txt').write_bytes(('\n\n'.join(logs)+'\n').encode())
    # Preserve the installer's detailed rollback reason before deleting the fixture.
    if 'receipt' not in globals():
        records = {str(p.relative_to(fixture)): json.loads(p.read_text(encoding='utf-8-sig'))
                   for p in fixture.glob('Other D3D12 Game/NeuRotic-backups/*/INSTALL-MANIFEST.json')}
        (work / 'live-installer-failure-records.json').write_text(json.dumps(records, indent=2)+'\n', encoding='utf-8')
    assert fixture.resolve()==root.resolve().parent/'_inspector-motion-fixture'
    assert not (fixture.stat().st_file_attributes&0x400)
    for directory, dirs, files in os.walk(fixture, followlinks=False):
        for name in dirs + files:
            assert not (os.stat(Path(directory, name), follow_symlinks=False).st_file_attributes & 0x400)
    shutil.rmtree(fixture)
(work / 'live-installer-tests.json').write_text(json.dumps(receipt, indent=2)+'\n', encoding='utf-8')
print('PASS full installer lifecycle; fixture cleaned.', flush=True)
