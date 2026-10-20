@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set "NR_OUT=%~dp0..\builds\native-guides\pixels"
if not exist "%NR_OUT%" mkdir "%NR_OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /MD /DNOMINMAX /I"%~dp0..\OptiScaler" "%~dp0native_gpu_pixels.cpp" /Fo"%NR_OUT%\\" /Fe"%NR_OUT%\native_gpu_pixels.exe" /link d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
"%NR_OUT%\native_gpu_pixels.exe"
exit /b %errorlevel%
