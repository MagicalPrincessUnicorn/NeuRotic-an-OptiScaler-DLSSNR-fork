@echo off
setlocal
cd /d "%~dp0.."
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-hdr-observation-test"
if not exist "%testOut%" mkdir "%testOut%"
call "%ProgramFiles%\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_hdr_observation.cpp /Fe:"%testOut%\nr_hdr_observation.exe" /Fo:"%testOut%\nr_hdr_observation.obj" /link dxgi.lib >"%testOut%\build.txt" 2>&1
if errorlevel 1 exit /b 1
"%testOut%\nr_hdr_observation.exe" >"%testOut%\test.txt" 2>&1
if errorlevel 1 exit /b 1
type "%testOut%\test.txt"
endlocal
