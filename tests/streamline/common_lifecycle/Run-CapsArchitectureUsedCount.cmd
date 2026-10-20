@echo off
setlocal
if "%~1"=="" exit /b 2
set "CONTROL="
if /I "%~2"=="old" (
 if not exist "%~1\old_caps_architecture_reader.inc" exit /b 2
 set "CONTROL=/D NR_OLD_CAPS_ARCHITECTURE_READER"
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I "%~1" tests\streamline\common_lifecycle\caps_architecture_used_count.cpp /Fo:"%~1\caps_architecture_used_count.obj" /Fe:"%~1\caps_architecture_used_count.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\caps_architecture_used_count.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
