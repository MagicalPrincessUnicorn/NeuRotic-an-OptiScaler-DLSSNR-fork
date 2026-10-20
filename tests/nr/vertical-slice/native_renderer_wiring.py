"""Structural boundary checks only; these cannot qualify model/GPU behavior."""
import pathlib,sys
repo=pathlib.Path(__file__).resolve().parents[3]
source=(repo/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8-sig')
forwarder=(repo/'OptiScaler/dlssnr/forwarder/dlssnr_forwarder.cpp').read_text(encoding='utf-8-sig')
def ordered(*tokens):
    cursor=0
    for token in tokens:
        cursor=source.find(token,cursor)
        if cursor<0:return False
        cursor+=len(token)
    return True
checks={
 'typed-entry':'void EvaluateNativeProtocolTyped(' in source,
 'first-transition-check':ordered('void DlssNr_Dx12::Dispatch(', 'NativeCheckPoint::BeforeRecord','resourceStates.Transition(target, outputArrival'),
 'model-check-and-entry-callback':ordered('const bool mainResetSubmitted = g_nr.reset;', 'NativeCheckPoint::BeforeModel',
     'const DlssNr::ProviderEntryCallbacks callbacks', 'c.observer->ProviderEntry(c.reset)', 'result=g_nr.evaluateGuidesV4('),
 'forwarder-actual-entry': forwarder.find('if(callbacks&&!entry.Enter())return 0;') <
     forwarder.find('volatile int result = g_snip.evaluate(cmd, feature, capabilityParams, nullptr);') <
     forwarder.find('if(callbacks)entry.Returned(result==1);'),
 'history-owner-reset-projection':ordered('observer->RequiresReset()', 'const bool mainResetSubmitted = g_nr.reset;'),
 'model-result':ordered('const bool mainResetSubmitted = g_nr.reset;', 'observer->ModelResult(', 'g_nr.reset = g_nr.preSrSoftResetDispatch'),
 'composition-check-and-milestone':ordered('NativeCheckPoint::BeforeComposition',
     'const bool resolveRecorded=DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer',
     'resolveObservation.Finish();', 'if (!resolveRecorded)', 'observer->Composed()'),
 'typed-pre-sr-shared-core':'EvaluateBeforeUpscaleCore(' in source and 'EvaluateNativeProtocolTyped(cmdList, scratchInput' in source,
 'pre-sr-restores-output':ordered('EvaluateNativeProtocolTyped(cmdList, scratchInput','params->Set(NVSDK_NGX_Parameter_Output, originalOutputVoid);'),
 'pre-sr-copy-check':ordered('Native.PreSrCopyBack', 'cmdList->CopyResource(color, g_nr.preSrScratch);'),
 'typed-readiness-not-counter':'typed ? !typed->observer->CompositionRecorded()' in source,
 'actual-pass-output-capability':ordered('const bool resolveRecorded=DispatchPass(cmdList, resolveParams, resolveProxy, resolveAnswer',
     'if (!resolveRecorded)', 'const DlssNr::NativeOutputPassToken pass(rendererBorrow,resolveOutcome);',
     'observer->OutputProduced(pass)'),
}
for name,matched in checks.items():print(f'{name}: {"PASS" if matched else "FAIL"}')
print('Source wiring only. GPU/model/parity/IQ/performance remain NOT_RUN.')
sys.exit(0 if all(checks.values()) else 1)
