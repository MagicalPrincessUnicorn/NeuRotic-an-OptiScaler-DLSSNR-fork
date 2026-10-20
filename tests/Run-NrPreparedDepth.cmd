@echo off
setlocal EnableDelayedExpansion
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\prepared-depth\tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "!ERRORLEVEL!"=="0" exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_prepared_depth.cpp /Fe:"%OUT%\prepared-depth.exe" /Fo:"%OUT%\prepared-depth.obj" /link d3d12.lib dxgi.lib OptiScaler\library\detours\detours.lib
if not "!ERRORLEVEL!"=="0" (popd & exit /b 1)
"%OUT%\prepared-depth.exe"
set "RESULT=%ERRORLEVEL%"
if not "%RESULT%"=="0" (popd & exit /b %RESULT%)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File tests\Test-NrPreparedDepth.ps1
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
