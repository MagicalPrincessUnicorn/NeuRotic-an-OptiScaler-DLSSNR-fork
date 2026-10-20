"""Package the exact UI review build with the matching flagship installer and dependencies."""
import hashlib,json,re,subprocess,zipfile
from pathlib import Path
root=Path(__file__).resolve().parents[3]
base=root.parent/'_exports/NeuRotic-Flagship-Vulkan-35326b84.zip'
def sha(raw): return hashlib.sha256(raw).hexdigest()
def encoded(value): return (json.dumps(value,indent=2)+'\n').encode('utf-8')
def git(*args): return subprocess.check_output(['git','-c','safe.directory='+root.as_posix(),'-C',str(root),*args],text=True).strip()
commit=git('rev-parse','HEAD')
receipt=json.loads((root/'builds/ui-release/sleek/exact-build.json').read_text(encoding='utf-8'))
assert receipt['source_commit']==commit and receipt['source_unchanged'] and receipt['exit_code']==0
assert not git('status','--porcelain','--','OptiScaler')
assert git('merge-base',commit,'35326b84')==git('rev-parse','35326b84')
files={}
with zipfile.ZipFile(base) as source:
    assert source.testzip() is None
    prefix=source.namelist()[0].split('/')[0]+'/'
    manifest=json.loads(source.read(prefix+'support/PACKAGE-MANIFEST.json'))
    for row in manifest['files']:
        path=row['path'].replace('\\','/')
        assert ':' not in path and '..' not in path.split('/') and path not in files
        raw=source.read(prefix+path)
        assert len(raw)==row['bytes'] and sha(raw)==row['sha256']
        files[path]=raw
prior=json.loads(files['support/BUILD-MANIFEST.json'])
assert prior['product_source_commit'].startswith('35326b84')
# UI work never changes or relabels the forwarder, provider adapters, launchers or installer.
installer_hashes={path:sha(raw) for path,raw in files.items() if path in
 {'NeuRotic-Setup.cmd','NeuRotic-Uninstall.cmd','support/NeuRotic-Setup-Engine.ps1'}}
product=(root/'builds/ui-release/bin/OptiScaler.dll').read_bytes()
assert product[:2]==b'MZ' and sha(product)==receipt['product']['sha256']
files['payload/OptiScaler.dll']=product
files['payload/OptiScaler.ini']=(root/'integration/OptiScaler.ini').read_bytes()
assert not git('status','--porcelain','--','OptiScaler.ini','integration/OptiScaler.ini')
for name in ('OptiScaler.ini','integration/OptiScaler.ini'):
    old=subprocess.check_output(['git','-c','safe.directory='+root.as_posix(),'-C',str(root),
        'show','35326b84:'+name])
    new=(root/name).read_bytes()
    # Root INI is Git text; the integration INI retains its exact mixed line endings.
    if name=='OptiScaler.ini': old,new=old.replace(b'\r\n',b'\n'),new.replace(b'\r\n',b'\n')
    start=re.search(rb'(?m)^\[Menu\][^\r\n]*[\r\n]+',old).end()
    following=re.search(rb'(?m)^\[',old[start:])
    end=start+following.start() if following else len(old)
    menu=old[start:end]
    for key,value in ((b'Scale',b'auto'),(b'OptiClip',b'false')):
        menu,count=re.subn(rb'(?m)^'+key+rb'[ \t]*=[^\r\n]*',key+b' = '+value,menu)
        assert count==1
    assert old[:start]+menu+old[end:]==new, 'Unexpected INI change: '+name
prior.update({'commit':commit,'product_source_commit':commit,'product_sha256':sha(product),
 'candidate':'Sleek UI screenshot-panel crash repair','prior_acceptance_transfers_to_this_binary':False,
 'game_runtime_verified':False,'ui_baseline_source':'35326b84','build':{'exit_code':0,'compiler':'14.44.35207'}})
