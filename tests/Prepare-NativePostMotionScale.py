"""Extract actual D3D12 motion-factor call sites for the public CPU regression test.

Usage: python tests/Prepare-NativePostMotionScale.py OUTPUT_DIRECTORY
Then run tests/Run-NativePostMotionScale.cmd OUTPUT_DIRECTORY.
No compiler, provider, model, game or GPU is started by this preparation step.
"""
from pathlib import Path
import hashlib,json,re,sys

root=Path(__file__).resolve().parents[1]
out=Path(sys.argv[1]).resolve()
out.mkdir(parents=True,exist_ok=True)
source=root/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp'
text=source.read_text(encoding='utf-8-sig')
policy=re.findall(r'const bool preserveNativePostMotionScale\s*=\s*[^;]+;',text)
if len(policy)!=1:
    raise RuntimeError('Expected one actual Native Post-SR motion policy declaration')
parts=[]
for name in ['mvToWork','mvToLayer2Work','mvToPassWork']:
    declarations=re.findall(r'const auto '+name+r'\s*=\s*[^;]+;',text)
    if len(declarations)!=1:
        raise RuntimeError('Actual motion-factor declaration missing or ambiguous: '+name)
    parts.append('''DlssNrMotionScale %s(const DlssNrFrameInfo& frame, bool currentRouteIsPreSr,
 bool imageOnlyDomain, unsigned width, unsigned height, unsigned workWidth, unsigned workHeight,
 unsigned layer2WorkWidth, unsigned layer2WorkHeight, unsigned passWorkWidth, unsigned passWorkHeight) {
 %s
 %s
 return %s;
}'''%(name+'Actual',policy[0],declarations[0],name))
generated=out/'native_post_motion_calls.inc'
generated.write_text('\n'.join(parts)+'\n',encoding='utf-8')
paths=[source,root/'OptiScaler/shaders/dlssnr/DlssNr_Common.h',
       root/'tests/nr_native_post_motion_scale.cpp',Path(__file__),generated]
manifest={'stage':'prepared-only','inputs':[{'path':str(p),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in paths],
          'qualification':'Actual production scalar policy and three pass declarations; CPU fixture does not establish provider consumption, runtime eligibility, GPU behavior or motion quality.'}
(out/'SOURCE.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
print(json.dumps({'manifest':str(out/'SOURCE.json'),'sha256':hashlib.sha256((out/'SOURCE.json').read_bytes()).hexdigest()}))
