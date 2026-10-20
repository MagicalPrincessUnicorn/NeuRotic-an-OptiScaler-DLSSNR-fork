@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
set VSTEL_EWDK=1
set "OUT=%~dp0..\builds\universal-guides\tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%~dp0..\OptiScaler" "%~dp0prepared_route.cpp" "%~dp0..\OptiScaler\dlssnr\connections\ConnectionPolicy.cpp" /Fe:"%OUT%\route.exe" /Fo:"%OUT%\\" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
set NEUROTIC_PREPARED_GUIDES=
"%OUT%\route.exe"
if errorlevel 1 exit /b 1
set NEUROTIC_PREPARED_GUIDES=10
"%OUT%\route.exe"
if errorlevel 1 exit /b 1
set NEUROTIC_PREPARED_GUIDES=1
"%OUT%\route.exe" selected
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%~dp0..\OptiScaler" "%~dp0prepared_guide_normalization.cpp" /Fe:"%OUT%\prepared.exe" /Fo:"%OUT%\prepared.obj" /link /INCREMENTAL:NO
if errorlevel 1 exit /b 1
"%OUT%\prepared.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /wd4324 /I"%~dp0..\OptiScaler" "%~dp0prepared_guide_observation.cpp" /Fe:"%OUT%\observation.exe" /Fo:"%OUT%\observation.obj" /link /INCREMENTAL:NO /STACK:8388608 ole32.lib
if errorlevel 1 exit /b 1
"%OUT%\observation.exe"
exit /b %errorlevel%
