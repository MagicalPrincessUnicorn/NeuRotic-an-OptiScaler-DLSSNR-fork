@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\opticlip-controller"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\opticlip_controller.cpp /Fe:"%testOut%\opticlip_controller.exe" /Fo:"%testOut%\opticlip_controller.obj" >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\opticlip_controller.exe" >"%testOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
type "%testOut%\test.txt" 2>nul
popd
exit /b %testResult%
