@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
if not exist "%~1" mkdir "%~1"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /I external\streamline tests\nr_mfg_options_abi.cpp /Fe:"%~1\nr_mfg_options_abi.exe" /Fo:"%~1\nr_mfg_options_abi.obj"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\nr_mfg_options_abi.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
