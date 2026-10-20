param([switch]$Gpu)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/native-output'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$before=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$source=Get-Content -Raw -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$first=$source.IndexOf('        const bool deliverySkipped=')
if($first -lt 0){$first=$source.IndexOf('        bool delivered=(((base.captureOnly')}
$last=$source.IndexOf('        if(delivered&&before&&root.generations',$first)
if($first -lt 0 -or $last -le $first){throw 'Actual final delivery extraction anchors missing'}
$source.Substring($first,$last-$first) | Set-Content -LiteralPath (Join-Path $output 'native_output_actual.inc') -Encoding utf8
$extra=''
$options=''
$link=''
$testSource='tests/vulkan_nr_native_output.cpp'
if($Gpu){
 $testSource='tests/vulkan_nr_native_output_gpu.cpp'
 $options='/DNR_VK_GPU_FIXTURE /wd4324 /I tests/fixtures/vulkan_shader'
 $extra='OptiScaler/shaders/Shader_Vk.cpp OptiScaler/shaders/dlssnr/DlssNr_Vk.cpp OptiScaler/dlssnr/VulkanNrExposureScan.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp OptiScaler/dlssnr/VulkanNrCompletion.cpp OptiScaler/dlssnr/VulkanPresentRegistry.cpp OptiScaler/dlssnr/VulkanNrSession.cpp OptiScaler/dlssnr/VulkanNrRuntime.cpp'
 $link='/link OptiScaler/library/vulkan/vulkan-1.lib'
}
$name=if($Gpu){'native-output-gpu'}else{'native-output'}
$cmd=Join-Path $output "$name.cmd"
$icd=Join-Path $repo 'builds/present-multipass-release/probe-swiftshader.json'
@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul',
 'if errorlevel 1 exit /b 1',"cd /d `"$repo`"",
 "cl /nologo /std:c++20 /EHsc /W4 /WX $options /I `"$output`" /I OptiScaler /I OptiScaler/include /I external/nvngx_dlss_sdk /I external/vulkan/include $testSource $extra /Fe:`"$output/$name.exe`" /Fo:`"$($output -replace '\\','/')/`" $link",
 'if errorlevel 1 exit /b 1',"set `"VK_ICD_FILENAMES=$icd`"","set `"VK_DRIVER_FILES=$icd`"","`"$output/$name.exe`"") | Set-Content -LiteralPath $cmd -Encoding ascii
& cmd.exe /d /c "`"$cmd`""
$result=$LASTEXITCODE
$peak=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$resolved=[IO.Path]::GetFullPath($output)
if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/vulkan-experiment/tests/native-output'))){throw 'Fixture cleanup directory mismatch'}
Get-ChildItem -LiteralPath $resolved -File -Filter '*.obj' | Remove-Item
$after=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
Write-Output "Native output fixture bytes before=$before peak=$peak after=$after"
if($result -ne 0){throw "Native output regression failed: $result"}
