"""Structural consumer attachment only; no shader/model execution evidence."""
import pathlib,sys
repo=pathlib.Path(sys.argv[1]) if len(sys.argv)>1 else pathlib.Path(__file__).resolve().parents[3]
renderer=(repo/'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text()
runtime=(repo/'OptiScaler/nr/protocol/NativeTemporalRuntime.h').read_text()
checks={
    'encode compares every actual constant': 'observer->CheckEncode({encodeParams.Passthrough, encodeParams.WhitePoint,\n        encodeParams.UseGameExposure, encodeParams.ExposurePreMul, encodeParams.ReversibleMode,\n        encodeParams.Width, encodeParams.Height})' in renderer,
    'encode guard precedes resource rights and actual dispatch': 0<=renderer.find('observer->CheckEncode(')<renderer.find('const auto encodeUses')<renderer.find('DispatchPass(cmdList, encodeParams'),
    'encode mismatch exits before recording': 'encodeParams.Width, encodeParams.Height}))\n    { device->Release(); return; }' in renderer,
    'observer uses admitted immutable product': 'const auto* product=admission_?admission_->Product():nullptr;' in runtime,
    'observer decodes retained binding plan': 'ReadNativeEncodeBindings(product->bindingPlan)' in runtime,
    'observer rejects absent or unequal controls': 'if(!facts_.failure.Empty()||!expected||*expected!=actual)' in runtime,
    'observer records truthful refusal': 'Fail("Native.EncodeContractMismatch");return false;' in runtime,
}
for name,passed in checks.items():print(('PASS ' if passed else 'FAIL ')+name)
sys.exit(0 if all(checks.values()) else 1)
