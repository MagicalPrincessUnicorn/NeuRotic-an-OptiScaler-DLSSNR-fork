$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$present = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_Present.cpp') -Raw
$compat = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_PresentCompatibility.h') -Raw
$history = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/dlssnr/DlssNr_PresentHistory.h') -Raw
$feature = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/shaders/dlssnr/DlssNr_Dx12.cpp') -Raw
$bridge = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/upscalers/IFeature_Dx11wDx12.cpp') -Raw
function Assert-Contract([bool]$condition, [string]$message) {
    if (-not $condition) { throw $message }
    Write-Output "PASS: $message"
}
$descriptor = $present.IndexOf('// Record the actual Present target before any compatibility guard.')
$guard = $present.IndexOf('const auto admission = PresentCompatibility::Admit(capabilities);', $descriptor)
foreach ($field in @('backbufferWidth', 'backbufferHeight', 'backbufferFormat', 'backbufferSampleCount')) {
    $assignment = $present.IndexOf('g_present.telemetry.' + $field, $descriptor)
    Assert-Contract ($assignment -gt $descriptor -and $assignment -lt $guard) "$field captured before the compatibility guard"
}
Assert-Contract ($present.Contains('target {}x{} format {} samples {} swap effect {} color space {}')) 'fallback log contains the exact Present target descriptor'
Assert-Contract ($history.Contains('CompleteOutputPresent') -and $history.Contains('Invalidate') -and $history.Contains('ResetForNextEvaluation')) 'Present history state distinguishes continuity from interruption'
Assert-Contract ($feature.Contains('frame.Reset = resetHistory;')) 'Present history reset value reaches Feature 18'
Assert-Contract ($present -match '(?s)EvaluateImageOnlyCommandList\(.*?g_present\.history\.ResetForNextEvaluation\(\)') 'Present passes continuity reset state into Feature 18'
Assert-Contract ($present -match '(?s)completedOutput.*?SUCCEEDED\(sample\.result\).*?CompleteOutputPresent\(\).*?original Present failed') 'only a successful original Present completes temporal continuity'
Assert-Contract ($compat.Contains('PixelPath::Rgba8Direct') -and $compat.Contains('PixelPath::Rgb10Conversion')) 'capability admission distinguishes direct RGBA8 and converted 10-bit SDR'
Assert-Contract ($present -match '(?s)Rgba8Direct.*?CopyResource\(g_present\.frame\.Get\(\), presentInput\).*?EvaluateImageOnlyCommandList') 'D3D12 RGBA8 direct copy route is preserved'
Assert-Contract ($present -match '(?s)conversionSource.*?inputTransfer->Dispatch.*?EvaluateImageOnlyCommandList.*?outputTransfer->Dispatch.*?conversionOutput') 'R10G10B10A2 conversion brackets model work'
Assert-Contract ($present -match '(?s)PrepareTextureFrom11To12\(\s*"Present input".*?PrepareTextureFrom11To12\(\s*"Present output".*?SyncDx11ToDx12\(\).*?EvaluateImageOnlyCommandList.*?SyncDx12ToDx11\(\).*?context11->CopyResource\(backbuffer11\.Get\(\), g_present\.dx11Output\.SharedTexture\)') 'D3D11 Present copies into private shared input, synchronizes NR, and copies completed output back'
Assert-Contract ($present.IndexOf('const auto admission = PresentCompatibility::Admit(capabilities);') -lt $present.IndexOf('EvaluateImageOnlyCommandList')) 'all target admission completes before model work'
Assert-Contract ($present -match '(?s)if \(!BuildResources\(.*?SetFallback\(api, "private Present resources could not be created".*?return identity;.*?EvaluateImageOnlyCommandList') 'private-resource creation failure returns before model work'
Assert-Contract ($present -match '(?s)if \(!inputReady \|\| !outputReady \|\| !resourcesArePrivate.*?SetFallback\(api, "D3D11 compatible private shared images could not be created".*?return identity;.*?EvaluateImageOnlyCommandList') 'D3D11 shared-resource failure returns before model work'
Assert-Contract ($present -match '(?s)if \(!Dx11WithDx12::SyncDx11ToDx12\(\)\).*?SetFallback\(api, "D3D11-to-D3D12 input synchronization failed".*?return identity;.*?EvaluateImageOnlyCommandList') 'D3D11 input synchronization failure returns before model work'
foreach ($cause in @('DirtyRectsCount', 'pScrollRect', 'pScrollOffset', 'HDR or non-SDR color space is unsupported', 'Present target format is unsupported', 'D3D11 shared-resource synchronization is unavailable', 'Present target and NR queue use different devices')) {
    Assert-Contract ($present.Contains($cause) -or $compat.Contains($cause)) "fail-closed rejection covers $cause"
}
Assert-Contract ($bridge -match '(?s)EvaluateBeforeUpscale\(.*?dx12Feature->Evaluate\(.*?RestoreAfterUpscale\(.*?EvaluateAfterUpscale\(') 'BG3 bridge follows pre-NR, upscale, restore, post-NR order'
Assert-Contract (-not $bridge.Contains('ProbeD3D11(')) 'speculative direct D3D11 model probe is not invoked'
Assert-Contract ($bridge -match '(?s)if \(!dx12EvalResult \|\| !commandListExecuted\).*?if \(!CopyBackOutput\(\)\)') 'failed bridge work cannot reach D3D11 output copy-back'
Assert-Contract ($bridge.Contains('completedPipelineEvaluations') -and $bridge.Contains('BridgeTelemetry().CopyBack(true)')) 'bridge success requires observed composition before copy-back status'
