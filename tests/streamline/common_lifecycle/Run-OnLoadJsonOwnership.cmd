@echo off
setlocal
if "%~1"=="" exit /b 2
set "CONTROL="
if /I "%~2"=="old" (
 if not exist "%~1\old_onload_json_return.inc" exit /b 2
 set "CONTROL=/D NR_OLD_ONLOAD_JSON_RETURN"
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" tests\streamline\common_lifecycle\onload_json_ownership.cpp /Fo:"%~1\onload_json_ownership.obj" /Fe:"%~1\onload_json_ownership.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\onload_json_ownership.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
