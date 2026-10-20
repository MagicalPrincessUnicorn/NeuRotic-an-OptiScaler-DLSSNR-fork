"""Check the actual new integration seams, not the legacy renderer's whole call graph."""
import re
from pathlib import Path
root=Path(__file__).resolve().parents[1]
hook=(root/'OptiScaler/hooks/D3D12_Hooks.cpp').read_text(encoding='utf-8-sig')
cold=(root/'OptiScaler/dllmain.cpp').read_text(encoding='utf-8-sig')
project=(root/'OptiScaler/OptiScaler.vcxproj').read_text(encoding='utf-8-sig')
assert hook.count('CandidateObserver::CreationScope')==2,'both existing creation hooks must observe'
assert hook.count('CandidateObserver::CreateValue(')==4,'normal and alternate returns must be represented'
assert 'if (wantSpoof || wantScan)' in hook and 'wantObserver' not in hook,'attachment policy must remain unchanged'
assert hook.count('CandidateObserver::ObserveCoverage(')>=3,'actual attach and both detach paths need receipts'
assert 'CandidateObserver::StartCold(_passThruMode)' in cold,'initialize only from cold worker'
assert 'CandidateObserver::RequestClose' in cold,'DllMain close request must be atomic only'
assert 'nr\\diagnostics\\candidate\\Observer.cpp' in project,'product build must include actual worker'
for p in (root/'OptiScaler/nr/diagnostics/candidate').glob('*'):
    text=p.read_text()
    for method in ('AddRef','Release','QueryInterface','GetDesc','GetDevice','Map','GetPrivateData'):
        assert not re.search(r'(?:->|\.)\s*'+method+r'\s*\(',text),(p.name,method)
    for forbidden in ('Config.h','StreamCoordinatorKernel.h','GpuSafety.h','ResTrack_Dx12.h'):
        assert forbidden not in text,(p.name,forbidden)
assert 'FreeLibraryAndExitThread(module,0)' in (root/'OptiScaler/nr/diagnostics/candidate/Observer.cpp').read_text()
print('actual producer/cold-start/ownership wiring contract passed')
