"""Compose the user's requested full copy from two sealed, verified deliveries."""
from pathlib import Path
import argparse, hashlib, json, subprocess, zipfile

ROOT = Path(__file__).resolve().parents[2]
parser=argparse.ArgumentParser()
parser.add_argument('--resolution',action='store_true')
parser.add_argument('--scheduling',action='store_true')
parser.add_argument('--product-commit',default='99986ad7ac7eb7d28e9d64dfd5f5532f935d229c')
args=parser.parse_args()
args.resolution=args.resolution or args.scheduling
product=args.product_commit
assert len(product)==40 and all(c in '0123456789abcdef' for c in product)
EVIDENCE = ROOT/('docs/implementation-evidence/anything-scheduling-full-20261003' if args.scheduling else 'docs/implementation-evidence/anything-resolution-full-20261003' if args.resolution else 'docs/implementation-evidence/anything-full-20261003')
BASE = Path('C:/NeuRotic/_exports/NeuRotic-Current-Flagship-4134edeb.zip')
DEST = Path('C:/NeuRotic/_exports')/(f'NeuRotic-Full-Anything-Scheduling-{product[:8]}-20261003.zip' if args.scheduling else f'NeuRotic-Full-Anything-Resolution-{product[:8]}-20261003.zip' if args.resolution else 'NeuRotic-Full-Anything-99986ad7-20261003.zip')
sha = lambda data: hashlib.sha256(data).hexdigest()
encode = lambda value: (json.dumps(value, indent=2)+'\n').encode('utf-8')
update_evidence=ROOT/('docs/implementation-evidence/anything-scheduling-20261003' if args.scheduling else 'docs/implementation-evidence/anything-resolution-20261003' if args.resolution else 'docs/implementation-evidence/anything-throughput-20261003')
receipt = json.loads((update_evidence/'update-receipt.json').read_text())
assert receipt['sealed']
assert sha(BASE.read_bytes()) == receipt['manifest']['baselineArchiveSha256']
UPDATE = Path(receipt['path'])
assert sha(UPDATE.read_bytes()) == receipt['sha256']
assert not DEST.exists(), 'Preserve previously delivered full copies'
EVIDENCE.mkdir(parents=True, exist_ok=True)
assert subprocess.check_output(['git','rev-parse','HEAD'],cwd=ROOT,text=True).strip() == product

