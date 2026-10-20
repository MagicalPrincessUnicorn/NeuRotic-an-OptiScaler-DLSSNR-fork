@echo off
setlocal
if "%~1"=="" exit /b 2
if not exist "%~1\common_invocation_policy.inc" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /I "%~1" /I external\nlohmann tests\streamline\common_lifecycle\common_invocation_policy.cpp /Fo:"%~1\common_invocation_policy.obj" /Fe:"%~1\common_invocation_policy.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\common_invocation_policy.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
