@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\interop-safety\tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_bridge_outcome.cpp /Fe:"%OUT%\outcome.exe" /Fo:"%OUT%\outcome.obj" >"%OUT%\outcome-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\outcome.exe" >"%OUT%\outcome-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\outcome-test.log"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_bridge_parameters.cpp /Fe:"%OUT%\parameters.exe" /Fo:"%OUT%\parameters.obj" >"%OUT%\parameters-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\parameters.exe" >"%OUT%\parameters-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\parameters-test.log"
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr\spectre\native_parameter_restore.cpp /Fe:"%OUT%\native-restore.exe" /Fo:"%OUT%\native-restore.obj" >"%OUT%\native-restore-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\native-restore.exe" >"%OUT%\native-restore-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\native-restore-test.log"
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_bridge_retirement.cpp /Fe:"%OUT%\retirement.exe" /Fo:"%OUT%\retirement.obj" /link d3d11.lib d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%OUT%\retirement-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\retirement.exe" >"%OUT%\retirement-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\retirement-test.log"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_bridge_telemetry.cpp /Fe:"%OUT%\telemetry.exe" /Fo:"%OUT%\telemetry.obj" >"%OUT%\telemetry-build.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
"%OUT%\telemetry.exe" >"%OUT%\telemetry-test.log" 2>&1
if not "!ERRORLEVEL!"=="0" goto failed
type "%OUT%\telemetry-test.log"
popd
exit /b 0
:failed
set "RESULT=%ERRORLEVEL%"
for %%N in (outcome parameters native-restore retirement telemetry) do if exist "%OUT%\%%N-build.log" type "%OUT%\%%N-build.log"
for %%N in (parameters native-restore retirement telemetry) do if exist "%OUT%\%%N-test.log" type "%OUT%\%%N-test.log"
if exist "%OUT%\outcome-test.log" type "%OUT%\outcome-test.log"
popd
exit /b %RESULT%
