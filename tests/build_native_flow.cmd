@echo off
setlocal
if /I "%~1"=="Debug" goto debug
call "%~dp0prepared_flow_backends_build.cmd"
exit /b %errorlevel%
:debug
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "NR_NATIVE_CMAKE=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
set "NR_NATIVE_NINJA=C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\Ninja\ninja.exe"
"%NR_NATIVE_CMAKE%" -S "%~dp0..\addons\prepared-guides\flow" -B "%~dp0..\builds\universal-guides\flow-debug" -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_MAKE_PROGRAM="%NR_NATIVE_NINJA%"
if errorlevel 1 exit /b 1
"%NR_NATIVE_CMAKE%" --build "%~dp0..\builds\universal-guides\flow-debug" --target neurotic_prepared_flow --parallel 4
exit /b %errorlevel%
