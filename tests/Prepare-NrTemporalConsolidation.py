from pathlib import Path
import subprocess,hashlib,json,re,sys
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
anchor='1e7d8994df7dafeaa4eabd3a30ee8a0db6e1ade4'
def original(path):
    return subprocess.check_output(['git','--no-optional-locks','show',anchor+':'+path],cwd=root).decode('utf-8')
def function(source,name):
    at=source.index(name+'(');start=source.rfind('inline ',0,at);opening=source.index('{',at);level=1;end=opening+1
    while level:
        if source[end]=='{':level+=1
        if source[end]=='}':level-=1
        end+=1
    return source[start:end]
common=original('OptiScaler/shaders/dlssnr/DlssNr_Common.h')
vk=original('OptiScaler/dlssnr/VulkanNrFrameContract.h')
names=['DlssNrWorkingMotionScale','DlssNrProjectPresentGuides','DlssNrReducedMotionRaster',
       'DlssNrPreserveNativePostMotionScale','DlssNrModelMotionFactor','DlssNrModelJitter']
body='\n\n'.join(function(common,n) for n in names)
for n in names:body=body.replace(n,'Baseline_'+n)
body+='\nnamespace DlssNr {\n'+function(vk,'ScaleVkNrTemporal').replace('ScaleVkNrTemporal','Baseline_ScaleVkNrTemporal')+'\n}\n'
header=out/'temporal_baseline.inc';header.write_text(body,encoding='utf-8')
old_cpp=original('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
new_cpp=(root/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')
def provider_calls(source):
    region=source[source.index('    // NR-NATIVE-POST-MOTION-TRACE END'):source.index('    // NR-NATIVE-POST-MOTION-ADDITIONAL-RECEIPT END')]
    result=[]
    for match in re.finditer(r'(g_nr\.evaluate(?:GuidesV4|GuidesV2|Guided)?|DlssNr::Proxy::Run)\s*\(',region):
        begin=match.end();pos=begin;level=1
        while level:
            if region[pos]=='(':level+=1
            if region[pos]==')':level-=1
            pos+=1
        call=region[begin:pos-1]
        scalar=re.search(r'(?P<x>(?:g_nr\.guideMvScaleX\s*\*\s*mvTo\w+\.x|temporalTo\w+\.values\.motionScale\.x))\s*,\s*'
                         r'(?P<y>(?:g_nr\.guideMvScaleY\s*\*\s*mvTo\w+\.y|temporalTo\w+\.values\.motionScale\.y))\s*,\s*'
                         r'(?P<jx>jitterTo\w+\.x)\s*,\s*(?P<jy>jitterTo\w+\.y)',call)
        assert scalar,match.group(1)
        result.append((match.group(1),','.join(scalar.group(n) for n in ['x','y','jx','jy'])))
    assert len(result)==9,len(result)
    return result
before=provider_calls(old_cpp);after=provider_calls(new_cpp)
live=['// Exact final four temporal argument expressions from all nine live D3D12 provider call sites.']
for index,((old_name,old_args),(new_name,new_args)) in enumerate(zip(before,after)):
    assert old_name==new_name
    suffix=re.search(r'(?:mvTo|temporalTo)(\w+)\.',new_args).group(1)
    declarations=[]
    for source,kind in [(old_cpp,'Old'),(new_cpp,'New')]:
        chosen=[]
        for variable in ['mvTo'+suffix]+(['temporalTo'+suffix] if kind=='New' else [])+['jitterTo'+suffix]:
            found=re.findall(r'const auto '+variable+r'\s*=\s*[^;]+;',source)
            assert len(found)==1,(kind,variable)
            chosen.append(found[0])
        code='\n'.join(chosen)
        if kind=='Old':
            for name in names:code=code.replace(name,'Baseline_'+name)
        args=old_args if kind=='Old' else new_args
        live.append('''inline std::array<float,4> TemporalCall%d%s(const DlssNrFrameInfo& frame,
 bool preserveNativePostMotionScale,unsigned width,unsigned height,unsigned workWidth,unsigned workHeight){
 [[maybe_unused]] const auto layer2WorkWidth=workWidth;
 [[maybe_unused]] const auto layer2WorkHeight=workHeight;
 [[maybe_unused]] const auto passWorkWidth=workWidth;
 [[maybe_unused]] const auto passWorkHeight=workHeight;
 struct {float guideMvScaleX,guideMvScaleY;} g_nr{frame.MvScaleX,frame.MvScaleY};
 %s
 return {%s};
}'''%(index,kind,code,args))
live.append('using TemporalCapture=std::array<float,4>(*)(const DlssNrFrameInfo&,bool,unsigned,unsigned,unsigned,unsigned);')
for kind in ['Old','New']:
    live.append('inline const std::array<TemporalCapture,9> TemporalCalls'+kind+'={'+','.join('&TemporalCall'+str(i)+kind for i in range(9))+'};')
calls=out/'temporal_live_calls.inc';calls.write_text('\n'.join(live)+'\n',encoding='utf-8')
receipt=dict(anchor=anchor,method='Exact function bodies extracted from committed source via git show; only function names prefixed, existing unchanged frame types shared. No guessed reference math.',
 baseline_sources=[dict(path=p,sha256=hashlib.sha256(original(p).encode()).hexdigest()) for p in ['OptiScaler/shaders/dlssnr/DlssNr_Common.h','OptiScaler/dlssnr/VulkanNrFrameContract.h','OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp']],
 generated=[dict(path=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest()) for p in [header,calls]],
 final_call_witness='Exact four temporal scalar expressions and required actual declarations extracted from nine committed/current D3D12 provider calls: V4 observer, Proxy Run, V2, guided, legacy and both second/later alternatives. Compiled public scalar capture, not full private ABI/resource dispatch or branch scheduling.',
 actual_cpp=dict(path='OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp',sha256=hashlib.sha256((root/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_bytes()).hexdigest()))
(out/'BASELINE-PREPARATION-03.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('Prepared exact committed temporal baseline; no native execution.')
