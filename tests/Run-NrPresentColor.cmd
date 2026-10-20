@echo off
setlocal
cd /d "%~dp0.."
set "testOut=C:\NeuRotic\builds\moonshot-w03-reconciliation\present-color"
if not exist "%testOut%" mkdir "%testOut%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_hdr_observation.cpp /Fe:"%testOut%\hdr.exe" /Fo:"%testOut%\hdr.obj" /link dxgi.lib
if errorlevel 1 exit /b 1
"%testOut%\hdr.exe"
if errorlevel 1 exit /b 1
if not exist tests\nr_present_color.cpp exit /b 0
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_present_color.cpp /Fe:"%testOut%\color.exe" /Fo:"%testOut%\color.obj" /link dxgi.lib
if errorlevel 1 exit /b 1
"%testOut%\color.exe"
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\nr_present_compatibility.cpp /Fe:"%testOut%\compatibility.exe" /Fo:"%testOut%\compatibility.obj"
if errorlevel 1 exit /b 1
"%testOut%\compatibility.exe"
exit /b %errorlevel%
