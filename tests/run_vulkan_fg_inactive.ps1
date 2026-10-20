param([string]$Label='fg-inactive-green',[switch]$Preimage)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$output=Join-Path $root 'builds/vulkan-experiment/tests/fg-inactive'
$evidence=Join-Path $root 'docs/implementation-evidence/vulkan-adversarial-20261002'
New-Item -ItemType Directory -Force -Path $output,$evidence | Out-Null
$source=if($Preimage){& git -C $root show 'dc30f2f9:OptiScaler/hooks/Vulkan_Hooks.cpp' | Out-String}else{Get-Content -Raw (Join-Path $root 'OptiScaler/hooks/Vulkan_Hooks.cpp')}
$start=$source.IndexOf('        if(const auto settings=TryNrConfigSnapshot(*Config::Instance());')
$block=''
if($start -ge 0){$end=$source.IndexOf('        bool originalCalled=false;', $start);$block=$source.Substring($start,$end-$start)}elseif(!$Preimage){throw 'Current FG maintenance block missing'}
$template=Get-Content -Raw (Join-Path $PSScriptRoot 'vulkan_fg_inactive.cpp.in')
[IO.File]::WriteAllText((Join-Path $output 'fixture.cpp'),$template.Replace('// PRODUCTION_FG_MAINTENANCE',$block))
[IO.File]::WriteAllLines((Join-Path $output 'run.cmd'),@('@echo off','call "C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat" -vcvars_ver=14.44 >nul','if errorlevel 1 exit /b 1',('cl /nologo /std:c++20 /EHsc /W4 /I "'+$root+'/external/vulkan/include" fixture.cpp /Fe:fixture.exe /Fo:fixture.obj'),'if errorlevel 1 exit /b 1','fixture.exe','exit /b %errorlevel%'))
Push-Location $output
try{$ErrorActionPreference='Continue';$log=(& cmd.exe /d /c run.cmd 2>&1 | Out-String);$result=$LASTEXITCODE;$ErrorActionPreference='Stop';[IO.File]::WriteAllText((Join-Path $evidence ($Label+'.log')),$log);Write-Output $log;if($result -ne 0){throw "FG inactive fixture failed: $result"}}finally{Pop-Location}
