@echo off
setlocal
if not exist "%~1\color_operation_calls.inc" exit /b 2
if not exist "%~2\vulkan\vulkan.h" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /wd4100 /wd4324 /I OptiScaler /I "%~1" /I "%~2" tests\nr_color_operation.cpp /Fo:"%~1\nr_color_operation.obj" /Fe:"%~1\nr_color_operation.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\nr_color_operation.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
