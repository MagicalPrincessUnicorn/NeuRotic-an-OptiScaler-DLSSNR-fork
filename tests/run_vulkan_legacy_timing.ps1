param([string]$Label='verification')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/legacy-timing'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-camera-return-103600-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$header=Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/upscaler_time/UpscalerTime_Vk.h')
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/upscaler_time/UpscalerTime_Vk.cpp')
$header=[regex]::Replace($header,'(?m)^#(?:include|pragma) [^\r\n]*\r?\n','')
$source=[regex]::Replace($source,'(?m)^#include [^\r\n]*\r?\n','')
$template=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'vulkan_legacy_timing.cpp.in')
[IO.File]::WriteAllText((Join-Path $output 'timing.cpp'),$template.Replace('// PRODUCTION_HEADER',$header).Replace('// PRODUCTION_SOURCE',$source))
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /I"'+$root+'/external/vulkan/include" timing.cpp /Fe:timing.exe /Fo:timing.obj /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','timing.exe','exit /b %errorlevel%'))
Push-Location $output
try {
 $ErrorActionPreference='Continue';$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log
 if($code -ne 0){throw "Legacy timing regression failed: $code"}
} finally {Pop-Location}
