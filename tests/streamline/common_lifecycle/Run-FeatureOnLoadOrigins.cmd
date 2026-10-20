@echo off
setlocal
if "%~1"=="" exit /b 2
if "%~2"=="" exit /b 2
if not exist "%~1\feature_onload_origins.inc" exit /b 2
if not exist "%~2\NrCommonFixtureA.dll" exit /b 2
if not exist "%~2\NrCommonFixtureB.dll" exit /b 2
set "CONTROL="
if /I "%~3"=="green" set "CONTROL=/D NR_FEATURE_ONLOAD_ORIGINS_GREEN"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" tests\streamline\common_lifecycle\feature_onload_origins.cpp /Fo:"%~1\feature_onload_origins.obj" /Fe:"%~1\feature_onload_origins.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\feature_onload_origins.exe" "%~2"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
