param([switch]$SystemGpu)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/inspector-motion/vulkan-tests'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$before=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$oldIcd=$env:VK_ICD_FILENAMES
$oldDrivers=$env:VK_DRIVER_FILES
try {
    if (!$SystemGpu) {
        $runtime=Get-ChildItem -LiteralPath 'C:/Program Files/Google/Chrome/Application' -Directory |
            Sort-Object Name -Descending | ForEach-Object {Join-Path $_.FullName 'vk_swiftshader.dll'} |
            Where-Object {Test-Path -LiteralPath $_} | Select-Object -First 1
        if (!$runtime) {throw 'Existing Chrome SwiftShader runtime unavailable; use -SystemGpu for an installed Vulkan driver.'}
        $manifest=Join-Path $output 'swiftshader.json'
        @{file_format_version='1.0.0';ICD=@{api_version='1.0.5';library_path=$runtime}} |
            ConvertTo-Json | Set-Content -LiteralPath $manifest -Encoding ascii
        $env:VK_ICD_FILENAMES=$manifest;$env:VK_DRIVER_FILES=$manifest
    }
    & cmd.exe /d /c "`"$PSScriptRoot/Run-CharacterCaptureVk.cmd`"" 2>&1 | Tee-Object -FilePath (Join-Path $output 'gpu.log')
    $result=$LASTEXITCODE
    $peak=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
    $resolved=[IO.Path]::GetFullPath($output)
    if ($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/inspector-motion/vulkan-tests'))) {throw 'Fixture cleanup boundary mismatch'}
    Get-ChildItem -LiteralPath $resolved -File -Filter '*.obj' | Remove-Item
    $after=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
    "Vulkan Inspector fixture bytes before=$before peak=$peak after=$after; own object files removed" |
        Tee-Object -FilePath (Join-Path $output 'gpu.log') -Append
    if($result -ne 0){throw "Vulkan Inspector GPU test failed: $result"}
} finally {
    $env:VK_ICD_FILENAMES=$oldIcd;$env:VK_DRIVER_FILES=$oldDrivers
}
