from pathlib import Path

root = Path(__file__).resolve().parents[1]
identity = (root / 'OptiScaler/dlssnr/NativeIdentity.h').read_text(encoding='utf-8')
guides = (root / 'OptiScaler/dlssnr/DlssNr_PresentGuides.h').read_text(encoding='utf-8')
renderer = (root / 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp').read_text(encoding='utf-8')

def require(condition, message):
    if not condition:
        raise AssertionError(message)
    print('PASS:', message)

require('if (base) { resolved.result = E_UNEXPECTED; return resolved; }' in identity,
        'malformed E_NOINTERFACE output is rejected')
require('SUCCEEDED(comparison.left.result)' in identity and
        'SUCCEEDED(comparison.right.result)' in identity and
        'comparison.left.object.Get() == comparison.right.object.Get()' in identity,
        'comparison requires valid canonical identities')
require('GetAdapterLuid' not in identity, 'LUID is not identity proof')
require('NativeIdentity::CompareDevices(device.Get(), depthDevice.Get()).equal' in guides and
        'NativeIdentity::CompareDevices(device.Get(), motionDevice.Get()).equal' in guides,
        'both guide resources use canonical device comparison')
require('NativeIdentity::CompareDevices(device, device).equal' in renderer and
        'NativeIdentity::CompareDevices(g_generationDevice, device).equal' in renderer,
        'generation owner proves adoption and later aliases')

present = (root / 'OptiScaler/dlssnr/DlssNr_Present.cpp').read_text(encoding='utf-8')
require('device = devices.left.object' not in present and
        'device = executionDevice.object' in present,
        'Present execution does not use the comparison-only device identity')
