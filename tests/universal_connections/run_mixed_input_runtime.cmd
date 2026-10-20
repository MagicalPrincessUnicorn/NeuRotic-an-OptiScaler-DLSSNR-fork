@echo off
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\builds\universal-input\runtime-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b %errorlevel%
set CL=
set _CL_=
cl /nologo /std:c++20 /EHsc /DNOMINMAX /I"%ROOT%\OptiScaler" "%~dp0mixed_input_runtime.cpp" "%ROOT%\OptiScaler\dlssnr\connections\ConnectionPolicy.cpp" "%ROOT%\OptiScaler\dlssnr\connections\ConnectionSelection.cpp" /Fo"%OUT%\\" /Fe"%OUT%\mixed_input_runtime.exe" /link /INCREMENTAL:NO
if not "%errorlevel%"=="0" exit /b %errorlevel%
"%OUT%\mixed_input_runtime.exe"
exit /b %errorlevel%
