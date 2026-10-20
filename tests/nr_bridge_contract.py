"""Source wiring only; behavioral value and WARP evidence live in C++ tests."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
def read(path): return (root / path).read_text()
bridge = read('OptiScaler/upscalers/IFeature_Dx11wDx12.cpp')
helper = read('OptiScaler/with_dx12/dx11_with_dx12.cpp')
fg = read('OptiScaler/inputs/FG/Upscaler_Inputs_Dx11wDx12.cpp')
owner = read('OptiScaler/framegen/IFGFeature_Dx12.cpp')
renderer = read('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
checks = 0
def check(value):
    global checks
    checks += 1
    assert value, f'bridge wiring check {checks}'
prepare = bridge[bridge.index('bool IFeature_Dx11wDx12::ProcessDx11Textures'):bridge.index('bool IFeature_Dx11wDx12::CopyBackOutput')]
check(prepare.index('WaitForGenerationReuse()') < prepare.index('slot.before={};'))
check(prepare.index('GpuSafety::Record') < prepare.index('PrepareUpscalerResources('))
check('UpscalerReadersReusable()' in prepare and 'MarkConsumer' in prepare and 'BindUpscalerUse(slot.use' in prepare)
release = bridge[bridge.index('void IFeature_Dx11wDx12::ReleaseSharedResources'):bridge.index('bool IFeature_Dx11wDx12::ProcessDx11Textures')]
check(release.index('GenerationReusable()') < release.index('SAFE_RELEASE'))
check('QuarantineUpscalerCache();' in bridge and '(void)dx12Feature.release();' in bridge)
check('WaitForSingleObject(Dx12FenceEvent, INFINITE)' not in bridge)
evaluation = bridge[bridge.index('bool IFeature_Dx11wDx12::Evaluate'):bridge.index('bool IFeature_Dx11wDx12::BaseInit')]
check(evaluation.index('LockBridgeLifecycle()') < evaluation.index('LockUpscalerResources()'))
check('BridgeTelemetry().RecordNr' not in evaluation)
check('CurrentIdentity(frame,DlssNr::BridgeLifecycleGeneration())' in bridge)
copy = helper[helper.index('bool Dx11WithDx12::CopyUpscalerOutputToDx11'):helper.index('Dx11WithDx12::RetainedResources')]
check(copy.index('CanCopyBack(identity)') < copy.index('SyncDx12ToDx11(retained)'))
check(copy.index('QueueCopyBack(identity)') < copy.index('CopyResource('))
check('UpscalerCacheQuarantined || !UpscalerReadersReusable()' in helper)
check('RetainDerivedUse(*action,pins)' in helper and 'BindExternalWait(list,UpscalerInputCompletion.fence.Get()' in helper)
check(fg.index('RetainUpscalerReader(') < fg.index('fg->SetResource(&setResource)'))
check(fg.count('FG_ResourceValidity::ValidButMakeCopy') == 3)
check('GpuSafety::Reusable(_uiBridgeUse[index])' in owner)
check('PrepareRecordingHooks(_uiCommandList[index])' in owner and 'GpuSafety::Record(_uiCommandList[index])' in owner)
get_ui=owner[owner.index('ID3D12GraphicsCommandList* IFGFeature_Dx12::GetUICommandList'):owner.index('bool IFGFeature_Dx12::ObserveUIBridgeSubmission')]
check(get_ui.index('GpuSafety::PrepareRecordingHooks(_uiCommandList[index])') < get_ui.index('_uiCommandList[index]->Reset('))
check('CancelOwnedUnsubmittedRecording(_uiCommandList[i],use,_uiCommandAllocator[i])' in owner)
for path, count in [('ffx/FSRFG_Dx12.cpp',2),('xefg/XeFG_Dx12.cpp',2),('dlssg/DLSSG_Dx12.cpp',1)]:
    check(read('OptiScaler/framegen/'+path).count('ObserveUIBridgeSubmission(fIndex);') == count)
    release_owner=read('OptiScaler/framegen/'+path)
    release_owner=release_owner[release_owner.index('::ReleaseObjects()'):]
    check(release_owner.index('RetireUIBridgeReaders()') < release_owner.index('SAFE_RELEASE(_uiCommandList[i])'))
check('CurrentBridgeWork(cmdList)) work->modelCreated=true' in renderer)
check('CurrentBridgeWork(cmdList)) work->evaluated=true' in renderer)
check('CurrentBridgeWork(cmdList)) work->composed=true' in renderer)
check(evaluation.index('work.outputRestoreFailed') < evaluation.index('dx12Feature->Evaluate(cmdList, InParameters)'))
check('if (typed || bridgeResourceParameters)' in renderer and 'bridgeResourceParameters=true' in renderer)
project=read('OptiScaler/OptiScaler.vcxproj')
check('dlssnr\\NrBridgeOutcome.h' in project and 'dlssnr\\NrBridgeRetirement.h' in project)
print(f'PASS: {checks} bridge owner, reader, receipt and publication source-wiring checks')
