@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 tests\nr_pre_fg.cpp /Fe:C:\OptiScaler-NR-Dev\logs\nr_pre_fg_test.exe /Fo:C:\OptiScaler-NR-Dev\logs\nr_pre_fg_test.obj /link d3d12.lib dxgi.lib
if errorlevel 1 (popd & exit /b 1)
C:\OptiScaler-NR-Dev\logs\nr_pre_fg_test.exe
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 tests\nr_pre_fg_dxgi.cpp /Fe:C:\OptiScaler-NR-Dev\logs\nr_pre_fg_dxgi_test.exe /Fo:C:\OptiScaler-NR-Dev\logs\nr_pre_fg_dxgi_test.obj /link d3d12.lib dxgi.lib user32.lib
if errorlevel 1 (popd & exit /b 1)
C:\OptiScaler-NR-Dev\logs\nr_pre_fg_dxgi_test.exe
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
