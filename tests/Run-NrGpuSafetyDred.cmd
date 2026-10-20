@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if not exist "%~1" mkdir "%~1"
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DSPDLOG_USE_STD_FORMAT /I OptiScaler /I OptiScaler\include /I external\spdlog\include tests\nr_gpu_safety_dred.cpp /Fe:"%~1\nr_gpu_safety_dred.exe" /Fo:"%~1\nr_gpu_safety_dred.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib
if errorlevel 1 (popd & exit /b 1)
"%~1\nr_gpu_safety_dred.exe" off "%~1\off"
if errorlevel 1 (popd & exit /b 1)
"%~1\nr_gpu_safety_dred.exe" on "%~1\on"
set "result=%errorlevel%"
popd
exit /b %result%
