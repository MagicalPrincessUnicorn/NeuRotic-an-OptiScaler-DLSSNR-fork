@echo off
setlocal
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if not "%errorlevel%"=="0" exit /b 1
set CL=
set _CL_=
set VSTEL_EWDK=1
set "OUT=%~dp0..\builds\universal-guides\transport-tests"
if not exist "%OUT%" mkdir "%OUT%"
pushd "%OUT%"
cl /nologo /std:c++20 /EHsc /W4 /WX /DNOMINMAX /I"%~dp0..\OptiScaler" /I"%~dp0..\addons\prepared-guides\external\reshade\include" /I"C:\NeuRotic\external\vulkan\include" "%~dp0prepared_transport_tests.cpp" "%~dp0..\OptiScaler\dlssnr\PreparedVkDx12Transport.cpp" "%~dp0..\addons\prepared-guides\src\SharedGpuClient.cpp" /Fe:prepared_transport.exe /Fo:.\ /link /INCREMENTAL:NO d3d12.lib dxgi.lib
if not "%errorlevel%"=="0" (popd & exit /b 1)
prepared_transport.exe
set "NR_TRANSPORT_RESULT=%errorlevel%"
popd
exit /b %NR_TRANSPORT_RESULT%
