$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$present = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Present.cpp') -Raw
$bridge = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/upscalers/IFeature_Dx11wDx12.cpp') -Raw
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}
$descriptor = $present.IndexOf('const auto backDesc = backbuffer->GetDesc();')
$guard = $present.IndexOf('backDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM', $descriptor)
foreach ($field in @('backbufferWidth', 'backbufferHeight', 'backbufferFormat', 'backbufferSampleCount')) {
    $assignment = $present.IndexOf('g_present.telemetry.' + $field, $descriptor)
    Assert-Contract ($assignment -gt $descriptor -and $assignment -lt $guard) "$field captured before the compatibility guard"
}
Assert-Contract ($present.Contains('target {}x{} format {} samples {} swap effect {} color space {}')) 'fallback log contains the exact Present target descriptor'
Assert-Contract ($present.Contains('backDesc.Format != DXGI_FORMAT_R8G8B8A8_UNORM')) 'conservative R8 SDR format guard is unchanged'
Assert-Contract ($present.Contains('Present Image-Only is DX12 direct-queue only')) 'D3D11 Present remains explicitly unsupported'
Assert-Contract ($bridge -match '(?s)EvaluateBeforeUpscale\(.*?dx12Feature->Evaluate\(.*?RestoreAfterUpscale\(.*?EvaluateAfterUpscale\(') 'BG3 bridge follows pre-NR, upscale, restore, post-NR order'
Assert-Contract (-not $bridge.Contains('ProbeD3D11(')) 'speculative direct D3D11 model probe is not invoked'
Assert-Contract ($bridge -match '(?s)if \(!dx12EvalResult \|\| !commandListExecuted\).*?if \(!CopyBackOutput\(\)\)') 'failed bridge work cannot reach D3D11 output copy-back'
Assert-Contract ($bridge.Contains('completedPipelineEvaluations') -and $bridge.Contains('BridgeTelemetry().CopyBack(true)')) 'bridge success requires observed composition before copy-back status'
