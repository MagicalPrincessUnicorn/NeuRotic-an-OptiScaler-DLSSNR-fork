@echo off
setlocal
set "NR_FLOW_SOURCE=%~dp0..\addons\prepared-guides\flow"
set "NR_FLOW_BUILD=%~dp0..\builds\universal-guides\flow"
set "NR_FLOW_CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NR_FLOW_NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
"%NR_FLOW_CMAKE%" -S "%NR_FLOW_SOURCE%" -B "%NR_FLOW_BUILD%" -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_MAKE_PROGRAM="%NR_FLOW_NINJA%"
if errorlevel 1 exit /b 1
"%NR_FLOW_CMAKE%" --build "%NR_FLOW_BUILD%" --target prepared_flow_backends --parallel 4
if errorlevel 1 exit /b 1
"%NR_FLOW_BUILD%\prepared_flow_backends.exe"
exit /b %errorlevel%
