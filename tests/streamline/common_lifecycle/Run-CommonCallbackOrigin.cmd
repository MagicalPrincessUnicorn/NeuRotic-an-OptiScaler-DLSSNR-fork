@echo off
setlocal
if "%~1"=="" exit /b 2
if "%~2"=="" exit /b 2
if not exist "%~2\NrCommonFixtureA.dll" exit /b 2
if not exist "%~2\NrCommonFixtureB.dll" exit /b 2
set "CONTROL="
if /I "%~3"=="old" (
 if not exist "%~1\old_common_callback_origin.inc" exit /b 2
 set "CONTROL=/D NR_OLD_COMMON_CALLBACK_ORIGIN"
)
if /I not "%~3"=="old" (
 if not exist "%~1\green_common_callback_origin.inc" exit /b 2
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" tests\streamline\common_lifecycle\common_callback_origin.cpp /Fo:"%~1\common_callback_origin.obj" /Fe:"%~1\common_callback_origin.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\common_callback_origin.exe" "%~2"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
