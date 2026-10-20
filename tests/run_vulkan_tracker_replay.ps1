param([string]$Label='replay-green')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/tracker-replay'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-adversarial-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $root 'OptiScaler/hooks/CommandBuffer_StateTracker.h')
$source=[regex]::Replace($source,'(?m)^#include <(pch.h|State.h)>\r?\n','').Replace('#pragma once','')
$template=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'vulkan_tracker_replay.cpp.in')
[IO.File]::WriteAllText((Join-Path $output 'replay.cpp'),$template.Replace('// PRODUCTION_TRACKER',$source))
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /I"'+$root+'/OptiScaler" /I"'+$root+'/external/vulkan/include" /I"'+$root+'/external/nvngx_dlss_sdk" replay.cpp /Fe:replay.exe /Fo:replay.obj /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','replay.exe','exit /b %errorlevel%'))
Push-Location $output
try {$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$code=$LASTEXITCODE;[IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log;if($code -ne 0){throw "Replay regression failed: $code"}} finally {Pop-Location}
