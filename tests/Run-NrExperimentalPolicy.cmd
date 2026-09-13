@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "ROOT=%~dp0.."
set "OUT=C:\OptiScaler-NR-Dev\artifacts\0.9.6-menu-experimental-controls\experimental-policy"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I"%ROOT%\OptiScaler" tests\nr_experimental_policy.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj" >"%OUT%\build.txt" 2>&1
set "RC=%ERRORLEVEL%"
if not "%RC%"=="0" goto done
"%OUT%\test.exe" >"%OUT%\test.txt" 2>&1
set "RC=%ERRORLEVEL%"
:done
type "%OUT%\build.txt"
if exist "%OUT%\test.txt" type "%OUT%\test.txt"
popd
exit /b %RC%
