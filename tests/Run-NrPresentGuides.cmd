@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_present_guides.cpp /Fe:C:\OptiScaler-NR-Dev\logs\nr_present_guides_test.exe /Fo:C:\OptiScaler-NR-Dev\logs\nr_present_guides_test.obj /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib
if errorlevel 1 (popd & exit /b 1)
C:\OptiScaler-NR-Dev\logs\nr_present_guides_test.exe
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
