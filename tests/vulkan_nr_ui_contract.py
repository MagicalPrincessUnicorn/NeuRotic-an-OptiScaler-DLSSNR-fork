from pathlib import Path
root = Path(__file__).resolve().parents[1]
menu = (root / 'OptiScaler/dlssnr/DlssNr_Menu.cpp').read_text()
for obsolete in ['BasicMultipass::Active(config->GetDlssNrConfigSnapshot()) && !IsVulkanInput()',
                 'nativeAfterOnly = IsVulkanInput() ||',
                 'unsupportedHold = State::Instance().api == API::Vulkan;',
                 'Vulkan remains single-pass', 'native Vulkan keeps its saved single-pass']:
    assert obsolete not in menu, obsolete
assert 'BestExposureScanVk' in menu
assert 'PassChainStatusVk' in menu
assert 'CurrentVulkanNrCapabilities().hold.available' in menu
print('PASS Vulkan controls admit implemented placement, Hold and multipass; calibration and chain read actual Vulkan state')

assert '#define LOW_PRECISION_TRACKING' not in (root / 'OptiScaler/hooks/CommandBuffer_StateTracker.h').read_text()

# A forwarding wrapper must invalidate real-frame associations for every work writer.
import re
hooks=(root/'OptiScaler/hooks/Vulkan_Hooks.cpp').read_text()
bridge=(root/'OptiScaler/hooks/VulkanwDx12_Hooks.cpp').read_text()
for name in ['DispatchBase','DispatchBaseKHR','DrawIndirectCountKHR','DrawIndexedIndirectCountKHR','DrawIndirectCountAMD','DrawIndexedIndirectCountAMD','TraceRaysNV','DrawMeshTasksNV','DrawMeshTasksIndirectNV','DrawMeshTasksIndirectCountNV']:
    assert '"vkCmd'+name+'"' in hooks and 'using NrWork'+name+'=' in hooks, name
    start=bridge.index('void Vulkan_wDx12::hk_vkCmd'+name+'(')
    end=bridge.index('\n}',start)
    body=bridge[start:end]
    assert 'VkNrObservation::Work' in body and 'VulkanNrRecordings().OnWork(cmdBuffer)' in body, name
print('PASS Vulkan work aliases and original forwarding paths observe the actual recording')

feature=(root/'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp').read_text()
capabilities=feature[feature.index('VkNrCapabilitySnapshot CurrentVulkanNrCapabilities()'):feature.index('bool NativeRayReconstructionVk()')]
assert 'selected->GetUpscalerType()==Upscaler::DLSSD' in capabilities
print('PASS selected Ray Reconstruction qualifies after-RR capture before an enabled NR observation')

# The flagship's actual primary readout and preflight must select the Vulkan owner.
assert 'return IsVulkanInput() ? NativeTelemetryVk() : Telemetry();' in menu, 'Vulkan preflight still reads only D3D12 telemetry'
primary=menu[menu.index('static void RenderLiveReadouts('):menu.index('void TickAdvisor(')]
assert 'SelectedNativeTelemetry()' in primary and 'LastGpuTimeVk()' in primary, 'Vulkan timing is absent beside Enable NR'
assert 'ObserveNativeVk(InParameters' in (root/'OptiScaler/inputs/NVNGX_DLSS_Vk.cpp').read_text(), 'selected Vulkan inputs are not published to Signal Status'
print('PASS primary NR activity/dimensions/timing and Signal Status select the Vulkan native owner')

# Guide copies use the authenticated pre-provider input boundary and invalidate failed SR.
ngx=(root/'OptiScaler/inputs/NVNGX_DLSS_Vk.cpp').read_text()
copy=ngx.index('DlssNr::CaptureVulkanPresentGuides(frame);')
evaluate=ngx.index('deviceContext->Evaluate(InCmdList, InParameters)',copy)
assert copy < evaluate
guard=ngx[ngx.index('// Copy borrowed guides'):copy]
assert 'output.width!=deviceContext->DisplayWidth()' in guard
assert 'output.height!=deviceContext->DisplayHeight()' in guard
assert '!resetKnown||outputX||outputY' in guard
assert 'Selected SR evaluation failed after guide capture' in ngx[evaluate:]
assert 'Selected SR evaluation threw after guide capture' in ngx[evaluate:]
print('PASS guide forwarding checks full output and reset before copy, and invalidates failed SR')
