@echo off
setlocal
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0Test-NrConfigCoverage.ps1"
if not "%ERRORLEVEL%"=="0" exit /b 1
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
set "nrTestOut=%~dp0..\builds\capability-facts\nr-config"
if not exist "%nrTestOut%" mkdir "%nrTestOut%"
if not exist "%nrTestOut%" exit /b 1
pushd "%~dp0.."
if not "%ERRORLEVEL%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX tests\nr_config.cpp /Fe:"%nrTestOut%\nr_config.exe" /Fo:"%nrTestOut%\nr_config.obj" >"%nrTestOut%\compile.log" 2>&1
set "nrCompileExit=%ERRORLEVEL%"
type "%nrTestOut%\compile.log"
if not "%nrCompileExit%"=="0" (popd & exit /b %nrCompileExit%)
"%nrTestOut%\nr_config.exe" >"%nrTestOut%\test.log" 2>&1
set "nrTestExit=%ERRORLEVEL%"
type "%nrTestOut%\test.log"
popd
exit /b %nrTestExit%

