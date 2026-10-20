@echo off
setlocal
set "ROOT=%~dp0..\.."
set "OUT=%ROOT%\builds\universal-connections\helper"
if not exist "%OUT%" mkdir "%OUT%"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
set CL=
set _CL_=
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /I"%ROOT%\OptiScaler" "%ROOT%\tests\universal_connections\helper_gpu_host_tests.cpp" /Fe:helper_gpu_protocol64.exe /Fo:helper_gpu_protocol64.obj /link /INCREMENTAL:NO
if errorlevel 1 goto fail
helper_gpu_protocol64.exe
if errorlevel 1 goto fail
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /I"%ROOT%\OptiScaler" /c "%ROOT%\addons\prepared-guides\helper\PreparedGpuHost.cpp" "%ROOT%\addons\prepared-guides\helper\PreparedGpuClient.cpp"
if errorlevel 1 goto fail
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DNRPG_CPU_MAINLINE_TESTING /I"%ROOT%\OptiScaler" "%ROOT%\tests\universal_connections\helper_gpu_client.cpp" "%ROOT%\addons\prepared-guides\src\CpuMainlineClient.cpp" "%ROOT%\addons\prepared-guides\helper\GpuRemoteConsumer.cpp" PreparedGpuHost.obj PreparedGpuClient.obj /Fe:helper_gpu_host64.exe /link /INCREMENTAL:NO d3d12.lib dxgi.lib d3dcompiler.lib bcrypt.lib advapi32.lib
if errorlevel 1 goto fail
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars32.bat" -vcvars_ver=14.44 >nul
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /I"%ROOT%\OptiScaler" "%ROOT%\tests\universal_connections\helper_gpu_host_tests.cpp" /Fe:helper_gpu_protocol32.exe /Fo:helper_gpu_protocol32.obj /link /INCREMENTAL:NO
if errorlevel 1 goto fail
helper_gpu_protocol32.exe
if errorlevel 1 goto fail
cl /nologo /std:c++20 /EHsc /W4 /DNOMINMAX /DNRPG_CPU_MAINLINE_TESTING /I"%ROOT%\OptiScaler" "%ROOT%\tests\universal_connections\helper_gpu_client.cpp" "%ROOT%\addons\prepared-guides\src\CpuMainlineClient.cpp" "%ROOT%\addons\prepared-guides\helper\GpuRemoteConsumer.cpp" "%ROOT%\addons\prepared-guides\helper\PreparedGpuHost.cpp" "%ROOT%\addons\prepared-guides\helper\PreparedGpuClient.cpp" /Fe:helper_gpu_client32.exe /link /INCREMENTAL:NO d3d12.lib dxgi.lib d3dcompiler.lib bcrypt.lib advapi32.lib
if errorlevel 1 goto fail
popd
exit /b 0
:fail
popd
exit /b 1
