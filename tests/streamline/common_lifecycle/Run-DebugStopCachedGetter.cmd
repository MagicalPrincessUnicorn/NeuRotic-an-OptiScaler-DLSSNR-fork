@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT tests\streamline\common_lifecycle\debug_stop_cached_getter.cpp /Fo:"%~1\debug_stop_cached_getter.obj" /Fe:"%~1\debug_stop_cached_getter.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\debug_stop_cached_getter.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
