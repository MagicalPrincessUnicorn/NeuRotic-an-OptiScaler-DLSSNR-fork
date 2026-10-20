@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "NR_OUT=%~dp0..\builds\native-guides\shared-capabilities"
if not exist "%NR_OUT%" mkdir "%NR_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" /I"C:\NeuRotic\external\vulkan\include" "%~dp0native_shared_capabilities.cpp" "%~dp0..\OptiScaler\dlssnr\PreparedVkDx12Transport.cpp" /Fo"%NR_OUT%\\" /Fe"%NR_OUT%\native_shared_capabilities.exe" /link "%~dp0..\OptiScaler\library\vulkan\vulkan-1.lib" d3d12.lib dxgi.lib
if errorlevel 1 exit /b 1
"%NR_OUT%\native_shared_capabilities.exe"
exit /b %errorlevel%
