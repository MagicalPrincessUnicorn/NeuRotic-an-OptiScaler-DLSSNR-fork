@echo off
setlocal
if "%~1"=="" exit /b 2
set "CONTROL="
if /I "%~2"=="cached" (
 if not exist "%~1\cached_caps_reader.inc" exit /b 2
 set "CONTROL=/D NR_CACHED_CAPS_READER_CONTROL"
)
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT %CONTROL% /I OptiScaler\include /I "%~1" tests\streamline\common_lifecycle\managed_caps_publication.cpp /Fo:"%~1\managed_caps_publication.obj" /Fe:"%~1\managed_caps_publication.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\managed_caps_publication.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
