@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
set "dispatchOut=%~dp0..\builds\ui-release\dispatch-resources"
if not exist "%dispatchOut%" mkdir "%dispatchOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler tests\nr_dispatch_resources.cpp /Fe:"%dispatchOut%\test.exe" /Fo:"%dispatchOut%\test.obj" /link d3d12.lib dxgi.lib
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%dispatchOut%\test.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