with zipfile.ZipFile(BASE) as baseline, zipfile.ZipFile(UPDATE) as patch:
    assert baseline.testzip() is None and patch.testzip() is None
    base_names = baseline.namelist()
    assert len(base_names) == len(set(base_names))
    prefix = 'NeuRotic/'
    assert all(n.startswith(prefix) and ':' not in n and '..' not in Path(n).parts for n in base_names)
    replacements = {prefix+entry['path']:patch.read(entry['path']) for entry in receipt['manifest']['files']}
    for entry in receipt['manifest']['files']:
        assert sha(baseline.read(prefix+entry['path'])) == entry['baselineSha256']
        assert sha(replacements[prefix+entry['path']]) == entry['sha256']
    for name in patch.namelist():
        if name.startswith('Source-Changes/'):
            relative = name.removeprefix('Source-Changes/')
            replacements[prefix+'Source/Anything-Changes/'+relative] = patch.read(name)
            # Keep the baseline's existing Hub source copies consistent too.
            existing = prefix+'Source/Hub/'+relative
            if existing in base_names:
                replacements[existing] = patch.read(name)
    for name in ['UPDATE-MANIFEST.json','Trace-Anything.ps1']:
        replacements[prefix+name] = patch.read(name)
    replacements[prefix+'Trace-Anything.cmd'] = b'@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Trace-Anything.ps1" -InstallRoot "%~dp0."\r\npause\r\n'
    instructions = '''NEUROTIC - FULL COPY WITH THE ANYTHING THROUGHPUT UPDATE

Extract the entire ZIP to a new folder and keep its folders together.
Open NeuRotic/NeuRotic.Hub.exe. The Anything update is already applied.
No separate update ZIP or patching step is required.
Choose your own DLSS NR file in the Anything tab; it is not included.
To record a test, close this App and run Trace-Anything.cmd beside it.

This is the complete 4134edeb flagship plus the verified Anything update
from 99986ad7: seamless overlay controls, performance measurements,
GPU neutral-input clearing and event-based capture/presentation waits.
It includes the original game installer, discovery runtime, Inspector
assets and Vulkan/game payload. Other experimental branches are not merged.
NeuRotic-Setup.cmd remains available for game installation.

Actual RTX FPS improvement and the reported visible comparison issue
still need runtime testing. Resolution scaling and audio are unchanged.
A fresh extraction does not copy preferences from an older App folder.
The private NR model remains yours; selecting its existing path is sufficient.

ARCHIVE-MANIFEST.json describes this composed package. Baseline component
receipts under provenance/baseline-* are historical, not updated build claims.
Source/Anything-Changes contains the update's source overlay for the baseline.
'''
    if args.resolution:
        instructions=instructions.replace('ANYTHING THROUGHPUT UPDATE','ANYTHING NR RESOLUTION CONTROLS').replace('from 99986ad7:',f'from {product[:8]}:')
        instructions=instructions.replace('Choose your own DLSS NR file', 'Choose NR resolution before Start: Full 100%, Quality 75%, Balanced 67%,\nPerformance 50%. Balanced is the default; output stays full size.\nThis is existing Present-style reconstruction, not DLSS SR or FSR.\nChoose your own DLSS NR file')
        instructions=instructions.replace('Resolution scaling and audio are unchanged.', 'Reduced-resolution NR quality/speed still need your model test.\nFocus persistence, snapshots, FPS pacing, selectable upscalers and multipass\nare recorded follow-ups; audio and those behaviors are unchanged.')
    if args.scheduling:
        instructions=instructions.replace('ANYTHING NR RESOLUTION CONTROLS','ANYTHING EVENT-DRIVEN SCHEDULING')
        instructions+='\nThis build removes the unconditional post-frame sleep. Capture, commands and\nwindow messages wake the worker. Trace includes wait and completion intervals.\nActual GPU throughput improvements require your next run.\n'
    for name in ['START HERE.txt','READ ME.txt']:
        replacements[prefix+name] = instructions.replace('\n','\r\n').encode('utf-8')
    for old, historical in [('ARCHIVE-MANIFEST.json','provenance/baseline-archive-manifest.json'),
                            ('verification/current-components.json','provenance/baseline-current-components.json')]:
        replacements[prefix+historical] = baseline.read(prefix+old)
    composition = {'role':'Full flagship copy with Anything event-driven scheduling' if args.scheduling else 'Full flagship copy with Anything resolution controls' if args.resolution else 'User-requested full flagship copy with Anything throughput update',
        'baselineArchive':BASE.name,'baselineSha256':sha(BASE.read_bytes()),
        'anythingUpdate':UPDATE.name,'anythingUpdateSha256':receipt['sha256'],
        'anythingProductCommit':product,'updatedRuntimeFiles':receipt['manifest']['files'],
        'gamePayloadChanged':False,'otherExperimentalBranchesMerged':False,
        'actualRTXThroughputAfterPatch':'NOT_RUN','visibleComparisonIssue':'OPEN',
        'baselineComponentHistory':'provenance/baseline-current-components.json'}
    replacements[prefix+'verification/current-components.json'] = encode(composition)
    replacements[prefix+('verification/anything-scheduling.json' if args.scheduling else 'verification/anything-resolution.json' if args.resolution else 'verification/anything-throughput.json')] = (update_evidence/'verification.json').read_bytes()
    inventory = []
    names = (set(base_names) | set(replacements)) - {prefix+'ARCHIVE-MANIFEST.json'}
    with zipfile.ZipFile(DEST,'x',zipfile.ZIP_DEFLATED,compresslevel=6) as output:
        for name in sorted(names):
            data = replacements[name] if name in replacements else baseline.read(name)
            output.writestr(name,data)
            inventory.append({'path':name.removeprefix(prefix),'bytes':len(data),'sha256':sha(data)})
        manifest = {'schemaVersion':1,'composition':composition,'game_qualified':False,'files':inventory}
        output.writestr(prefix+'ARCHIVE-MANIFEST.json',encode(manifest))
    with zipfile.ZipFile(DEST) as check:
        assert check.testzip() is None
        for entry in inventory:
            assert sha(check.read(prefix+entry['path'])) == entry['sha256']
        unchanged = 0
        for name in base_names:
            if name not in replacements and name != prefix+'ARCHIVE-MANIFEST.json':
                assert check.read(name) == baseline.read(name)
                unchanged += 1
        # This is the separate game forwarder; never replace it with the worker's.
        assert check.read(prefix+'packages/current/payload/nvngx.dll_dlssnr.dll') == baseline.read(prefix+'packages/current/payload/nvngx.dll_dlssnr.dll')
    result = {'path':str(DEST),'bytes':DEST.stat().st_size,'sha256':sha(DEST.read_bytes()),
        'members':len(inventory)+1,'allMemberHashesVerified':True,'unchangedBaselineMembers':unchanged,
        'composition':composition,'compilerBuildPerformed':False,'sealed':True}
    (EVIDENCE/'receipt.json').write_bytes(encode(result))
    print(json.dumps({k:result[k] for k in ['path','bytes','sha256','members','unchangedBaselineMembers']},indent=2))
