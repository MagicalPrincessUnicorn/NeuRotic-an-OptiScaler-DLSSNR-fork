@echo off
setlocal
cd /d "%~dp0..\.."
set "OUT=builds\universal-input\pipeline-tests"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler tests\universal_connections\nr_pipeline_view.cpp /Fo"%OUT%\\" /Fe"%OUT%\nr_pipeline_view.exe"
if errorlevel 1 exit /b 1
"%OUT%\nr_pipeline_view.exe"
exit /b %errorlevel%
