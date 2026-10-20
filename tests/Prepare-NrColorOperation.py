"""Prepare committed/current actual color constant assignments; does not compile/run."""
from pathlib import Path
import subprocess,hashlib,json,re,sys
root=Path(__file__).resolve().parents[1];out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
anchor='e010503f529d317a7c062072633acb6c894cdf73'
def original(path):return subprocess.check_output(['git','--no-optional-locks','show',anchor+':'+path],cwd=root).decode('utf-8')
paths=['OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp','OptiScaler/dlssnr/DlssNrFeature_Vk.cpp']
fields=['Passthrough','WhitePoint','SignedPresent','UseGameExposure','ExposurePreMul','ReversibleMode']
def unique(pattern,source):
    found=re.findall(pattern,source);assert len(found)==1,(pattern,len(found));return found[0]
def assignments(source,name,names):
    return '\n'.join(unique(r'\b'+name+r'\.'+field+r' = [^;]+;',source) for field in names)
generated=[]
for path,api in zip(paths,['Dx12','Vk']):
    for phase in ['Old','New']:
        source=original(path) if phase=='Old' else (root/path).read_text(encoding='utf-8')
        if api=='Dx12':
            if phase=='Old':
                code=assignments(source,'encodeParams',fields)+'\n'+assignments(source,'resolveParams',fields)
            else:
                code=unique(r'const Neurotic::Context::ModelColorOperation modelColorOperation \{[\s\S]*?\};',source)+'\n'
                for name in ['encodeParams','resolveParams']:
                    code+=unique(r'Neurotic::Context::ApplyModelColor\(modelColorOperation, '+name+r'\);',source)+'\n'
            code+='\nDlssNrConstants layer2Encode=encodeParams,layer2Resolve=resolveParams,passEncode=encodeParams,passResolve=resolveParams;\n'
            for prefix,operation in [('layer2','layer2ColorOperation'),('pass','passColorOperation')]:
                if phase=='Old':
                    for name in [prefix+'Encode',prefix+'Resolve']:code+=assignments(source,name,['ReversibleMode'])+'\n'
                else:
                    code+=unique(r'const auto '+operation+r'=Neurotic::Context::WithReversibleMode\([^;]+;',source)+'\n'
                    for name in [prefix+'Encode',prefix+'Resolve']:
                        code+=unique(r'Neurotic::Context::ApplyModelColor\('+operation+', '+name+r'\);',source)+'\n'
            generated.append('''inline std::array<ColorStamp,6> ColorDx12%s(const DlssNrFrameInfo& frame,
 bool isHdrBuffer,float whitePoint,unsigned useGameExposure,float exposurePreMul,unsigned reversible,unsigned secondMode,unsigned passMode){
 ColorConfig cfg{{reversible},{secondMode}};struct {unsigned reversibleMode;} settings{passMode};
 DlssNrConstants encodeParams{},resolveParams{};
 %s
 return {Stamp(encodeParams),Stamp(resolveParams),Stamp(layer2Encode),Stamp(layer2Resolve),Stamp(passEncode),Stamp(passResolve)};
}'''%(phase,code))
        else:
            if phase=='Old':code=assignments(source,'encode',['SignedPresent','WhitePoint','Passthrough','ReversibleMode'])
            else:
                code=unique(r'const auto selectedPresentColor=presentRecipe\.Operation\(\);',source)+'\n'
                code+=unique(r'const Neurotic::Context::ModelColorOperation modelColorOperation \{[\s\S]*?\};',source)+'\n'
                code+=unique(r'Neurotic::Context::ApplyModelColor\(modelColorOperation, encode\);',source)
            generated.append('''inline ColorStamp ColorVk%s(DlssNr::VkNrRoute route,const DlssNr::VkPresentColorRecipe& presentRecipe,
 bool linearHdr,float whitePoint,unsigned reversible){
 struct {DlssNr::VkNrRoute route;} request{route};ColorConfig cfg{{reversible},{0}};DlssNrConstants encode{};
 %s
 return Stamp(encode);
}'''%(phase,code))
target=out/'color_operation_calls.inc';target.write_text('\n'.join(generated)+'\n',encoding='utf-8')
receipt=dict(anchor=anchor,method='Exact old/current six resolved color fields and current shared-operation construction/application from actual Native encode/resolve plus second/later modes; actual Vulkan assignments/default-zero fields. Public constant capture, not private runtime dispatch.',
 baseline=[dict(path=p,sha256=hashlib.sha256(original(p).encode()).hexdigest()) for p in paths],
 current=[dict(path=p,sha256=hashlib.sha256((root/p).read_bytes()).hexdigest()) for p in paths],
 generated=dict(path=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(out/'PREPARATION.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8');print('Prepared actual color constant differential; no native execution.')
