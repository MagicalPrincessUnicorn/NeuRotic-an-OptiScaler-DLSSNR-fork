param([switch]$ExpectMissing)
$ErrorActionPreference='Stop'
$repo=(Resolve-Path -LiteralPath (Join-Path $PSScriptRoot '..')).Path
$output=Join-Path $repo 'builds/vulkan-experiment/tests/application-present'
New-Item -ItemType Directory -Path $output -Force | Out-Null
$command=Join-Path $output 'run.cmd'
@('@echo off','call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" >nul','if errorlevel 1 exit /b 2',
"cd /d `"$repo`"", "cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests/vulkan_application_present.cpp /Fe:`"$output/test.exe`" /Fo:`"$output/test.obj`"",'if errorlevel 1 exit /b 2',"`"$output/test.exe`"") | Set-Content -LiteralPath $command -Encoding ascii
& cmd.exe /d /c "`"$command`""
$result=$LASTEXITCODE
$object=Join-Path $output 'test.obj'
if(Test-Path -LiteralPath $object){Remove-Item -LiteralPath $object}
if($ExpectMissing){if($result -ne 1){throw "Expected missing association failure, got $result"}}
elseif($result -ne 0){throw "Application Present test failed: $result"}
