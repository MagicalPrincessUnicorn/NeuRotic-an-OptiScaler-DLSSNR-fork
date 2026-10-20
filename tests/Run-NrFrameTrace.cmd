@echo off
setlocal EnableDelayedExpansion
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
set "testOut=%~dp0..\builds\ui-release\frame-trace"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler tests\nr_frame_trace.cpp /Fe:%testOut%\nr_frame_trace_test.exe /Fo:%testOut%\nr_frame_trace_test.obj
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
%testOut%\nr_frame_trace_test.exe
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
cl /nologo /std:c++20 /utf-8 /EHsc /W4 /DSPDLOG_USE_STD_FORMAT /I OptiScaler /I external\spdlog\include tests\nr_frame_trace_runtime.cpp /Fe:%testOut%\nr_frame_trace_runtime_test.exe /Fo:%testOut%\nr_frame_trace_runtime_test.obj
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
for %%m in (off invalid invalid-trigger invalid-profile on delayed association) do (
    %testOut%\nr_frame_trace_runtime_test.exe %%m
    if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
)
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
