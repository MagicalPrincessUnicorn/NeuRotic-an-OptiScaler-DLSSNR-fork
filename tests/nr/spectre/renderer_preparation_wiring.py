"""Bounded preparation attachment audit, not a model/runtime qualification."""
import pathlib,re
repo=pathlib.Path(__file__).resolve().parents[3]
source=(repo/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
header=repo/'OptiScaler/dlssnr/NativeRendererPreparation.h'
marker='NativeRendererPreparation::TryAcquire('
checks=[]
if marker not in source or not header.exists():
    checks.append((False,'missing renderer preparation-only entry'))
else:
    body=source[source.index(marker):source.index('// The pass. Resources in, nothing read',source.index(marker))]
    interface=header.read_text()
    checks.extend([
        ('std::unique_ptr<DlssNr::NativeModelCreation> nativeCreation' in source,'actual model owner retains creation recording'),
        ('std::unique_lock<std::recursive_mutex> lifecycle' in source and 'std::unique_lock<std::mutex> model' in source,'preparation retains lifecycle and model exclusion'),
        ('BeginLocalAction(' in body,'actual recording action required'),
        ('nativeCreation->TryCreate(' in body and 'CreateNrFeature(' in body,'uses real one-attempt creation boundary'),
        ('nativeCreation->ReadyFor(' in body,'observed initialization completion required'),
        ('TuningMatchesFeature(settings)' in body and 'RequiresRetirement(preSr)' in body,'current feature meaning is checked'),
        ('scratch.Prepare(' in body and 'scratch.Commit(ParkNrResource)' in body,'real transactional allocation uses existing retirement'),
        (not re.search(r'->Dispatch\(|\.evaluate\w*\s*\(|EvaluateNativeProtocol|EvaluateAfterUpscale',body),'preparation cannot enter a model evaluation'),
        ('DirectD3D12AvailableLocked(' in body,'actual runtime capability checked'),
        ('private:' in interface and 'friend class Neurotic::Lifecycle::NativeProcessBootstrap;' in interface,'factory stays under real process bootstrap'),
        ('NativePreparationStatus::Ready' in body and 'action->Current()' in body,'readiness needs live recording guard'),
    ])
for okay,name in checks:
    if not okay:print('FAIL '+name)
print(f'Renderer preparation wiring: {len(checks)} structural checks, {sum(not x for x,_ in checks)} failures; runtime NOT_RUN')
raise SystemExit(0 if all(x for x,_ in checks) else 1)
