@echo off
setlocal
if "%~1"=="" exit /b 2
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%ERRORLEVEL%"=="0" exit /b 1
if not exist "%~1" mkdir "%~1"
pushd "%~dp0.."
set "TEST_NAME=vulkan_nr_streamline_observation"
if not "%~2"=="" set "TEST_NAME=%~2"
cl /nologo /std:c++20 /EHsc /W4 /WX /I OptiScaler /I OptiScaler\include /I external\streamline /I external\simpleini /I external\nvngx_dlss_sdk /I external\vulkan\include tests\%TEST_NAME%.cpp OptiScaler\dlssnr\VulkanNrStreamline.cpp OptiScaler\dlssnr\VulkanNrPreFg.cpp OptiScaler\dlssnr\VulkanNrRecording.cpp /Fe:"%~1\%TEST_NAME%.exe" /Fo:"%~1\\"
if not "%ERRORLEVEL%"=="0" (popd & exit /b 1)
"%~1\%TEST_NAME%.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
