@echo off
setlocal EnableDelayedExpansion
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "OUT=builds\native-mfg\owners"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\dred\DRED.log" del /f /q "%OUT%\dred\DRED.log"
if exist "%OUT%\dred\DRED.log" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 tests\nr_pre_fg.cpp /Fe:"%OUT%\nr_pre_fg.exe" /Fo:"%OUT%\nr_pre_fg.obj" /link d3d12.lib dxgi.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_pre_fg.exe"
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 tests\nr_pre_fg_dxgi.cpp /Fe:"%OUT%\nr_pre_fg_dxgi.exe" /Fo:"%OUT%\nr_pre_fg_dxgi.obj" /link d3d12.lib dxgi.lib user32.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_pre_fg_dxgi.exe"
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_gpu_safety.cpp /Fe:"%OUT%\nr_gpu_safety.exe" /Fo:"%OUT%\nr_gpu_safety.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_gpu_safety.exe"
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DSPDLOG_USE_STD_FORMAT /I OptiScaler /I external\spdlog\include tests\nr_fg_lifecycle.cpp /Fe:"%OUT%\nr_fg_lifecycle.exe" /Fo:"%OUT%\nr_fg_lifecycle.obj" /link d3d12.lib dxgi.lib
if errorlevel 1 (popd & exit /b 1)
for %%m in (off missing-directory on dred-unavailable dred) do (
    "%OUT%\nr_fg_lifecycle.exe" %%m "%OUT%"
    if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
)
popd
exit /b 0