files['support/BUILD-MANIFEST.json']=encoded(prior)
files['support/UI-BUILD-RECEIPT.json']=encoded(receipt)
files['UI-REVIEW.txt']=(
 'NeuRotic - Sleek UI review\r\n\r\n'
 'A separate UI checkpoint based on flagship source 35326b84.\r\n'
 'Repairs a crash when expanding Comparison screenshots: its shared keybind\r\n'
 'control now opens its required table outside General settings.\r\n'
 'Seven equal top tabs; scale, graphs, theme, save and close in the header.\r\n'
 'Thin tab dividers, centered captions and continuous section opening/collapse.\r\n'
 'Dropdowns unfold at steady speed with exact endpoints; sibling section gaps\r\n'
 'use the same tight rhythm across all seven tabs, including nested NR panels.\r\n'
 'Left Ko-fi support, language and compact device readout; right OptiClip toggle.\r\n'
 'Compact upscaler status table, flush NR selector outlines and consistent gaps.\r\n'
 'Static Scale/Language labels, pointer-only hints and darker light-theme text.\r\n'
 'Official NeuRotic logo/wordmark replaces the upper-left generic header branding.\r\n'
 'Interface brightness ranges from 0.25x to 3.0x; default and reset stay 1.0x.\r\n'
 'Default menu scale Auto, dark theme, OptiClip off, Cinematic style and\r\n'
 'collapsed advanced NR features. Explicit existing settings are respected.\r\n'
 'General > Menu Theme and Color > Reduce motion settles transitions for this session.\r\n'
 'All seven pages, existing settings/save confirmations, live graphs and OptiClip remain.\r\n'
 'Preview/motion.gif is an offline component demonstration with example state.\r\n'
 'This exact binary has passed local build and UI checks; it needs a new game test.\r\n'
 'Earlier game acceptance belongs to earlier binaries.\r\n'
 'The matching flagship installer, dependencies and forwarder are preserved.\r\n'
 'The private NR model is not bundled.\r\n').encode('ascii')
preview=root/'docs/implementation-evidence/ui-reveal-spacing-20261002/preview'
for name in ('motion.gif','dark.png','light.png','waiting.png','heart.png'):
    files['Preview/'+name]=(preview/name).read_bytes()
files['support/PACKAGE-MANIFEST.json']=encoded({'kind':'neurotic-customer-candidate',
 'name':'NeuRotic Sleek UI review','version':'0.9.6','commit':commit,
 'files':[{'path':path.replace('/','\\'),'bytes':len(raw),'sha256':sha(raw)} for path,raw in sorted(files.items())]})
destination=root.parent/f'_exports/NeuRotic-Sleek-UI-{commit[:8]}-review.zip'
assert not destination.exists()
with zipfile.ZipFile(destination,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as archive:
    for path,raw in sorted(files.items()):
        archive.writestr(destination.stem+'/'+path,raw)
with zipfile.ZipFile(destination) as archive:
    assert archive.testzip() is None
    prefix=destination.stem+'/'
    final=json.loads(archive.read(prefix+'support/PACKAGE-MANIFEST.json'))
    for row in final['files']:
        raw=archive.read(prefix+row['path'].replace('\\','/'))
        assert len(raw)==row['bytes'] and sha(raw)==row['sha256']
    for path,hash_value in installer_hashes.items(): assert sha(archive.read(prefix+path))==hash_value
result={'package':str(destination),'sha256':sha(destination.read_bytes()),'bytes':destination.stat().st_size,
 'source_commit':commit,'baseline_package':str(base),'baseline_sha256':sha(base.read_bytes()),
 'product_sha256':sha(product),'forwarder_sha256':sha(files['payload/nvngx.dll_dlssnr.dll']),
 'installer_sha256':installer_hashes,'manifest_verified':True,'game_runtime_verified':False}
(root/'builds/ui-release/sleek/package-receipt.json').write_bytes(encoded(result))
print(json.dumps(result,indent=2))
