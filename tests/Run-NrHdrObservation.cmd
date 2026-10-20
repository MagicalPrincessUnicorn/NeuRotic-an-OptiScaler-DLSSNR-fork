@echo off
setlocal
cd /d "%~dp0.."
set "testOut=%~dp0..\builds\capability-facts\hdr"
if not exist "%testOut%" mkdir "%testOut%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul 2>&1
if not "%ERRORLEVEL%"=="0" exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_hdr_observation.cpp /Fe:"%testOut%\nr_hdr_observation.exe" /Fo:"%testOut%\nr_hdr_observation.obj" /link dxgi.lib >"%testOut%\build.txt" 2>&1
if not "%ERRORLEVEL%"=="0" exit /b 1
"%testOut%\nr_hdr_observation.exe" >"%testOut%\test.txt" 2>&1
if not "%ERRORLEVEL%"=="0" exit /b 1
type "%testOut%\test.txt"
endlocal

