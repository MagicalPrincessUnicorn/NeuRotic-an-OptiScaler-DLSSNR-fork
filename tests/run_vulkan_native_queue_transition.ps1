$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/native-queue-transition'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-return-165420-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$first=$source.IndexOf('static bool PrepareNativeQueueTransitionLocked(')
$last=$source.IndexOf('static VkRecordResult EvaluateVkChain(',$first)
if($first -lt 0 -or $last -le $first){throw 'Production Native queue transition helper missing'}
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'vulkan_native_queue_transition.cpp.in')
[IO.File]::WriteAllText((Join-Path $output 'fixture.cpp'),$template.Replace('// PRODUCTION_TRANSITION',$source.Substring($first,$last-$first)))
$includes='/I "'+$root+'/OptiScaler" /I "'+$root+'/external/vulkan/include"'
$production='"'+$root+'/OptiScaler/dlssnr/VulkanNrRecording.cpp"'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',('cl /nologo /std:c++20 /EHsc /W4 /WX '+$includes+' fixture.cpp '+$production+' /Fe:fixture.exe'),'if errorlevel 1 exit /b 1','fixture.exe','exit /b %errorlevel%'))
Push-Location $output
try{
 $log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$result=$LASTEXITCODE
 [IO.File]::WriteAllText((Join-Path $evidence 'native-queue-transition.log'),$log);Write-Output $log
 if($result -ne 0){throw "Native queue transition fixture failed: $result"}
}finally{Pop-Location}
