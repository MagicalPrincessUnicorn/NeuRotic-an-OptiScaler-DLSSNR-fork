@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b %ERRORLEVEL%
cd /d "%~dp0..\.."
if not exist builds\depth-anything-v2\gpu mkdir builds\depth-anything-v2\gpu
call tools\depth-anything-v2\Build-Shaders.cmd
if errorlevel 1 exit /b %ERRORLEVEL%
cl /nologo /std:c++20 /EHsc /W4 /WX /O2 /DNOMINMAX /DWIN32_LEAN_AND_MEAN /Ibuilds\depth-anything-v2\shaders tests\window-worker\depth-anything-v2\GpuTests.cpp /Fobuilds\depth-anything-v2\gpu\ /Febuilds\depth-anything-v2\gpu\GpuTests.exe /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b %ERRORLEVEL%
builds\depth-anything-v2\gpu\GpuTests.exe
exit /b %ERRORLEVEL%
