$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/meter-readback'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$before=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$source=Get-Content -Raw -Encoding UTF8 -LiteralPath (Join-Path $repo 'OptiScaler/dlssnr/DlssNrFeature_Vk.cpp')
$first=$source.IndexOf('        if (!ActiveVk().meterPublished[slot]')
$last=$source.IndexOf('        if (ActiveVk().queryPool &&',$first)
if($first -lt 0 -or $last -le $first){throw 'Actual meter readback source anchors missing'}
$source.Substring($first,$last-$first) | Set-Content -LiteralPath (Join-Path $output 'meter_readback_actual.inc') -Encoding utf8
$cmd=Join-Path $output 'run.cmd'
@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul',
 'if errorlevel 1 exit /b 1',"cd /d `"$repo`"",
 "cl /nologo /std:c++20 /EHsc /W4 /WX /wd4324 /I `"$output`" /I OptiScaler /I external/nvngx_dlss_sdk /I external/vulkan/include tests/vulkan_nr_meter_readback.cpp OptiScaler/dlssnr/VulkanNrExposureScan.cpp OptiScaler/dlssnr/VulkanNrRecording.cpp /Fe:`"$output/meter-readback.exe`" /Fo:`"$($output -replace '\\','/')/`"",
 'if errorlevel 1 exit /b 1',"`"$output/meter-readback.exe`"") | Set-Content -LiteralPath $cmd -Encoding ascii
& cmd.exe /d /c "`"$cmd`""
$result=$LASTEXITCODE
$peak=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
$resolved=[IO.Path]::GetFullPath($output)
if($resolved -ne [IO.Path]::GetFullPath((Join-Path $repo 'builds/vulkan-experiment/tests/meter-readback'))){throw 'Fixture cleanup directory mismatch'}
Get-ChildItem -LiteralPath $resolved -File -Filter '*.obj' | Remove-Item
$after=(Get-ChildItem -LiteralPath $output -File | Measure-Object Length -Sum).Sum
Write-Output "Meter readback fixture bytes before=$before peak=$peak after=$after"
if($result -ne 0){throw "Meter readback regression failed: $result"}
