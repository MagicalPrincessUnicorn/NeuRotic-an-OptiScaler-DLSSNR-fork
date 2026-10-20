@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
set VSTEL_EWDK=1
set "OUT=%~dp0..\builds\universal-guides\tests"
if not defined NR_DEPENDENCIES set "NR_DEPENDENCIES=C:\NeuRotic\Optiscaler-Stuff\external"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" /I"%NR_DEPENDENCIES%\vulkan\include" "%~dp0prepared_vulkan_device.cpp" /Fe:vulkan-device.exe /Fo:vulkan-device.obj /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
vulkan-device.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%NR_DEPENDENCIES%\nvngx_dlss_sdk" "%~dp0prepared_dlaa.cpp" /Fe:dlaa.exe /Fo:dlaa.obj /link /INCREMENTAL:NO d3d11.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 (popd & exit /b 1)
dlaa.exe
if errorlevel 1 (popd & exit /b 1)
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX "%~dp0prepared_color.cpp" /Fe:color.exe /Fo:color.obj /link /INCREMENTAL:NO
if errorlevel 1 (popd & exit /b 1)
color.exe
if errorlevel 1 (popd & exit /b 1)
popd
echo PASS: Vulkan preparation, DLAA shaders/refusal, source color
exit /b 0
