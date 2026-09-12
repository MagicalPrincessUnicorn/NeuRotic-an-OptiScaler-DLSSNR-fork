@echo off
setlocal
rem Pinned upstream host, linked directly to OptiScaler's NGX exports in dxgi.dll.
rem No ReShade, external NR consumer, static NVIDIA loader, or third-party bridge.
set "gym=C:\OptiScaler-NR-Dev\artifacts\dependencies\ngxGym-fbaca339\NIGos-ngxGym-fbaca33"
set "testOut=C:\OptiScaler-NR-Dev\artifacts\native-dx11-ngxgym\host"
if not exist "%gym%\src\d3d11.cpp" exit /b 2
if not exist "%testOut%" mkdir "%testOut%"
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul
if errorlevel 1 exit /b 1
pushd "%~dp0.."
if errorlevel 1 exit /b 1
lib /nologo /def:tests\nr_ngxgym_exports.def /machine:x64 /out:"%testOut%\ngxgym_proxy.lib" >"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
cl /nologo /std:c++17 /MT /EHsc /W4 /I OptiScaler\include /I C:\OptiScaler-NR-Dev\artifacts\dependencies\nvidia-dlss-headers /I "%gym%\src" tests\nr_ngxgym_host.cpp tests\nr_ngxgym_params.cpp /Fe:"%testOut%\ngxGym-d3d11.exe" /Fo:"%testOut%\\" /link "%testOut%\ngxgym_proxy.lib" d3d11.lib dxgi.lib d3dcompiler.lib user32.lib advapi32.lib shlwapi.lib dbghelp.lib OptiScaler\library\detours\detours.lib >>"%testOut%\build.txt" 2>&1
if errorlevel 1 goto failed
copy /y "%gym%\LICENSE" "%testOut%\ngxGym-MIT-LICENSE.txt" >nul
popd
exit /b 0
:failed
set "testResult=%ERRORLEVEL%"
type "%testOut%\build.txt"
popd
exit /b %testResult%
