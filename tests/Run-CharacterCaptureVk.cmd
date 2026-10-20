@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
cd /d "%~dp0.."
if not exist builds\inspector-motion\vulkan-tests mkdir builds\inspector-motion\vulkan-tests
cl /nologo /std:c++20 /EHsc /O2 /W4 /WX /I OptiScaler /I external\vulkan\include tests\nr\semantic\character_capture_vk_tests.cpp OptiScaler\nr\semantic\character\CharacterCaptureVk.cpp /Fe:builds\inspector-motion\vulkan-tests\capture.exe /Fo:builds\inspector-motion\vulkan-tests\ /link OptiScaler\library\vulkan\vulkan-1.lib >builds\inspector-motion\vulkan-tests\build.log 2>&1
if errorlevel 1 (type builds\inspector-motion\vulkan-tests\build.log & exit /b 1)
builds\inspector-motion\vulkan-tests\capture.exe
exit /b %errorlevel%
