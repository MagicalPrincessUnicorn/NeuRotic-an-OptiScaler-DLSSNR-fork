"""Extract the actual current modern setArch body for one public legacy-control RED."""
from pathlib import Path
import sys,re,hashlib,json
root=Path(__file__).resolve().parents[3];out=Path(sys.argv[1]).resolve();out.mkdir(parents=True,exist_ok=True)
p=root/'OptiScaler/hooks/Streamline_Hooks.cpp';source=p.read_text(encoding='utf-8')
start=source.index('void StreamlineHooks::setArch(')
begin=source.index('        auto caps =',start);opening=source.index('        if (caps)',begin)
brace=source.index('{',opening);end=brace+1;depth=1
while depth:
    if source[end]=='{':depth+=1
    if source[end]=='}':depth-=1
    end+=1
body=source[begin:end]
body=re.sub(r'auto caps = [^;]+;','auto caps = value;',body,count=1)
body=body.replace('fakenvapi::isUsingAsMainNvapi() || primaryGpu.vendorId != VendorId::Nvidia','fakeOrNonNvidia')
assert 'caps->adapters[i].architecture = arch;' in body and 'caps->adapters[i].vendor = VendorId::Nvidia;' in body
assert 'caps->driverVersionMajor = 999;' in body and 'caps->hwsSupported = true;' in body
target=out/'legacy_caps_set_arch.inc'
target.write_text('inline void LegacySetArch(Caps* value,std::uint32_t arch,bool fakeOrNonNvidia){\n'+body+'\n}\n',encoding='utf-8')
receipt=dict(source=str(p),sha256=hashlib.sha256(p.read_bytes()).hexdigest(),
 method='Actual modern setArch body with pointer selection and already-resolved fake/nonNvidia condition adapted to authored fixture arguments. No replacement restoration algorithm.',
 generated=dict(path=str(target),sha256=hashlib.sha256(target.read_bytes()).hexdigest()))
(out/'LEGACY-PREPARATION.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
print('Prepared actual legacy setArch control, no native execution.')
