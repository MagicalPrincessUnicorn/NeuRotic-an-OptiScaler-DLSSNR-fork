@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-present-stage-capture"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_present_stage_capture.cpp /Fe:"%testOut%\nr_present_stage_capture.exe" /Fo:"%testOut%\nr_present_stage_capture.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\nr_present_stage_capture.exe" "%testOut%\captures" >"%testOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testResult%
