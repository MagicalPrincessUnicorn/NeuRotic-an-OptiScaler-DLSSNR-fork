$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$owners = @(
    'OptiScaler/upscalers/xess/XeSSFeature_Dx12.cpp',
    'OptiScaler/upscalers/ffx/FFXFeature_Dx12.cpp',
    'OptiScaler/upscalers/fsr2/FSR2Feature_Dx12.cpp',
    'OptiScaler/upscalers/fsr2_212/FSR2Feature_Dx12_212.cpp'
)
foreach ($owner in $owners) {
    $source = Get-Content -Raw (Join-Path $root $owner)
    $evaluation = $source.Substring($source.IndexOf('::EvaluateInternal('))
    if (([regex]::Matches($evaluation, 'const auto depthArrival = Config::Instance\(\)->DepthResourceBarrier\.snapshot\(\);').Count -ne 1) -or
        ([regex]::Matches($evaluation, 'depthArrival\.has_value\(\)').Count -ne 2) -or
        ([regex]::Matches($evaluation, 'depthArrival\.value\(\)').Count -ne 2) -or
        ([regex]::Matches($evaluation, 'DepthResourceBarrier\.').Count -ne 1)) {
        throw "Entry and restore must use one copied depth arrival: $owner"
    }
    Write-Output "PASS depth barrier consumer wiring: $owner"
}
