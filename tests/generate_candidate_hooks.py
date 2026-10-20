"""Compile the production callback bodies against a CPU-only host spy fixture."""
from pathlib import Path
root=Path(__file__).resolve().parents[1]
source=(root/'OptiScaler/hooks/D3D12_Hooks.cpp').read_text(encoding='utf-8')
parts=[]
for name in ('hkCreateCommittedResource','hkCreatePlacedResource'):
    start=source.index('static HRESULT '+name+'(')
    opening=source.index('{',start);depth=1;end=opening+1
    while depth:
        depth+=(source[end]=='{')-(source[end]=='}');end+=1
    parts.append(source[start:end])
out=root/'builds/candidate-observer/production-hooks.inc'
out.parent.mkdir(parents=True,exist_ok=True);out.write_text('\n'.join(parts),encoding='utf-8')
