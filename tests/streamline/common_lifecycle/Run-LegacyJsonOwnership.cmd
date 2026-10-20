@echo off
setlocal
if "%~1"=="" exit /b 2
set "CONTROL="
if /I "%~2"=="old" (
 if not exist "%~1\old_legacy_json_return.inc" exit /b 2
 set "CONTROL=/D NR_OLD_LEGACY_JSON_RETURN"
)
if /I not "%~2"=="old" (
 if not exist "%~1\green_legacy_json_return.inc" exit /b 2
 if not exist "%~2\NrCommonFixtureA.dll" exit /b 2
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" /I external\nlohmann tests\streamline\common_lifecycle\legacy_json_ownership.cpp /Fo:"%~1\legacy_json_ownership.obj" /Fe:"%~1\legacy_json_ownership.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\legacy_json_ownership.exe" "%~2"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
