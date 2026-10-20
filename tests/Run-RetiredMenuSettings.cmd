@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "retiredOut=builds\ui-release\retired-menu"
if not exist "%retiredOut%" mkdir "%retiredOut%"
cl /nologo /std:c++20 /EHsc /W3 /I OptiScaler tests\retired_menu_settings.cpp /Fe:"%retiredOut%\test.exe" /Fo:"%retiredOut%\test.obj" >"%retiredOut%\build.log" 2>&1
if errorlevel 1 goto failed
"%retiredOut%\test.exe" >"%retiredOut%\test.log" 2>&1
if errorlevel 1 goto failed
type "%retiredOut%\test.log"
popd
exit /b 0
:failed
set "result=%ERRORLEVEL%"
type "%retiredOut%\build.log"
if exist "%retiredOut%\test.log" type "%retiredOut%\test.log"
popd
exit /b %result%
