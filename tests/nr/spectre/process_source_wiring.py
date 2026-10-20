"""Actual NGX attachment/exclusion structure; no model/provider runtime claim."""
import pathlib
repo=pathlib.Path(__file__).resolve().parents[3]
text=(repo/'OptiScaler/inputs/NVNGX_DLSS_Dx12.cpp').read_text()
evaluate=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_EvaluateFeature('):]
evaluate=evaluate[:evaluate.index('#pragma endregion')]
checks=[
    ('class NativeDx12Source' in text,'actual NGX owns private process bootstrap'),
    ('host->Capture(HandleToFeature, std::move(pin))' in text or 'host_->Capture(HandleToFeature, std::move(pin))' in text,'exact registered physical source is used'),
    ('NativeDx12Source::Capture(std::move(*sourcePin), InCmdList, InParameters)' in evaluate,'outer callback transfers one physical source pin and original call'),
    ('if (!nativeSource || !nativeSource->Current())' in evaluate,'unavailable captured source refuses'),
    ('std::optional<Neurotic::Lifecycle::NativeProcessBootstrap::Callback> nativeSource;' in evaluate,'one captured source spans pre SR and post SR'),
    (evaluate.count('!nrSettings->DlssNrNativeProtocol.value_or_default()')==4,'all four legacy NR entries exclude candidate opt-in'),
]
bootstrap=(repo/'OptiScaler/nr/lifecycle/NativeProcessBootstrap.h').read_text()
capture=(repo/'OptiScaler/dlssnr/NativeNgxCallCapture.h').read_text()
checks.extend([
    ('host->BindCall(*callback, list, parameters, NVSDK_NGX_Result_Success)' in text or 'host_->BindCall(*callback, list, parameters, NVSDK_NGX_Result_Success)' in text,'actual source binds the original call once'),
    (evaluate.index('NativeDx12Source::Capture(')<evaluate.index('DlssNr::EvaluateBeforeUpscale(')<evaluate.index('NVNGXProxy::D3D12_EvaluateFeature()('),'original capture precedes pre and SR actions'),
    ('callback.state_!=state_||callback.callCaptured_||!callback.Current()' in bootstrap,'capture is tied to current exact root and one attempt'),
    ('NativeIdentity::CompareDevices(device.Get(),capture->device_.Get()).equal' in capture,'actual device identity required for every retained resource'),
    ('ContentWrite(' not in capture and 'FromKnown' not in capture,'raw capture issues no semantic or content truth'),
])
sr_call=evaluate.index('NVNGXProxy::D3D12_EvaluateFeature()(InCmdList, InFeatureHandle, InParameters, InCallback)')
restore_gate=evaluate.find('if (!Neurotic::Protocol::NativeMayContinueOuterEvaluation(nativeBefore.facts))')
checks.append((restore_gate>evaluate.index('const auto nativeBefore = DlssNr::NativeDx12Source::RunBefore(') and
               restore_gate<evaluate.index('// Native DLSS passthrough') and
               'return NVSDK_NGX_Result_FAIL_PlatformError;' in evaluate[restore_gate:restore_gate+200],
               'failed Output restoration refuses before either actual SR path'))
renderer=(repo/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
checks.append((renderer.count('observer->OutputRestoreFailed()')==2,
               'normal and RAII restoration failures preserve the outer refusal fact'))
sr_return=evaluate.index('return result;',sr_call)
sr_observation=evaluate.find('NativeDx12Source::ObserveSrReturn(',sr_call,sr_return)
checks.append((sr_observation>sr_call and sr_observation<evaluate.index('RestoreAfterUpscale(InParameters)',sr_call),
               'actual SR result and original Output binding are observed before the outer return'))
checks.append(('if (isSuperResolution && nrSettings->DlssNrRunBeforeSr.value_or_default())' in evaluate[:sr_call],
               'unfinished Native After does not prepare the model before its required SR service'))
source=text[text.index('class NativeDx12Source'):text.index('static std::mutex ngxObservationMutex')]
run_before=source[source.index('    static auto RunBefore('):source.index('    static void FinishReturn(')]
checks.extend([
    ('Native.CommandStateCaptureFailed");return host->RecordNativeResult(callback,result);' in run_before,
     'capture exceptions preserve their reason before the actual callback returns'),
    ('if(!captured)return host->RecordCommandStateUnavailable(callback,D3D12Hooks::ObserveNativeRecording(' in run_before,
     'capture refusal records the existing owner observation without granting coverage'),
    (run_before.rfind('return host->RecordNativeResult(callback,result);')>run_before.index('host->ObserveNativeRestoration('),
     'final preparation and execution result is retained after actual restoration observation'),
])
creation=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result NVSDK_NGX_D3D12_CreateFeature('):]
creation=creation[:creation.index('#pragma endregion')]
selected_creation=creation[creation.index('const bool selectedNativeSr='):]
checks.append(('if (selectedNativeSr)' in selected_creation and
               'D3D12Hooks::InstallNativeRecordingHooks(InCmdList);' in selected_creation and
               selected_creation.find('D3D12Hooks::HookToCommandListLate(InCmdList);') <
               selected_creation.find('D3D12Hooks::InstallNativeRecordingHooks(InCmdList);') <
               selected_creation.find('NVNGXProxy::D3D12_CreateFeature()(InCmdList'),
               'enabled selected Create installs native tracking after optional legacy hooks and before real provider entry'))
checks.extend([
    ('NativeNgxCreationParameters::Capture(InParameters, NVSDK_NGX_Result_Success)' in creation,
     'original creation is captured at the actual wrapper'),
    ('const auto nativeCreation = cfg.DlssNrNativeProtocol.value_or_default()' in creation and
     creation.index('const auto nativeCreation')<creation.index('TryCreateOptiFeature('),
     'candidate creation snapshot precedes host overrides and preserves legacy opt-out'),
    (creation.count('HandleToFeature.Set((*OutHandle)->Id, InFeatureID, nativeCreation)')==3,
     'all successful actual feature incarnations retain original creation snapshot'),
])
checks.append(('host = host_;\n        }\n        auto callback = host->Capture' in source,
               'root lock is released before external COM capture'))
for signature in ['NVSDK_NGX_D3D12_Shutdown(void)','NVSDK_NGX_D3D12_Shutdown1(ID3D12Device* InDevice)']:
    body=text[text.index('NVSDK_NGX_API NVSDK_NGX_Result '+signature):].split('\n}\n',1)[0]
    checks.append(('NativeDx12Source::Close()' in body and body.index('NativeDx12Source::Close()')<body.index('ShutdownDx12Core()'),
                   signature+' closes genuine source before physical shutdown'))
hooks=(repo/'OptiScaler/hooks/D3D12_Hooks.cpp').read_text()
installation=hooks.split('void D3D12Hooks::InstallNativeRecordingHooks(',1)[1].split('\nstatic void HookToCommandList',1)[0]
checks.extend([
    (installation.index('GpuSafety::PrepareRecordingHooks(commandList)') < installation.index('s_nativeRecordingHooks.Install(commandList)'),
     'completion owner installs before native executable routes are pinned'),
    ('s_nativeRecordingHooks.Revoke();' in installation.split('s_nativeRecordingHooks.Install(commandList)')[0] and
     'return;' in installation.split('s_nativeRecordingHooks.Install(commandList)')[0],
     'completion preparation failure revokes coverage and stops native installation'),
])
for value,label in checks:
    if not value:print('FAIL '+label)
print(f'Process source wiring: {len(checks)} structural checks, {sum(not value for value,_ in checks)} failures; runtime NOT_RUN')
raise SystemExit(0 if all(value for value,_ in checks) else 1)
