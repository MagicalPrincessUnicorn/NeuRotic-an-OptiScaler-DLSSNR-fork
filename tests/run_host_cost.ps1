param([string]$Label='verification')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/host-cost'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-camera-timing-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /I"'+$root+'/OptiScaler" "'+$root+'/tests/host_cost.cpp" /Fe:host_cost.exe /Fo:host_cost.obj /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off',('call "'+$vcvars+'" -vcvars_ver=14.44 >nul'),'if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','host_cost.exe','exit /b %errorlevel%'))
Push-Location $output
try {
    $ErrorActionPreference='Continue'
    $log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String)
    $code=$LASTEXITCODE
    $ErrorActionPreference='Stop'
    [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log)
    Write-Output $log
    if($code -ne 0){throw "Host timing regression failed: $code"}
} finally {Pop-Location}
