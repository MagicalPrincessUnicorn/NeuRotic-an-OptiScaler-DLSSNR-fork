param([string]$Label='tracker-interleaved-green')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/tracker-interleaved-cost'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-return-065329-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/hooks/CommandBuffer_StateTracker.h')
# Replace only application include boundaries; all tracker methods execute verbatim.
$source=[regex]::Replace($source,'(?m)^#include <(pch.h|State.h)>\r?\n','').Replace('#pragma once','')
$template=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'vulkan_tracker_interleaved_cost.cpp.in')
[IO.File]::WriteAllText((Join-Path $output 'tracker.cpp'),$template.Replace('// PRODUCTION_TRACKER',$source))
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /I"'+$root+'/OptiScaler" /I"'+$root+'/external/vulkan/include" /I"'+$root+'/external/nvngx_dlss_sdk" tracker.cpp /Fe:tracker.exe /Fo:tracker.obj /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','tracker.exe','exit /b %errorlevel%'))
Push-Location $output
try {
 $ErrorActionPreference='Continue';$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log
 if($code -ne 0){throw "Tracker interleaved regression failed: $code"}
} finally {Pop-Location}
