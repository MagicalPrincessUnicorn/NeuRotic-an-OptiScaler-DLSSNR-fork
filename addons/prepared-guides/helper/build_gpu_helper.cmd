@echo off
setlocal
set "ROOT=%~dp0..\..\.."
set "OUT=%ROOT%\builds\universal-connections\helper"
set "FLOW=%ROOT%\builds\universal-guides\flow"
if not defined NR_FFX_SDK set "NR_FFX_SDK=C:\NeuRotic\external\FidelityFX-SDK"
if not exist "%FLOW%\neurotic_prepared_flow.lib" exit /b 2
if not exist "%OUT%\production" mkdir "%OUT%\production"
call "C:\Program Files\Microsoft Visual Studio\18\Community\VC\Auxiliary\Build\vcvars64.bat" -vcvars_ver=14.44 >nul
if errorlevel 1 exit /b 1
set CL=
set _CL_=
pushd "%OUT%\production"
cl /nologo /std:c++20 /EHsc /O2 /MD /W4 /DNOMINMAX /I"%ROOT%\OptiScaler" /I"C:\NeuRotic\Optiscaler-Stuff\external\FidelityFX-SDK\ffx-api\include\ffx_api" "%~dp0PreparedGpuMain.cpp" "%~dp0PreparedGpuHost.cpp" "%ROOT%\OptiScaler\dlssnr\NativeGuideProcessor.cpp" "%ROOT%\OptiScaler\dlssnr\NativeFsr3D3D12.cpp" "%~dp0NativeFsrUnavailable.cpp" "%ROOT%\addons\prepared-guides\src\CpuMainlineClient.cpp" /Fe:NeuRotic.PreparedGpuHost.exe /link /INCREMENTAL:NO "%FLOW%\neurotic_prepared_flow.lib" "%FLOW%\lib\ffx_opticalflow_x64.lib" "%FLOW%\lib\ffx_backend_dx12_x64.lib" "%NR_FFX_SDK%\sdk\libs\pix\bin\x64\WinPixEventRuntime.lib" d3d12.lib dxgi.lib d3dcompiler.lib
if errorlevel 1 goto fail
copy /y "%NR_FFX_SDK%\sdk\libs\pix\bin\x64\WinPixEventRuntime.dll" . >nul
popd
exit /b 0
:fail
popd
exit /b 1
