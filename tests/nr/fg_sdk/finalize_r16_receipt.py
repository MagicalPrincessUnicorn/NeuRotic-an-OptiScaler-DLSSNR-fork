"""Freeze R16 SDK evidence separately from the historical pre-algorithm receipt."""
from pathlib import Path
import datetime, hashlib, json, os, re
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
BUILD=OUT/'full-v1'
def files(base):
    for directory,dirs,names in os.walk(base,followlinks=False):
        dirs[:]=[d for d in dirs if not (Path(directory)/d).is_symlink() and not os.path.isjunction(Path(directory)/d)]
        for name in names:
            p=Path(directory)/name
            if not p.is_symlink(): yield p
def item(p): return dict(path=str(p),bytes=p.stat().st_size,sha256=hashlib.sha256(p.read_bytes()).hexdigest())
def total(base): return sum(p.stat().st_size for p in files(base))
supervision=json.loads((OUT/'full-v1-build-supervision.json').read_text())
assert supervision['numeric_exit_code']==0 and supervision['group_empty_observed'] and not supervision['timed_out']
assert not supervision['errors'] and not supervision['cleanup_errors']
probe=json.loads((OUT/'manual-fg-probe-build-receipt.json').read_text())
assert probe['build_exit']==0 and not probe['worker_executed']
counts=[int(re.search(r'(\d+) checks, 0 failures',(OUT/f'swapchain-run-{i}.log').read_text()).group(1)) for i in range(7)]
assert counts==[59,57,58,59,60,118,63]
for name,count in [('algorithm-final',184),('algorithm-owner-final',34),('cleanup-final',15),('r16-fixed-helpers-v1',36),('r16-fixed-helpers-v2',19),('r16-fixed-worker-transition',37)]:
    assert f'{count} checks, 0 failures' in (OUT/(name+'-run.log')).read_text(),name
before=total(BUILD);removed=[]
for p in files(BUILD):
    if p.suffix.lower() in {'.obj','.pch','.ilk','.ipdb','.iobj'}:
        assert p.resolve().is_relative_to(BUILD.resolve())
        removed.append(dict(path=str(p),bytes=p.stat().st_size));p.unlink()
storage=dict(before_bytes=before,after_bytes=total(BUILD),removed_bytes=sum(p['bytes'] for p in removed),removed=removed,scope='Inactive SDK full-v1 intermediates only; no junction traversal; sources, binaries, symbols, libraries, shaders, logs preserved')
(OUT/'sdk-r16-storage-cleanup.json').write_text(json.dumps(storage,indent=2)+'\n')
sources=[]
for base in [ROOT/'external/FidelityFX-CheckedClosure',ROOT/'tests/nr/fg_sdk']:
    sources.extend(p for p in files(base) if p.suffix in {'.h','.cpp','.py','.md','.cmake'} or p.name=='CMakeLists.txt')
for sub in ['sdk/include','sdk/src','ffx-api/include','ffx-api/src']:
    sources.extend(p for p in files(ROOT/'external/FidelityFX-SDK'/sub) if p.suffix in {'.h','.cpp','.c','.hlsl','.hlsli','.cmake'} or p.name=='CMakeLists.txt')
v2=ROOT/'external/FidelityFX-SDK-v2/Kits/FidelityFX'
sources.extend([v2/'framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12_Helpers.h',v2/'framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.cpp',v2/'framegeneration/fsr3/dx12/ffx_provider_fsr3framegenerationswapchain_dx12.cpp'])
sources.extend(ROOT/'tools/nr/lab/w03_closure'/n for n in ['fg_probe.py','test_fg_probe.py'])
sources.append(ROOT/'tests/nr/spectre/owned_process.py')
exports=re.findall(r'^\s+\d+\s+[0-9A-F]+\s+[0-9A-F]+\s+(ffx\w+)',(OUT/'r16-exports.log').read_text(),re.M)
assert len(exports)==8 and 'ffxNeuRoticQueryExperimentalServiceV1' in exports
permutations=[int(n) for n in re.findall(r'Processed (\d+) shader permutations',(OUT/'full-v1-initial-build.log').read_text())]
artifacts=[p for p in OUT.iterdir() if p.is_file() and p.suffix in {'.log','.json','.yaml','.exe'} and p.name not in {'sdk-r16-source-receipt.json'}]
artifacts.extend(p for p in (BUILD/'bin').iterdir() if p.is_file())
receipt=dict(utc=datetime.datetime.now(datetime.timezone.utc).isoformat(),status='MANUAL_EXPERIMENTAL_CANDIDATE_PUBLIC_QUALIFICATION_UNSUPPORTED',total_cpp_checks=799,python_oracle_tests=11,swapchain_warp_checks=counts,full_algorithm_executed=False,physical_provider_game_executed=False,coverage_flags=0,qualification='C04_AND_STRICT_NOT_GRANTED',toolchain='SDK and manual probe MSVC14.51.36231 C++20 Release /MD; product14.44 dormant ABI unqualified',static_exports=exports,shader_commands=len(permutations),processed_permutations_including_duplicates=sum(permutations),build_supervision=supervision,manual_probe_build=probe,storage=storage,source_files=[item(p) for p in sorted(set(sources))],artifacts=[item(p) for p in sorted(set(artifacts))])
dest=OUT/'sdk-r16-source-receipt.json';dest.write_text(json.dumps(receipt,indent=2)+'\n')
for entry in receipt['source_files']+receipt['artifacts']:
    assert item(Path(entry['path']))==entry,entry['path']
print(json.dumps(dict(receipt=item(dest),sources=len(receipt['source_files']),artifacts=len(receipt['artifacts']),checks=799,storage={k:v for k,v in storage.items() if k!='removed'},dll=item(BUILD/'bin/amd_fidelityfx_dx12.dll'),probe=item(OUT/'W03FgProbe.exe')),indent=2))
