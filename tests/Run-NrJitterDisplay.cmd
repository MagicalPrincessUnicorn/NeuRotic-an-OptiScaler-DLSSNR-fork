@echo off
setlocal
cd /d "%~dp0.."
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "OUT=builds\capability-facts\jitter-display"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_jitter_display.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj"
if errorlevel 1 exit /b 1
"%OUT%\test.exe"
exit /b %errorlevel%
