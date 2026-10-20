@echo off
setlocal
set "ROOT=%~dp0.."
set "OUT=%ROOT%\builds\moonshot-w03-reconciliation\swapchain-extent"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
pushd "%ROOT%"
cl /nologo /std:c++20 /EHsc /W4 /UNDEBUG /I OptiScaler tests\nr_swapchain_extent.cpp /Fe:"%OUT%\test.exe" /Fo:"%OUT%\test.obj" /link d3d11.lib d3d12.lib dxgi.lib user32.lib
if errorlevel 1 (popd & exit /b 1)
"%OUT%\test.exe"
set "testResult=%ERRORLEVEL%"
popd
exit /b %testResult%
