@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-present-converter-lifetime"
if not exist "%testOut%" mkdir "%testOut%"
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /I tests\present_conversion_stubs /I OptiScaler /I OptiScaler\include /I external\magic_enum\include\magic_enum tests\nr_present_conversion.cpp OptiScaler\shaders\Shader_Dx12.cpp OptiScaler\shaders\format_transfer\FT_Dx12.cpp /Fe:"%testOut%\conversion.exe" /Fo:"%testOut%\\" /link d3d12.lib dxgi.lib d3dcompiler.lib >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\conversion.exe" >"%testOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
