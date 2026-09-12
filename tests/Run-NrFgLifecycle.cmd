@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DSPDLOG_USE_STD_FORMAT /I OptiScaler /I external\spdlog\include tests\nr_fg_lifecycle.cpp /Fe:"%~1\nr_fg_lifecycle.exe" /Fo:"%~1\nr_fg_lifecycle.obj" /link d3d12.lib dxgi.lib
if errorlevel 1 (popd & exit /b 1)
for %%m in (off missing-directory on dred-unavailable dred) do (
    "%~1\nr_fg_lifecycle.exe" %%m "%~1"
    if errorlevel 1 (popd & exit /b 1)
)
popd
exit /b 0
