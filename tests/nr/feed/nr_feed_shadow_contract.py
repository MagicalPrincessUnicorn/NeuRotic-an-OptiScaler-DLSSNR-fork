"""T10 additive source audit only. A game baseline/Shadow comparison remains NOT_RUN."""
import argparse,hashlib,json,pathlib,re,subprocess
ROOT=pathlib.Path(__file__).resolve().parents[3]
SOURCES=['OptiScaler/inputs/'+s+'.cpp' for s in ['NVNGX_DLSS_Dx11','NVNGX_DLSS_Dx12','NVNGX_DLSS_Vk',
 'FSR2_Dx11','FSR2_Dx12','FSR2_Vk','FSR3_Dx12','FfxApi_Dx12','FfxApi_Vk','FfxApiExe_Dx12',
 'XeSS_Common','XeSS_Dx11','XeSS_Dx12','XeSS_Vulkan']]
SOURCES+=['OptiScaler/hooks/Streamline_Hooks.cpp','OptiScaler/dlssnr/DlssNr_PresentGuides.h',
 'OptiScaler/dlssnr/DlssNr_Dx11.cpp','OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp']
def audit(baseline,git):
 results=[]
 for name in SOURCES:
  original=subprocess.check_output([git,'-C',str(ROOT),'show',baseline+':'+name])
  data=(ROOT/name).read_bytes()
  stripped,n=re.subn(rb'^[ \t]*// NR-FEED-001 BEGIN\r?\n.*?^[ \t]*// NR-FEED-001 END\r?\n',b'',data,flags=re.M|re.S)
  assert n>0 and stripped==original,('Legacy source changed',name)
  results.append({'path':name,'sha256':hashlib.sha256(data).hexdigest(),'additive_blocks':n,'original_sha256':hashlib.sha256(original).hexdigest()})
 adapters=[]
 for path in sorted((ROOT/'OptiScaler/inputs/universal_feeder/providers').glob('*.h')):
  body=path.read_text()
  code=re.sub(r'//[^\n]*','',body)
  assert not re.search(r'\b(CreateCommittedResource|CreateCommandQueue|CreateFence|CopyResource|CopyTextureRegion|Dispatch|ExecuteCommandLists|Signal|Wait|ResourceBarrier|AddRef|Release|Set|Save|DetourAttach|WaitForSingleObject)\s*\(',code),path
  assert not re.search(r'#include\s*[<"].*(?:renderer|shaders|Config|State|fallback|Feature_Dx)',code),path
  assert not re.search(r'\b(BuildCanonicalFrameContext|BeginConsumption|ValidateForSubmit)\s*\(',code),path
  adapters.append({'path':path.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(path.read_bytes()).hexdigest()})
 assert len(adapters)==6
 return {'work_order':'NR-FEED-001','baseline':baseline,'sources':results,'adapters':adapters,
  'sourceT10':'PASS_ADDITIVE_NO_FORBIDDEN_CALLS','runtimeT10':'NOT_RUN','gpuResource':'NOT_RUN','temporalImageQuality':'NOT_RUN','performanceLatency':'NOT_RUN'}
if __name__=='__main__':
 p=argparse.ArgumentParser();p.add_argument('--baseline',required=True);p.add_argument('--git',default='git');p.add_argument('--output',type=pathlib.Path,required=True);a=p.parse_args()
 result=audit(a.baseline,a.git);assert not a.output.exists();a.output.write_text(json.dumps(result,indent=2)+'\n');print(json.dumps({'sourceT10':result['sourceT10'],'runtimeT10':'NOT_RUN','evidence':str(a.output)}))
