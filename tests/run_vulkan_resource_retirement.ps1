param([string]$Label='resource-retirement-green')
$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$output=Join-Path $repo 'builds/vulkan-experiment/tests/resource-retirement'
$evidence=Join-Path $repo 'docs/implementation-evidence/vulkan-adversarial-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$start=$source.IndexOf('thread_local VkState* g_activeVk =');$end=$source.IndexOf('uint64_t GenerationBytesVk()',$start)
$scope=$source.Substring($start,$end-$start)
$start=$source.IndexOf('static void RetireVkPassGenerationsLocked(')
if($start -ge 0){$end=$source.IndexOf('// End pass generation retirement',$start);$retire=$source.Substring($start,$end-$start)}else{
    $old=[regex]::Match($source,'\{std::lock_guard lock\(g_vkMutex\);for\(uint32_t pass=static_cast<uint32_t>\(plan.passes.size\(\)\);pass<10;\+\+pass\)\{.*?\}\}\}')
    if(!$old.Success){throw 'Production child retirement boundary not found'}
    # Execute the exact old child cleanup body while the fixture owns its normal state lock.
    $body=$old.Value.Replace('{std::lock_guard lock(g_vkMutex);','{').Replace('static_cast<uint32_t>(plan.passes.size())','firstPass').Replace('base.route','route')
    $retire='static void RetireVkPassGenerationsLocked(VkNrRoute route,uint32_t firstPass,std::unique_lock<std::mutex>&)'+$body
}
$first=$source.IndexOf('static bool PrepareVkRouteTransitionLocked(')
if($first -ge 0){$last=$source.IndexOf('// End route transition',$first);$handoff=$source.Substring($first,$last-$first)}else{
    # Before hardening the chain allocates without a predecessor route drain.
    $handoff='static bool PrepareVkRouteTransitionLocked(VkDevice,VkNrRoute,std::unique_lock<std::mutex>&,uint64_t){return true;}'
}
$template=Get-Content -Raw -LiteralPath (Join-Path $PSScriptRoot 'vulkan_resource_retirement.cpp.in')
$start=$source.IndexOf('void RetireInactiveVk(');$end=$source.IndexOf('void DeviceDestroyedVk(',$start)
$inactive=$source.Substring($start,$end-$start)
[IO.File]::WriteAllText((Join-Path $output 'retirement.cpp'),$template.Replace('// PRODUCTION_SCOPE',$scope).Replace('// PRODUCTION_RETIREMENT',$retire).Replace('// PRODUCTION_INACTIVE',$inactive).Replace('// PRODUCTION_HANDOFF',$handoff))
$compile='cl /nologo /std:c++20 /EHsc /O2 /W4 /I"'+$repo+'/OptiScaler" /I"'+$repo+'/external/vulkan/include" /I"'+$repo+'/external/nvngx_dlss_sdk" retirement.cpp "'+$repo+'/OptiScaler/dlssnr/VulkanNrGeneration.cpp" "'+$repo+'/OptiScaler/dlssnr/VulkanNrRecording.cpp" "'+$repo+'/OptiScaler/dlssnr/VulkanNrPreFg.cpp" /Fe:retirement.exe /Fo:"'+$output+'/" /link /INCREMENTAL:NO'
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',$compile,'if errorlevel 1 exit /b 1','retirement.exe','exit /b %errorlevel%'))
Push-Location $output
try {$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$code=$LASTEXITCODE;[IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log;if($code -ne 0){throw "Retirement regression failed: $code"}} finally {Pop-Location}
