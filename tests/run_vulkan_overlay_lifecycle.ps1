param([string]$Label = 'verification', [switch]$CapturePreimage, [string]$EvidenceDirectory = '')
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$build = Join-Path $root 'builds/vulkan-experiment/tests/overlay-lifecycle'
$evidence = Join-Path $root 'docs/implementation-evidence/vulkan-adversarial-20261002/overlay'
if ($EvidenceDirectory) { $evidence = Join-Path $root $EvidenceDirectory }
New-Item -ItemType Directory -Force -Path $build,$evidence | Out-Null
$before = (Get-ChildItem -LiteralPath $build -File | Measure-Object Length -Sum).Sum
$source = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_overlay_vk.cpp') -Raw
$source = [regex]::Replace($source, '(?m)^#include[^\r\n]*', '')
$template = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'vulkan_overlay_lifecycle.cpp.in') -Raw
$common = Get-Content -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Raw
$captureStart = $common.IndexOf('void MenuCommon::SetVisibility(')
$captureEnd = $common.IndexOf('void MenuCommon::FinalizeFrame(', $captureStart)
$shutdownStart = $common.IndexOf('void MenuCommon::Shutdown()')
$capture = $common.Substring($captureStart, $captureEnd-$captureStart) + $common.Substring($shutdownStart)
if ($CapturePreimage) {
    $oldCommon = (& git -C $root show 'HEAD:OptiScaler/menu/menu_common.cpp' | Out-String)
    $oldHeader = (& git -C $root show 'HEAD:OptiScaler/menu/menu_common.h' | Out-String)
    $oldVisibility = [regex]::Match($oldHeader, '(?s)static void SetVisibility\(bool visible\)\s*(\{.*?\n    \})')
    if (!$oldVisibility.Success) { throw 'Preimage visibility definition not found' }
    $deferStart = $common.IndexOf('void MenuCommon::DeferInputCapture(')
    $capture = 'void MenuCommon::SetVisibility(bool visible) ' + $oldVisibility.Groups[1].Value + "`n" +
        $common.Substring($deferStart, $captureEnd-$deferStart) + $oldCommon.Substring($oldCommon.IndexOf('void MenuCommon::Shutdown()'))
}
[IO.File]::WriteAllText((Join-Path $build 'overlay.cpp'), $template.Replace('// PRODUCTION_CAPTURE', $capture).Replace('// PRODUCTION_OVERLAY', $source))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$vulkanHeaders = Join-Path $root 'external/vulkan/include'
if (!(Test-Path (Join-Path $vulkanHeaders 'vulkan/vulkan.h'))) { $vulkanHeaders = Join-Path $root '../Optiscaler-Stuff/external/vulkan/include' }
$compile = 'cl /nologo /std:c++20 /EHsc /MT /W4 /I"' + $vulkanHeaders + '" overlay.cpp /Fe:overlay.exe /Fo:overlay.obj /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $build 'build.cmd'), @('@echo off', ('call "'+$vcvars+'" -vcvars_ver=14.44 >nul'), 'if errorlevel 1 exit /b 1', 'set CL=', 'set _CL_=', $compile, 'exit /b %errorlevel%'))
Push-Location $build
try {
    $log = (& cmd /c build.cmd 2>&1 | Out-String)
    $exit = $LASTEXITCODE
    if ($exit -eq 0) {
        foreach ($mode in @('fg-startup-no-idle','generated-overlay','inspector-end-failure','inspector-capture','inspector-uncertain','inspector-busy','shutdown-owner','unavailable-shortcut','capture-owner','capture-failed-present','lineage-retirement','capacity-recovery','cpu-ahead-proof','proof-after-bypass','invisible-capture','create-retry','recreate','idle-failure','partial','incomplete','present-pin','wsi-retained','queue-growth','downstream-pin','uncertain','wrong-swapchain','semaphore-reuse','queue-change','foreign-backend','acquire-proof','retention-budget','unknown-queue','busy-frame','device-notification','detached-device','close-during-create','device-close-during-create','boundary-isolation','boundary-images','present-chain','present-metering','fg-running','chain-before-create','swapchain-shutdown')) {
            $info = [Diagnostics.ProcessStartInfo]::new((Join-Path $build 'overlay.exe'), $mode)
            $info.UseShellExecute = $false
            $info.CreateNoWindow = $true
            $info.RedirectStandardOutput = $true
            $info.RedirectStandardError = $true
            $process = [Diagnostics.Process]::Start($info)
            if (!$process.WaitForExit(15000)) {
                $process.Kill()
                $process.WaitForExit()
                $log += "FAIL $mode exceeded 15 second fixture deadline`n"
                $exit = 1
            }
            $log += $process.StandardOutput.ReadToEnd() + $process.StandardError.ReadToEnd()
            if ($process.ExitCode -ne 0) { $exit = 1 }
            $process.Dispose()
        }
    }
    Remove-Item -LiteralPath (Join-Path $build 'overlay.obj') -ErrorAction SilentlyContinue
    Remove-Item -LiteralPath (Join-Path $build 'overlay.cpp'),(Join-Path $build 'build.cmd') -ErrorAction SilentlyContinue
    $after = (Get-ChildItem -LiteralPath $build -File | Measure-Object Length -Sum).Sum
    $log += "storage before=$before after=$after bytes; fixture object removed`n"
    [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')), $log)
    $receipt = [ordered]@{
        exit_code = $exit
        source_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_overlay_vk.cpp') -Algorithm SHA256).Hash
        header_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_overlay_vk.h') -Algorithm SHA256).Hash
        fixture_sha256 = (Get-FileHash -LiteralPath (Join-Path $PSScriptRoot 'vulkan_overlay_lifecycle.cpp.in') -Algorithm SHA256).Hash
        common_sha256 = (Get-FileHash -LiteralPath (Join-Path $root 'OptiScaler/menu/menu_common.cpp') -Algorithm SHA256).Hash
        toolchain = 'MSVC x64 14.44; C++20; Vulkan SDK header types; fake driver/UI boundary'
        storage_before_bytes = $before
        storage_after_bytes = $after
        gpu_execution = $false
        capture_preimage = [bool]$CapturePreimage
    }
    $receipt | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $evidence ($Label+'.json')) -Encoding utf8
    Write-Output $log
} finally { Pop-Location }
exit $exit
