"""Combined ownership and capability wiring gates; complement actual GPU fixture tests."""
from pathlib import Path
root = Path(__file__).resolve().parents[1]
def read(name): return (root / name).read_text(encoding='utf-8')
native = read('OptiScaler/dlssnr/DlssNr_Dx11.cpp')
copy = native[native.index('// A depth dispatch'):native.index('if (s.nativePreSr)\n    {', native.index('// A depth dispatch'))]
assert copy.index('slot.originalDepth = depth') < copy.index('runtime.converter.Copy')
assert copy.index('runtime.context11->Signal') < copy.index('if (!copied)')
assert 'runtime.failed = true' in copy and 'runtime.context11->Flush();' in copy
assert 's.nativePreSr && !Dx11Transport::SupportedShape(colorDesc)' in native
assert 's.copyGuides, nullptr, 0, proof' in native
dx = read('OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp')
assert dx.count('primaryPlacement.RequiresRetirement(currentRouteIsPreSr)') == 1
assert 'privateCommandList && !nativeTemporalDomain' in dx
assert 'Screenshots::BackendRefusal(route, enabled, State::Instance().api == API::DX12)' in dx
menu = read('OptiScaler/dlssnr/DlssNr_Menu.cpp')
assert 'Screenshots::BackendRefusal(route, enabled, State::Instance().api == API::DX12)' in menu
assert 'busy || analysis || backendRefusal != nullptr' in menu
assert 'Native NR on###ScreenshotNative' in menu and 'Current full output###ScreenshotNative' in menu
assert 'g_timingSettings->SameConfiguration(cfg)' in dx
assert 'g_timingSettings->SameConfiguration(*currentSettings)' in dx
assert 'timingCapture != g_timingCapture || frame.Reset' in dx
assert 'queue != nullptr && !timingCapture' in dx
assert 'void InvalidateSamples() { _trigger.fill(false); _recording = false; }' in read('OptiScaler/gpu_time/GpuTime_Dx12.h')
print('PASS: partial-copy retirement, original inputs, distinct provider proof, placement deduplication, shared screenshot refusal, stable widget ID, fresh timing wiring')
