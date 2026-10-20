@echo off
setlocal
if "%~1"=="" exit /b 2
if not exist "%~1\nvngx_dlssg.dll" exit /b 2
if not exist "%~1\sl.dlss_g.dll" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
set "OUT=builds\native-mfg\tests"
if not exist "%OUT%" mkdir "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX tests\nr_mfg_canonical.cpp /Fe:"%OUT%\nr_mfg_canonical.exe" /Fo:"%OUT%\nr_mfg_canonical.obj" /link version.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\nr_mfg_canonical.exe" "%~1\nvngx_dlssg.dll" "%~1\sl.dlss_g.dll"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
