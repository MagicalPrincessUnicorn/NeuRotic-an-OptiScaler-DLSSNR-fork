@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
set "testOut=C:\OptiScaler-NR-Dev\logs\nr-native-dx11-%RANDOM%-%RANDOM%"
if exist "%testOut%" exit /b 2
mkdir "%testOut%"
if errorlevel 1 exit /b 2
echo Evidence: %testOut%
pushd "%~dp0.."
if errorlevel 1 exit /b 1
cl /nologo /std:c++20 /EHsc /W4 /I OptiScaler /I OptiScaler\include tests\nr_native_dx11.cpp /Fe:"%testOut%\transport.exe" /Fo:"%testOut%\\" /link d3d11.lib d3d12.lib dxgi.lib d3dcompiler.lib OptiScaler\library\detours\detours.lib >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
"%testOut%\transport.exe" %* >"%testOut%\test.txt" 2>&1
if errorlevel 1 goto failed
type "%testOut%\test.txt"
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
if exist "%testOut%\test.txt" type "%testOut%\test.txt"
popd
exit /b %testResult%
