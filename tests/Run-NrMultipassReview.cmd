@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-multipass-review-fixes"
if not exist "%testOut%" mkdir "%testOut%"
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_multipass_display.cpp /Fe:"%testOut%\display.exe" /Fo:"%testOut%\display.obj" /link d3d11.lib >"%testOut%\display-build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\display.exe" >"%testOut%\display-test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\display-test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\display-build.txt"
type "%testOut%\display-test.txt"
popd
exit /b %testResult%
