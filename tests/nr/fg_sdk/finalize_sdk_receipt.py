"""Freeze this lane's verified source/build receipt and remove its inactive objects."""
from pathlib import Path
import json,hashlib,datetime,os,re
ROOT=Path(__file__).resolve().parents[3]
OUT=ROOT.parent/'builds/w03-fg-sdk-closure'
BUILD=OUT/'full-v1'
def files(base):
 for directory,dirs,names in os.walk(base,followlinks=False):
  dirs[:]=[d for d in dirs if not (Path(directory)/d).is_symlink() and not os.path.isjunction(Path(directory)/d)]
  for name in names:
   p=Path(directory)/name
   if not p.is_symlink():yield p
def size(base):return sum(p.stat().st_size for p in files(base))
def item(p):return {'path':str(p),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()}
supervision=json.loads((OUT/'full-v1-build-supervision.json').read_text())
assert supervision['numeric_exit_code']==0 and supervision['group_empty_observed'] and supervision['capture_complete'] and not supervision['errors'] and not supervision['cleanup_errors']
before=size(BUILD);removed=[]
for p in files(BUILD):
 if p.suffix.lower() in {'.obj','.pch','.ilk','.ipdb','.iobj'}:
  assert p.resolve().is_relative_to(BUILD.resolve())
  removed.append({'path':str(p),'bytes':p.stat().st_size});p.unlink()
cleanup={'before_bytes':before,'after_bytes':size(BUILD),'removed_bytes':sum(p['bytes'] for p in removed),'removed':removed,'scope':'Completed SDK-owned full-v1 objects only; no junction traversal; final binaries and shader cache retained'}
if not removed and (OUT/'sdk-storage-cleanup.json').exists():
 cleanup=json.loads((OUT/'sdk-storage-cleanup.json').read_text())
(OUT/'sdk-storage-cleanup.json').write_text(json.dumps(cleanup,indent=2)+'\n')
sources=[]
for base in [ROOT/'external/FidelityFX-CheckedClosure',ROOT/'tests/nr/fg_sdk']:
 sources.extend(p for p in files(base) if p.suffix in {'.h','.cpp','.py','.md','.cmake'} or p.name=='CMakeLists.txt')
for sub in ['sdk/include','sdk/src','ffx-api/include','ffx-api/src']:
 sources.extend(p for p in files(ROOT/'external/FidelityFX-SDK'/sub) if p.suffix in {'.h','.cpp','.c','.hlsl','.hlsli','.cmake'} or p.name=='CMakeLists.txt')
v2=ROOT/'external/FidelityFX-SDK-v2/Kits/FidelityFX'
sources.extend([v2/'framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12_Helpers.h',v2/'framegeneration/fsr3/dx12/FrameInterpolationSwapchainDX12.cpp',v2/'framegeneration/fsr3/dx12/ffx_provider_fsr3framegenerationswapchain_dx12.cpp'])
assert all(p.exists() for p in sources)
build=json.loads((OUT/'full-v1-build-receipt.json').read_text())
counts=[int(re.search(r'(\d+) checks, 0 failures',(OUT/f'swapchain-run-{i}.log').read_text()).group(1)) for i in range(7)]
assert counts==[59,57,58,59,60,86,63]
for name,count in [('owned-output-final-run.log',184),('final-owned-helpers-v1-run.log',36),('final-owned-helpers-v2-run.log',19),('final-owned-worker-transition-run.log',37)]:
 assert f'{count} checks, 0 failures' in (OUT/name).read_text(),name
initial=(OUT/'full-v1-initial-build.log').read_text()
permutations=[int(n) for n in re.findall(r'Processed (\d+) shader permutations',initial)]
receipt={'utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'status':'EXPERIMENTAL_FULL_V1_BUILD_PUBLIC_QUERIES_UNSUPPORTED','cpu_checks':184,'helper_checks':{'v1':36,'v2':19},'worker_event_checks':37,'v1_sdk_warp_checks':counts,'total_checks':184+36+19+37+sum(counts),'full_module_load_or_execution':False,'physical_provider_game_execution':False,'coverage_flags':0,'full_build':build,'shader_compiler_invocations':len(permutations),'processed_permutations_including_duplicates':sum(permutations),'storage':cleanup,'sources':[item(p) for p in sorted(set(sources))],'evidence':[]}
for p in sorted(OUT.iterdir()):
 if p.is_file() and p.name not in {'sdk-final-source-receipt.json','SDK-FINAL-HANDOFF.md'} and p.suffix in {'.log','.json','.yaml','.exe'}:receipt['evidence'].append(item(p))
for p in sorted((BUILD/'bin').iterdir()):
 if p.is_file():receipt['evidence'].append(item(p))
(OUT/'sdk-final-source-receipt.json').write_text(json.dumps(receipt,indent=2)+'\n')
print(json.dumps({'sources':len(receipt['sources']),'evidence':len(receipt['evidence']),'checks':receipt['total_checks'],'shader_commands':len(permutations),'processed_permutations':sum(permutations),'before_bytes':before,'after_bytes':cleanup['after_bytes'],'removed_bytes':cleanup['removed_bytes'],'receipt':item(OUT/'sdk-final-source-receipt.json')},indent=2))
