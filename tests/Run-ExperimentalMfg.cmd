@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
set "testOut=builds\native-mfg\tests"
if not exist "%testOut%" mkdir "%testOut%"
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_experimental_mfg.cpp /Fe:"%testOut%\nr_experimental_mfg.exe" /Fo:"%testOut%\nr_experimental_mfg.obj"
if errorlevel 1 (popd & exit /b 1)
"%testOut%\nr_experimental_mfg.exe"
set "result=%errorlevel%"
popd
exit /b %result%
