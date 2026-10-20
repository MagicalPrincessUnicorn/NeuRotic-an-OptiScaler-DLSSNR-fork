@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
pushd "%~dp0.."
cl /nologo /std:c++20 /EHsc /W4 /WX /I . /I OptiScaler /I external\vulkan\include "%~1\vulkan_guide_recording.cpp" /Fe:"%~1\vulkan_guide_recording.exe" /Fo:"%~1\\"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\vulkan_guide_recording.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
