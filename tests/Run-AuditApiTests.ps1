param([string]$Python='python')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$out=Join-Path $repo 'builds/audit-api'
New-Item -ItemType Directory -Force -Path $out | Out-Null
Push-Location $repo
try {
 foreach($generator in 'audit_api_runtime_tests.py','audit_gpu_time_tests.py','audit_vulkan_metadata_tests.py','audit_dispatch_gate_tests.py') {
  & $Python (Join-Path $PSScriptRoot $generator)
  if($LASTEXITCODE -ne 0){throw "Generator failed: $generator"}
 }
 $commands=@(
  '@echo off',
  'call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul',
  'if errorlevel 1 exit /b 1'
 )
 $fixtures=@(
  @('api-contracts','builds/audit-api/api-contracts.cpp',''),
  @('dispatch-gate','builds/audit-api/dispatch-gate.cpp',''),
  @('gpu-time','builds/audit-api/gpu-time.cpp',''),
  @('guide-metadata','builds/audit-api/guide-metadata.cpp','/I external/vulkan/include'),
  @('pool-identity','tests/vulkan_pool_device_identity.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp','/I OptiScaler /I external/vulkan/include'),
  @('owned-frame-ring','tests/window-worker/OwnedFrameRingTests.cpp','')
 )
 foreach($fixture in $fixtures) {
  $commands+='cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX '+$fixture[2]+' '+$fixture[1]+' /Fo:builds/audit-api/ /Fe:builds/audit-api/'+$fixture[0]+'.exe'
  $commands+='if errorlevel 1 exit /b 1'
  $commands+='builds\audit-api\'+$fixture[0]+'.exe'
  $commands+='if errorlevel 1 exit /b 1'
 }
 $commands+='exit /b 0'
 $commands | Set-Content -LiteralPath (Join-Path $out 'run-focused.cmd')
 & cmd.exe /d /c builds\audit-api\run-focused.cmd
 if($LASTEXITCODE -ne 0){throw "API fixture failed: $LASTEXITCODE"}
} finally {Pop-Location}
