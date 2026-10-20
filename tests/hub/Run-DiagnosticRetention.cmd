@echo off
setlocal
rem Run only from the canonical checkout after this afterimage is applied.
rem One stable test/compiler directory; no external handoff includes or copied App caches.
pushd "%~dp0..\.."
if errorlevel 1 exit /b 1
set "NR_RETENTION_RESULT=1"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 goto :done
set "NR_RETENTION_OUT=builds\hub\diagnostic-retention"
set "NR_RETENTION_OBJ=builds/hub/diagnostic-retention/obj"
set "NR_RETENTION_PS=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
set "PSModulePath=%SystemRoot%\System32\WindowsPowerShell\v1.0\Modules"
if not exist "%NR_RETENTION_OUT%\obj" mkdir "%NR_RETENTION_OUT%\obj"
if errorlevel 1 goto :done
cl /nologo /std:c++20 /EHsc /MT /O2 /utf-8 /W3 /DUNICODE /D_UNICODE /DNOMINMAX ^
 /I apps\NeuRoticHub /I apps\NeuRoticHub\library /I external\nlohmann /I OptiScaler ^
 tests\hub\DiagnosticRetentionTests.cpp ^
 apps\NeuRoticHub\install\OperationController.cpp ^
 apps\NeuRoticHub\ui\ProfileCache.cpp ^
 apps\NeuRoticHub\library\ManualLibrary.cpp ^
 OptiScaler\menu\Localization.cpp ^
 OptiScaler\menu\localization\LanguageRuntime.cpp ^
 OptiScaler\menu\localization\LanguagePack.cpp ^
 OptiScaler\menu\localization\LanguageCatalog.cpp ^
 OptiScaler\menu\localization\LanguageBundles.cpp ^
 /Fe:"%NR_RETENTION_OUT%\test.exe" /Fo"%NR_RETENTION_OBJ%/" ^
 /link shell32.lib ole32.lib advapi32.lib user32.lib ^
 >"%NR_RETENTION_OUT%\build.txt" 2>&1
set "NR_RETENTION_RESULT=%ERRORLEVEL%"
if not "%NR_RETENTION_RESULT%"=="0" goto :done
rem A failed or interrupted previous run is retained for inspection, never overwritten.
"%NR_RETENTION_PS%" -NoProfile -Command "$ErrorActionPreference='Stop'; $fixture=[IO.Path]::GetFullPath((Join-Path (Get-Location) 'builds/hub/diagnostic-retention/fixture')); if(Test-Path -LiteralPath $fixture){throw 'Prior retention fixture must be inspected and safely cleared before rerunning.'}; $target=Join-Path $fixture 'junction-target'; $link=Join-Path $fixture 'junction'; New-Item -ItemType Directory -Path $target | Out-Null; New-Item -ItemType Junction -Path $link -Target $target | Out-Null"
set "NR_RETENTION_RESULT=%ERRORLEVEL%"
if not "%NR_RETENTION_RESULT%"=="0" goto :done
set "NEUROTIC_HUB_FIXTURE_ROOT=%CD%\%NR_RETENTION_OUT%\fixture"
"%NR_RETENTION_OUT%\test.exe" "%CD%\%NR_RETENTION_OUT%\fixture" >"%NR_RETENTION_OUT%\result.txt" 2>&1
set "NR_RETENTION_RESULT=%ERRORLEVEL%"
type "%NR_RETENTION_OUT%\result.txt"
if not "%NR_RETENTION_RESULT%"=="0" goto :done
rem Success means the fixture observed its exact child exit. Unlink its one known
rem junction non-recursively, refuse all other reparse points, then remove only
rem the resolved fixed fixture path. Logs and the single working cache remain.
"%NR_RETENTION_PS%" -NoProfile -Command "$ErrorActionPreference='Stop'; $workspace=[IO.Path]::GetFullPath((Get-Location).Path); $fixture=[IO.Path]::GetFullPath((Join-Path $workspace 'builds/hub/diagnostic-retention/fixture')); if(-not $fixture.StartsWith($workspace.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Fixture escapes workspace'}; for($walk=$fixture;$walk;$walk=[IO.Path]::GetDirectoryName($walk)){if(([IO.File]::GetAttributes($walk) -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Linked fixture ancestor refused'}}; $link=Join-Path $fixture 'junction'; if(Test-Path -LiteralPath $link){if(([IO.File]::GetAttributes($link) -band [IO.FileAttributes]::ReparsePoint) -eq 0){throw 'Unexpected ordinary junction path'}; [IO.Directory]::Delete($link)}; $pending=New-Object 'Collections.Generic.Stack[string]'; $pending.Push($fixture); while($pending.Count -gt 0){$folder=$pending.Pop(); foreach($entry in Get-ChildItem -LiteralPath $folder -Force){if(($entry.Attributes -band [IO.FileAttributes]::ReparsePoint) -ne 0){throw 'Unexpected fixture reparse point'}; if($entry.PSIsContainer){$pending.Push($entry.FullName)}}}; Remove-Item -LiteralPath $fixture -Recurse -Force"
if errorlevel 1 set "NR_RETENTION_RESULT=1"
:done
popd
exit /b %NR_RETENTION_RESULT%
