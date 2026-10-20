$ErrorActionPreference='Stop'
$repo=Split-Path $PSScriptRoot -Parent
$out=Join-Path $repo 'builds/vulkan-experiment/tests/memory-headroom'
New-Item -ItemType Directory -Force $out | Out-Null
[IO.File]::WriteAllLines((Join-Path $out 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul',('cl /nologo /std:c++20 /EHsc /W4 /WX /I"'+$repo+'/OptiScaler" /I"'+$repo+'/external/vulkan/include" "'+$repo+'/tests/vulkan_memory_headroom.cpp" /Fo:"'+$out+'/" /Fe:"'+$out+'/test.exe"'),'if errorlevel 1 exit /b 1',('"'+$out+'/test.exe"')))
& cmd.exe /d /c "`"$out/run.cmd`""
if($LASTEXITCODE -ne 0){throw 'Memory headroom regression failed'}
