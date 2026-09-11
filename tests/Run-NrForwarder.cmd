@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 tests\nr_forwarder_lifecycle.cpp /Fe:C:\OptiScaler-NR-Dev\logs\nr_forwarder_lifecycle.exe /Fo:C:\OptiScaler-NR-Dev\logs\nr_forwarder_lifecycle.obj
if errorlevel 1 (popd & exit /b 1)
C:\OptiScaler-NR-Dev\logs\nr_forwarder_lifecycle.exe
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
