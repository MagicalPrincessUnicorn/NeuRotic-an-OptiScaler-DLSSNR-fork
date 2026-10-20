$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/pixel-probe-gpu'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$before=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$source=Get-Content -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/VulkanNrCaptureVk.cpp') -Raw
$start=$source.IndexOf('constexpr VkDeviceSize ProbeImageStride=')
$end=$source.IndexOf('VulkanNrCapture& ProbeOwner()')
if($start -lt 0 -or $end -le $start){throw 'Actual pixel probe extraction anchors missing'}
$body="namespace DlssNr {`nVkCommandBuffer recording=VK_NULL_HANDLE;VkDevice device=VK_NULL_HANDLE;VkPhysicalDevice physical=VK_NULL_HANDLE;`n"+$source.Substring($start,$end-$start)+"`n}`n"
Set-Content -LiteralPath (Join-Path $output 'pixel_probe_actual.inc') -Value $body -Encoding utf8
$icd=Join-Path $repo 'builds/present-multipass-release/probe-swiftshader.json'
$runtime=(Get-Content -LiteralPath $icd -Raw | ConvertFrom-Json).ICD.library_path
if(-not(Test-Path -LiteralPath $runtime)){throw 'Preserved SwiftShader runtime unavailable; no download attempted'}
$cmd=Join-Path $output 'run.cmd'
@(
 '@echo off'
 'call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul'
 'if errorlevel 1 exit /b 1'
 "cd /d `"$repo`""
 "cl /nologo /std:c++20 /EHsc /W4 /WX /wd4324 /I `"$output`" /I OptiScaler /I OptiScaler/include /I external/nvngx_dlss_sdk /I external/vulkan/include tests/vulkan_nr_pixel_probe_gpu.cpp OptiScaler/dlssnr/VulkanNrCapture.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp OptiScaler/dlssnr/VulkanNrCompletion.cpp OptiScaler/dlssnr/VulkanPresentRegistry.cpp OptiScaler/dlssnr/VulkanNrSession.cpp OptiScaler/dlssnr/VulkanNrRuntime.cpp /Fe:`"$output/pixel-probe-gpu.exe`" /Fo:`"$($output -replace '\\','/')/`" /link OptiScaler/library/vulkan/vulkan-1.lib"
 'if errorlevel 1 exit /b 1'
 "set `"VK_ICD_FILENAMES=$icd`""
 "set `"VK_DRIVER_FILES=$icd`""
 "`"$output/pixel-probe-gpu.exe`""
) | Set-Content -LiteralPath $cmd -Encoding ASCII
$receipt=Join-Path $repo 'docs/implementation-evidence/vulkan-reactivation-20261001/pixel-probe-gpu.log'
& cmd.exe /d /c "`"$cmd`"" 2>&1 | Tee-Object -FilePath $receipt
$result=$LASTEXITCODE
$peak=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
# Only this fixture's resolved stable directory; preserve executable, generated source and receipt.
$resolved=[IO.Path]::GetFullPath($output)
if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/vulkan-experiment/tests/pixel-probe-gpu'))){throw 'Fixture cleanup directory mismatch'}
Get-ChildItem -LiteralPath $resolved -File -Filter '*.obj' | Remove-Item
$after=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
"Sparse GPU fixture bytes before=$before peak=$peak after=$after" | Tee-Object -FilePath $receipt -Append
if($result -ne 0){throw "Sparse GPU fixture failed: $result"}
