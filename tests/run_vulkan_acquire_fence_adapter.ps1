param([string]$Label='fence-adapter-green')
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/acquire-fence-adapter'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-adversarial-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$lines=@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',('cd /d "'+$root+'"'),('cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I external/vulkan/include /I external/nvngx_dlss_sdk /I external/simpleini tests/vulkan_acquire_fence_adapter.cpp OptiScaler/dlssnr/VulkanPresentRegistry.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp /Fe:"'+$output+'/adapter.exe" /Fo:"'+$output+'/"'),'if errorlevel 1 exit /b 1',('"'+$output+'/adapter.exe"'),'exit /b %errorlevel%')
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),$lines)
& cmd.exe /d /c "$(Join-Path $output 'run.cmd')" *> (Join-Path $evidence ($Label+'.log'))
$result=$LASTEXITCODE
Get-Content (Join-Path $evidence ($Label+'.log')) -Tail 10
if($result -ne 0){throw "Fence adapter regression failed: $result"}
