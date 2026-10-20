@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "testOut=builds\moonshot-w03-reconciliation\present-live-settings"
if not exist "%testOut%" mkdir "%testOut%"
cl /nologo /std:c++20 /EHsc /W4 /WX %* tests\nr_present_live_settings.cpp /Fe:"%testOut%\test.exe" /Fo:"%testOut%\test.obj" /link d3d12.lib dxgi.lib >"%testOut%\build.log" 2>&1
if errorlevel 1 (type "%testOut%\build.log" & popd & exit /b 1)
"%testOut%\test.exe" >"%testOut%\test.log" 2>&1
set "result=%ERRORLEVEL%"
type "%testOut%\test.log"
popd
exit /b %result%
