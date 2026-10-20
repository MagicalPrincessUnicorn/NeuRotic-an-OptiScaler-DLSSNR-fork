@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set "testOut=%~dp0..\builds\capability-facts\native-temporal-inputs"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\native_temporal_inputs.cpp /Fe:"%testOut%\native_temporal_inputs.exe" /Fo:"%testOut%\native_temporal_inputs.obj" >"%testOut%\build.txt" 2>&1
if not "%errorlevel%"=="0" goto failed
"%testOut%\native_temporal_inputs.exe" >"%testOut%\test.txt" 2>&1
if not "%errorlevel%"=="0" goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
popd
exit /b %testResult%

