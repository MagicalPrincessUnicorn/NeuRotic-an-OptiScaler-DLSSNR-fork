@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
set "ROOT=%~dp0.."
set "testOut=%ROOT%\builds\capability-facts\preflight-signals"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_preflight_signals.cpp /Fe:"%testOut%\test.exe" /Fo:"%testOut%\test.obj" >"%testOut%\build.txt" 2>&1
set "testExit=%ERRORLEVEL%"
if not "%testExit%"=="0" goto done
"%testOut%\test.exe" >"%testOut%\test.txt" 2>&1
set "testExit=%ERRORLEVEL%"
:done
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testExit%
