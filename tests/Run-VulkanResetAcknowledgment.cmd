@echo off
setlocal
if "%~1"=="" exit /b 2
if not exist "%~1\old_model_ack.inc" exit /b 2
if not exist "%~1\current_model_ack.inc" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /MT /I OptiScaler /I external\nvngx_dlss_sdk /I external\vulkan\include /I "%~1" tests\vulkan_reset_acknowledgment.cpp /Fo:"%~1\vulkan_reset_acknowledgment.obj" /Fe:"%~1\vulkan_reset_acknowledgment.exe" /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
"%~1\vulkan_reset_acknowledgment.exe"
set "RESULT=%ERRORLEVEL%"
popd
exit /b %RESULT%
