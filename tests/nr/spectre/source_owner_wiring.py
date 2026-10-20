"""Structural boundary evidence only; never provider/game runtime evidence."""
import pathlib
repo=pathlib.Path(__file__).resolve().parents[3]
text=(repo/'OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp').read_text()
release=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_ReleaseFeature('):]
release=release[:release.index('NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_GetFeatureRequirements(')]
evaluate=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature('):]
assert 'HandleToFeature.BeginRelease(handleId, featureSnapshot)' in release, 'source release must reserve exclusion before physical release'
assert 'if (featureSnapshot)' in release and 'HandleToFeature.Protected(' not in release, 'release must exclude a racing first candidate pin atomically'
assert release.index('HandleToFeature.BeginRelease(')<release.index('ExposureScan::ReleaseTrackedResources()'), 'no ownership effects before busy refusal'
assert 'sourceRelease->Complete(result == NVSDK_NGX_Result_Success)' in release, 'actual provider result controls source release'
assert 'return finishRelease(NVSDK_NGX_Result_FAIL_FeatureNotFound);' in release, 'definitive unsupported release restores source admission'
assert 'sourcePin = HandleToFeature.Pin(handleId, featureSnapshot);' in evaluate, 'actual candidate callback must own source pin'
assert evaluate.index('DlssNrNativeProtocol.value_or_default()')<evaluate.index('sourcePin = HandleToFeature.Pin(')<evaluate.index('// Native DLSS passthrough'), 'default-off pin must precede native work'
assert 'if (!sourcePin) return NVSDK_NGX_Result_FAIL_FeatureNotFound;' in evaluate, 'missing source pin refuses before candidate work'
for name in ['NVSDK_NGX_D3D12_Shutdown(void)','NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* InDevice)']:
    shutdown=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result '+name):]
    shutdown=shutdown.split('\n}\n',1)[0]
    assert 'HandleToFeature.BeginShutdown()' in shutdown, 'actual shutdown must reserve exclusive source ownership'
    assert shutdown.index('HandleToFeature.BeginShutdown()')<shutdown.index('ShutdownDx12Core()'), 'reservation must precede physical shutdown'
    assert 'sourceShutdown->Complete();' in shutdown, 'definitive shutdown return ends only the physical source action'
assert 'return NVSDK_NGX_D3D12_Shutdown();' not in text, 'Shutdown1 must share the core without double reservation'
print('Source ownership wiring: 15 structural checks PASS; runtime NOT_RUN')
