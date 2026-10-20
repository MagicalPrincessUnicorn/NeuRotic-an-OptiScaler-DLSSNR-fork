@echo off
setlocal
if "%~1"=="" exit /b 2
if /I "%~2"=="legacy" if not exist "%~1\legacy_caps_set_arch.inc" exit /b 2
set "CONTROL="
if /I "%~2"=="legacy" set "CONTROL=/D NR_LEGACY_CAPS_RESTORE_CONTROL"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" tests\streamline\common_lifecycle\scoped_caps_restore.cpp /Fo:"%~1\scoped_caps_restore.obj" /Fe:"%~1\scoped_caps_restore.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\scoped_caps_restore.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
