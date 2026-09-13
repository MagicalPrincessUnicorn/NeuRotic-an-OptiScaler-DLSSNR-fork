@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\artifacts\0.9.6-advisor-safety-opticlip\advisor-input"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_advisor_input.cpp /Fe:"%testOut%\test.exe" /Fo:"%testOut%\test.obj" >"%testOut%\build.txt" 2>&1
set "testExit=%ERRORLEVEL%"
if not "%testExit%"=="0" goto done
"%testOut%\test.exe" >"%testOut%\test.txt" 2>&1
set "testExit=%ERRORLEVEL%"
:done
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testExit%
