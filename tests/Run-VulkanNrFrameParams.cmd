@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "frameTestOut=C:\OptiScaler-NR-Dev\logs\vulkan-nr-resource-params"
if not exist "%frameTestOut%" mkdir "%frameTestOut%"
if not exist "%frameTestOut%" exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I external\nvngx_dlss_sdk /I external\vulkan\include tests\vulkan_nr_frame_params.cpp /Fe:"%frameTestOut%\frame_params.exe" /Fo:"%frameTestOut%\frame_params.obj" >"%frameTestOut%\compile.txt" 2>&1
set "frameCompileExit=%ERRORLEVEL%"
type "%frameTestOut%\compile.txt"
if not "%frameCompileExit%"=="0" (popd & exit /b %frameCompileExit%)
"%frameTestOut%\frame_params.exe" >"%frameTestOut%\test.txt" 2>&1
set "frameTestExit=%ERRORLEVEL%"
type "%frameTestOut%\test.txt"
popd
exit /b %frameTestExit%
