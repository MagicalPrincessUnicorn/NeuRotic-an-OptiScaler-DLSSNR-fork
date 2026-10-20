@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "OUT=builds\native-mfg\tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /O2 /EHsc /W4 /WX /DNR_MFG_RUNTIME_TEST /I tests\mfg-runtime-stubs tests\nr_mfg_stack.cpp OptiScaler\mfg\MfgAdaUnlock.cpp /Fe:"%OUT%\nr_mfg_stack.exe" /Fo:"%OUT%\\" /link psapi.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_mfg_stack.exe"
set "result=%ERRORLEVEL%"
popd
exit /b %result%
