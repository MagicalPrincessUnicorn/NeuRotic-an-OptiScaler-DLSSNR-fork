$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/native-guides/acquisition'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$vulkanInclude=$env:NR_TEST_VULKAN_INCLUDE
if(-not $vulkanInclude){$vulkanInclude=Join-Path $repo 'external/vulkan/include'}
if(-not(Test-Path -LiteralPath (Join-Path $vulkanInclude 'vulkan/vulkan.h'))){$vulkanInclude=Join-Path $repo '../flagship-all-merges/external/vulkan/include'}
if(-not(Test-Path -LiteralPath (Join-Path $vulkanInclude 'vulkan/vulkan.h'))){throw 'Set NR_TEST_VULKAN_INCLUDE to the existing Vulkan headers; no dependency copying is performed'}
$vcvars=$env:NR_TEST_VCVARS
if(-not $vcvars){$vcvars='C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'}
$icd=$env:NR_TEST_VULKAN_ICD
if(-not $icd){$icd=Join-Path $repo '../_deps/legacy-ui-20261003/probe-swiftshader.json'}
if(-not(Test-Path -LiteralPath $icd)){throw 'Set NR_TEST_VULKAN_ICD to an existing ICD JSON; no runtime download is performed'}
$driver=(Get-Content -LiteralPath $icd -Raw|ConvertFrom-Json).ICD.library_path
if(-not(Test-Path -LiteralPath $driver)){throw 'Preserved ICD runtime is unavailable'}
$compile=Join-Path $output 'compile-fixture.cmd'
@(
 '@echo off'
 "call `"$vcvars`" >nul"
 'if errorlevel 1 exit /b 1'
 "cd /d `"$repo`""
 "cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I OptiScaler /I OptiScaler/include /I C:/NeuRotic/Optiscaler-Stuff/external/nvngx_dlss_sdk /I `"$vulkanInclude`" tests/native_vulkan_guides_gpu.cpp OptiScaler/dlssnr/connections/ConnectionPolicy.cpp OptiScaler/dlssnr/connections/ConnectionSelection.cpp OptiScaler/dlssnr/PreparedGuideStatusV2.cpp OptiScaler/dlssnr/NativeFgVulkan.cpp OptiScaler/dlssnr/PreparedVkDx12Transport.cpp OptiScaler/dlssnr/VulkanNrImageFacts.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp OptiScaler/dlssnr/VulkanNrCompletion.cpp OptiScaler/dlssnr/VulkanPresentRegistry.cpp OptiScaler/dlssnr/VulkanNrSession.cpp OptiScaler/dlssnr/VulkanNrRuntime.cpp /Fe:`"$output/native-vulkan-guides-gpu.exe`" /Fo:`"$($output -replace '\\','/')/`" /link C:/NeuRotic/Optiscaler-Stuff/OptiScaler/library/vulkan/vulkan-1.lib user32.lib d3d12.lib dxgi.lib"
) | Set-Content -LiteralPath $compile -Encoding ASCII
$receipt=Join-Path $output 'verification.log'
$savedIcd=$env:VK_ICD_FILENAMES;$savedDriver=$env:VK_DRIVER_FILES
$result=1
try {
 & cmd.exe /d /c "`"$compile`"" 2>&1 | Tee-Object -FilePath $receipt
 if($LASTEXITCODE -ne 0){throw 'Native Vulkan fixture compilation failed'}
 $env:VK_ICD_FILENAMES=$icd;$env:VK_DRIVER_FILES=$icd
 & (Join-Path $output 'native-vulkan-guides-gpu.exe') 2>&1 | Tee-Object -FilePath $receipt -Append
 $result=$LASTEXITCODE
 if($result -eq 0){
  foreach($policy in @(@('1','0'),@('0','0'),@('2','0'),@('0','1','0'),@('1','0','0'),@('2','0','0'),@('0','0','0','1'),@('0','1','1'),@('0','1','3'),@('0','1','4'))){
   & (Join-Path $output 'native-vulkan-guides-gpu.exe') @policy 2>&1 | Tee-Object -FilePath $receipt -Append
   $result=$LASTEXITCODE;if($result -ne 0){break}
  }
 }
} finally {
 $env:VK_ICD_FILENAMES=$savedIcd;$env:VK_DRIVER_FILES=$savedDriver
 $resolved=[IO.Path]::GetFullPath($output)
 if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/native-guides/acquisition'))){throw 'Fixture cleanup target mismatch'}
 # Only this runner's compiler intermediates; keep executable, source and receipt.
 Get-ChildItem -LiteralPath $resolved -File -Filter '*.obj' | ForEach-Object {Remove-Item -LiteralPath $_.FullName}
}
if($result -eq 77){throw 'Native Vulkan fixture unavailable; see verification.log. This is not a passing runtime result.'}
if($result -ne 0){throw "Native Vulkan fixture failed: $result"}
