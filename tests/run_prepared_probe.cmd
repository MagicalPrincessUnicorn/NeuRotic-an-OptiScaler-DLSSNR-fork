@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set CL=
set _CL_=
set VSTEL_EWDK=1
set "OUT=%~dp0..\builds\universal-guides\probe-tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%~dp0..\OptiScaler" "%~dp0prepared_content_probe.cpp" /Fe:"%OUT%\prepared_probe.exe" /Fo:"%OUT%\prepared_probe.obj" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\prepared_probe.exe"
if not "%errorlevel%"=="0" exit /b 1
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%~dp0..\OptiScaler" "%~dp0prepared_content_probe.cpp" /Fe:"%OUT%\prepared_probe32.exe" /Fo:"%OUT%\prepared_probe32.obj" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b 1
"%OUT%\prepared_probe32.exe"
exit /b %errorlevel%
