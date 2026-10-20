@echo off
setlocal
if "%~1"=="" exit /b 2
if not exist "%~1\source_set_pointer_type.inc" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0..\..\.."
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /MT /I OptiScaler\include /I "%~1" /FAcs /Fa"%~1\parameter_pointer_abi.asm" tests\streamline\common_lifecycle\parameter_pointer_abi.cpp /Fo:"%~1\parameter_pointer_abi.obj" /Fe:"%~1\parameter_pointer_abi.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\parameter_pointer_abi.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
