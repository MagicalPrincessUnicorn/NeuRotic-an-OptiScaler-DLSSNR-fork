param([string]$Label='recording-work-green')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/recording-work-cost'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-return-065329-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I"'+$root+'/external/vulkan/include" "'+$root+'/tests/vulkan_recording_work_cost.cpp" "'+$root+'/OptiScaler/dlssnr/VulkanNrRecording.cpp" /Fe:work.exe /Fo:"'+$output+'/" /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','work.exe','exit /b %errorlevel%'))
Push-Location $output
try {
 $ErrorActionPreference='Continue';$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$code=$LASTEXITCODE;$ErrorActionPreference='Stop'
 [IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log
 if($code -ne 0){throw "Recording work regression failed: $code"}
} finally {Pop-Location}
