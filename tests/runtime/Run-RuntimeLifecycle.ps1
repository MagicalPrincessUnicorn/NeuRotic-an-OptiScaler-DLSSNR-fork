param([string]$OutputDirectory='builds/core-runtime-audit/vendor-contracts')
$ErrorActionPreference='Stop'
$root=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$out=Join-Path $root $OutputDirectory
New-Item -ItemType Directory -Force $out | Out-Null
$source=Get-Content -Raw (Join-Path $root 'OptiScaler/dllmain.cpp')
$detachStart=$source.IndexOf('if (ul_reason_for_call == DLL_PROCESS_DETACH)')
$detachEnd=$source.IndexOf('#if', $detachStart)
$detach=$source.Substring($detachStart,$detachEnd-$detachStart)
foreach($forbidden in @('LoadAsiPlugins(', 'CheckQuirks(', 'PrepareLogger(', 'CloseLogger(', 'FreeLibrary_Ldr(')) {if($detach.Contains($forbidden)){throw "Loader detach still calls $forbidden"}}
# Normal provider attach remains synchronous pending an ABI-safe first-call bootstrap.
# This fixture proves process pinning and the ASI worker, not loader-free provider attach.
$cmd=@'
@echo off
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /LD /I "SOURCE_ROOT\OptiScaler" "SOURCE_ROOT\tests\runtime\lifecycle_plugin.cpp" /Fe:lifecycle-plugin.dll /Fo:lifecycle-plugin.obj
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX "SOURCE_ROOT\tests\runtime\lifecycle_host.cpp" /Fe:lifecycle-host.exe /Fo:lifecycle-host.obj
if errorlevel 1 exit /b 1
lifecycle-host.exe
'@
Set-Content (Join-Path $out 'run-lifecycle.cmd') $cmd.Replace('SOURCE_ROOT',$root)
Push-Location $out
try {& cmd.exe /c run-lifecycle.cmd;$result=$LASTEXITCODE} finally {Pop-Location;foreach($name in @('lifecycle-host.obj','lifecycle-plugin.obj','lifecycle-plugin.exp','lifecycle-plugin.lib')){Remove-Item (Join-Path $out $name) -ErrorAction SilentlyContinue}}
exit $result
