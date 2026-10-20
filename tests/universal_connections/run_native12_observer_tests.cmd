@echo off
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\builds\universal-connections\native12"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /I"%ROOT%\OptiScaler" /I"%ROOT%\OptiScaler\include" /I"%ROOT%\external\nvngx_dlss_sdk" /I"%ROOT%\external\spdlog\include" /c "%ROOT%\OptiScaler\dlssnr\NativeD3D12Guides.cpp" /Fo:native12_production.obj
if errorlevel 1 goto fail
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DNR_NATIVE12_TESTING /I"%ROOT%\OptiScaler" /I"%ROOT%\OptiScaler\include" "%ROOT%\tests\universal_connections\native_d3d12_observer.cpp" "%ROOT%\OptiScaler\dlssnr\NativeD3D12Guides.cpp" /Fe:native12_observer.exe /link /INCREMENTAL:NO "%ROOT%\OptiScaler\library\detours\detours.lib" d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 goto fail
native12_observer.exe
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
