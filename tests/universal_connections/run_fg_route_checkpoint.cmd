@echo off
setlocal
cd /d "%~dp0..\.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
set "OUT=builds\universal-input\pipeline-tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\universal_connections\fg_route_checkpoint.cpp /Fo:"%OUT%\fg-route.obj" /Fe:"%OUT%\fg-route.exe"
if errorlevel 1 exit /b 1
"%OUT%\fg-route.exe"
exit /b %errorlevel%
