@echo off
setlocal
cd /d "%~dp0.."
set "testOut=C:\NeuRotic\builds\moonshot-w03-reconciliation\present-color"
if not exist "%testOut%" mkdir "%testOut%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul 2>&1
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /I tests\present_conversion_stubs /I OptiScaler /I OptiScaler\include /I external\magic_enum\include\magic_enum tests\nr_present_composition_warp.cpp OptiScaler\shaders\Shader_Dx12.cpp OptiScaler\shaders\format_transfer\FT_Dx12.cpp /Fe:"%testOut%\composition-warp.exe" /Fo:"%testOut%\\" /link d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 exit /b 1
"%testOut%\composition-warp.exe"
exit /b %errorlevel%
