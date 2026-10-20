@echo off
setlocal
rem Args: prepared baseline directory, actual existing Vulkan SDK include directory.
if not exist "%~1\temporal_baseline.inc" exit /b 2
if not exist "%~1\temporal_live_calls.inc" exit /b 2
if not exist "%~2\vulkan\vulkan.h" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
rem Common's shader constants intentionally contain 256-byte alignment padding.
cl /nologo /std:c++20 /EHsc /W4 /WX /wd4100 /wd4324 /I OptiScaler /I "%~1" /I "%~2" tests\nr_temporal_consolidation.cpp /Fe:"%~1\nr_temporal_consolidation.exe" /Fo:"%~1\nr_temporal_consolidation.obj"
if errorlevel 1 (popd & exit /b 1)
"%~1\nr_temporal_consolidation.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
